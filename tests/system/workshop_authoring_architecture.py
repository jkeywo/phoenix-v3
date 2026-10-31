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
from workshop_authoring_scan import calls, fixed_initializers, static_value, raw_authoring_widgets, fixed_assignments, tokens

# Regression fixtures for the scanner itself (including constructs the old regex missed).
probe = '\nedit_shared_field("Label", row, /* comment */ "scope.key with spaces");\ninteger(\n row, "new field", 3\n);\nstd::string("ignored"); phoenix::integer(row, "namespaced", 3);'
found = list(calls(probe, {"edit_shared_field", "integer", "string"}))
assert [row[0] for row in found] == ["edit_shared_field", "integer", "integer"]
assert json.loads(probe[slice(*found[0][1][-1])].strip()) == "scope.key with spaces"
assert not list(fixed_assignments('if (row["kind"] == "type") {}'))
assert list(fixed_assignments('row[key] = 7;'))
assert len(list(fixed_initializers('row["field"] = Json::array({{"name", "value"}});'))) == 1
assert raw_authoring_widgets('float colour[3]; if (ImGui::ColorEdit3("Colour", colour)) row["colour"] = colour;', {})
assert raw_authoring_widgets('ImGui /* comment */ :: InputFloat3("New vector", values);', {})
assert not raw_authoring_widgets('ImGui::SliderFloat("Zoom", &view.zoom, .1f, 4.f);', {"&view.zoom": {"widgets": ["SliderFloat"]}})
view_bindings = json.loads((WORKSHOP / "shell/view-bindings.json").read_text(encoding="utf-8"))
references = set()
seeds = set()
records = set()
groups = set()
actions = set()
authoring_dirs = {"gameplay-editors", "tree-editor", "layout-editor", "map-editor", "workflow", "preview"}
for path in WORKSHOP.rglob("*"):
    if path.suffix not in (".cpp", ".hpp", ".cppm") or "authoring-core" in path.parts:
        continue
    source = path.read_text(encoding="utf-8-sig")
    for name, spans, _, _ in calls(source, {"edit_registered", "edit_shared_field", "edit_shared_control", "workshop_seed", "workshop_record", "workshop_record_value", "edit_shared_group", "workshop_custom_action"}):
        index = 2 if name in ("edit_shared_field", "edit_shared_control") else 1 if name in ("edit_registered", "edit_shared_group", "workshop_custom_action") else 0
        if len(spans) <= index:
            continue
        argument = source[slice(*spans[index])].strip()
        if not argument.startswith('"'):
            continue # helpers forward a descriptor parameter
        identity = json.loads(argument)
        if name == "edit_shared_group":
            groups.add(identity)
        elif name == "workshop_custom_action":
            actions.add(identity)
        elif name == "workshop_seed":
            seeds.add(identity)
        elif name in ("workshop_record", "workshop_record_value"):
            records.add(identity)
        else:
            references.add(identity)
            if identity not in fields:
                errors.append(f"{path.relative_to(ROOT)}: unknown field descriptor {identity}")
    if path.parent.name in authoring_dirs:
        for error in raw_authoring_widgets(source, view_bindings.get(path.relative_to(WORKSHOP).as_posix(), {})):
            errors.append(f"{path.relative_to(ROOT)}: {error}")
        for name, spans, _, _ in calls(source, {"integer", "boolean", "decimal", "string", "choice", "tool_real_field"}):
            args = [source[slice(*span)].strip() for span in spans]
            if len(args) < 2:
                continue
            if name == "tool_real_field":
                fixed = len(args) == 6 and args[2].startswith('"') and all(static_value(a) for a in args[3:])
            elif name == "choice":
                fixed = len(args) >= 3 and args[1].startswith('"') and args[2].startswith('{') and static_value(args[2])
            else:
                fixed = len(args) > 2 or (args[1].startswith('"') and all(static_value(a) for a in args[2:]))
            if fixed:
                errors.append(f"{path.relative_to(ROOT)}: fixed {name} declaration belongs in shared fields")
        for name, spans, _, _ in calls(source, {"records", "append_row", "push_back"}):
            args = [source[slice(*span)].strip() for span in spans]
            argument = args[2] if name in ("records", "append_row") and len(args) > 2 else args[0] if args else ""
            if argument.startswith("{") and (name == "records" or static_value(argument)):
                errors.append(f"{path.relative_to(ROOT)}: fixed record defaults belong in shared prototypes")
        if path.name != "diagnostics.cpp" and list(fixed_assignments(source)):
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
for group in groups:
    if not any(field.get("group") == group for field in fields.values()):
        errors.append(f"Unknown shared control group {group}")
action_source = (WORKSHOP / "authoring-core/custom_actions.cpp").read_text(encoding="utf-8-sig")
for action in actions:
    if json.dumps(action) not in action_source:
        errors.append(f"Unknown shared authoring action {action}")
# These transformations have semantic behavior beyond generic JSON edits.
for relative, adapter in {"gameplay-editors/ship_graph.cpp": "workshop_ship_graph", "layout-editor/layout.cpp": "workshop_layout_action", "tree-editor/tree.cpp": "workshop_tree_promote"}.items():
    if adapter not in (WORKSHOP / relative).read_text(encoding="utf-8-sig"):
        errors.append(f"{relative}: semantic edits must use shared authoring")
provider_tokens = tokens((WORKSHOP / "authoring-core/controls.cpp").read_text(encoding="utf-8-sig"))
providers = {json.loads(provider_tokens[at + 2].text) for at in range(len(provider_tokens) - 2) if provider_tokens[at].text == "provider" and provider_tokens[at + 1].text == "==" and provider_tokens[at + 2].text.startswith('"')}
for identity, field in fields.items():
    if not all(key in field for key in ("key", "label", "type", "default")):
        errors.append(f"{identity}: incomplete field description")
    if field["type"] == "array" and not all(key in field for key in ("length", "items")):
        errors.append(f"{identity}: incomplete vector schema")
    if ("minimum" in field) != ("maximum" in field):
        errors.append(f"{identity}: incomplete numeric bounds")
    if field.get("provider") and field["provider"] not in providers:
        errors.append(f"{identity}: unknown dynamic field provider")
for path in (WORKSHOP / "authoring-core").glob("*.cpp*"):
    source = path.read_text(encoding="utf-8-sig")
    if re.search(r'#include\s*[<"](?:imgui|raylib)|import\s+phoenix\.(?:simulation|world|workshop\.shell)', source):
        errors.append(f"{path.relative_to(ROOT)}: authoring core depends on rendering/host")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Workshop shared authoring architecture passed ({len(references)} field descriptions used)")
