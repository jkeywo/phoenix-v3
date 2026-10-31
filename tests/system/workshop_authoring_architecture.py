"""Keep the CLI's field discovery and GUI authoring descriptions together."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[2]
WORKSHOP = ROOT / "src/workshop"
fields = json.loads((WORKSHOP / "authoring-core/fields.json").read_text(encoding="utf-8"))
errors = []
# Use balanced lexical arguments: IDs may contain spaces and calls span lines.
import sys
sys.path.insert(0, str(ROOT / "scripts"))
from workshop_authoring_scan import calls, fixed_initializers, static_value

# Regression fixtures for the scanner itself (including constructs the old regex missed).
probe = '\nedit_shared_field("Label", row, /* comment */ "scope.key with spaces");\ninteger(\n row, "new field", 3\n);\nstd::string("ignored"); phoenix::integer(row, "namespaced", 3);'
found = list(calls(probe, {"edit_shared_field", "integer", "string"}))
assert [row[0] for row in found] == ["edit_shared_field", "integer", "integer"]
assert json.loads(probe[slice(*found[0][1][-1])].strip()) == "scope.key with spaces"
assert len(list(fixed_initializers('row["field"] = Json::array({{"name", "value"}});'))) == 1
references = set()
seeds = set()
records = set()
authoring_dirs = {"gameplay-editors", "tree-editor", "layout-editor", "map-editor", "workflow", "preview"}
for path in WORKSHOP.rglob("*"):
    if path.suffix not in (".cpp", ".hpp", ".cppm") or "authoring-core" in path.parts:
        continue
    source = path.read_text(encoding="utf-8-sig")
    for name, spans, _, _ in calls(source, {"edit_registered", "edit_shared_field", "workshop_seed", "workshop_record"}):
        argument = source[slice(*spans[-1])].strip()
        if not argument.startswith('"'):
            continue # helpers forward a descriptor parameter
        identity = json.loads(argument)
        if name == "workshop_seed":
            seeds.add(identity)
        elif name == "workshop_record":
            records.add(identity)
        else:
            references.add(identity)
            if identity not in fields:
                errors.append(f"{path.relative_to(ROOT)}: unknown field descriptor {identity}")
    if path.parent.name in authoring_dirs:
        for name, spans, _, _ in calls(source, {"integer", "boolean", "decimal", "string", "choice", "tool_real_field"}):
            args = [source[slice(*span)].strip() for span in spans]
            if len(args) < 2:
                continue
            if name == "tool_real_field":
                fixed = len(args) == 6 and args[2].startswith('"') and all(static_value(a) for a in args[3:])
            elif name == "choice":
                fixed = len(args) >= 3 and args[1].startswith('"') and args[2].startswith('{') and static_value(args[2])
            else:
                fixed = args[1].startswith('"') and all(static_value(a) for a in args[2:])
            if fixed:
                errors.append(f"{path.relative_to(ROOT)}: fixed {name} declaration belongs in shared fields")
        for name, spans, _, _ in calls(source, {"records", "append_row", "push_back"}):
            args = [source[slice(*span)].strip() for span in spans]
            argument = args[2] if name in ("records", "append_row") and len(args) > 2 else args[0] if args else ""
            if argument.startswith("{") and static_value(argument):
                errors.append(f"{path.relative_to(ROOT)}: fixed record defaults belong in shared prototypes")
        if path.name != "diagnostics.cpp" and list(fixed_initializers(source)):
            errors.append(f"{path.relative_to(ROOT)}: optional-block defaults belong in shared seeds")
    if "WorkshopHistory" in source or re.search(r"\bhistory\.", source):
        errors.append(f"{path.relative_to(ROOT)}: GUI history must use WorkshopAuthoring")
seed_source = (WORKSHOP / "authoring-core/seeds.cpp").read_text(encoding="utf-8-sig")
for identity in seeds:
    if json.dumps(identity) not in seed_source:
        errors.append(f"Unknown shared seed {identity}")
record_source = (WORKSHOP / "authoring-core/records.cpp").read_text(encoding="utf-8-sig")
for identity in records:
    if json.dumps(identity) not in record_source:
        errors.append(f"Unknown shared record {identity}")
# These transformations have semantic behavior beyond generic JSON edits.
for relative, adapter in {"gameplay-editors/ship_graph.cpp": "workshop_ship_graph", "layout-editor/layout.cpp": "workshop_layout_action", "tree-editor/tree.cpp": "workshop_tree_promote"}.items():
    if adapter not in (WORKSHOP / relative).read_text(encoding="utf-8-sig"):
        errors.append(f"{relative}: semantic edits must use shared authoring")
for identity, field in fields.items():
    if not all(key in field for key in ("key", "label", "type", "default")):
        errors.append(f"{identity}: incomplete field description")
for path in (WORKSHOP / "authoring-core").glob("*.cpp*"):
    source = path.read_text(encoding="utf-8-sig")
    if re.search(r'#include\s*[<"](?:imgui|raylib)|import\s+phoenix\.(?:simulation|world|workshop\.shell)', source):
        errors.append(f"{path.relative_to(ROOT)}: authoring core depends on rendering/host")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Workshop shared authoring architecture passed ({len(references)} field descriptions used)")
