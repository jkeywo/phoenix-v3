"""Keep the CLI's field discovery and GUI authoring descriptions together."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[2]
WORKSHOP = ROOT / "src/workshop"
fields = json.loads((WORKSHOP / "authoring-core/fields.json").read_text(encoding="utf-8"))
errors = []
references = set()
for path in WORKSHOP.rglob("*.cpp"):
    if "authoring-core" in path.parts:
        continue
    source = path.read_text(encoding="utf-8-sig")
    for identity in re.findall(r'\b(?:edit_registered|edit_shared_field)\([^;]*?"([\w/-]+\.[\w./-]+)"\s*\)', source):
        references.add(identity)
        if identity not in fields:
            errors.append(f"{path.relative_to(ROOT)}: unknown field descriptor {identity}")
    if path.parent.name in ("gameplay-editors", "tree-editor"):
        # Dynamic choices/defaults remain context-dependent. Fixed declarations
        # must live in the shared catalogue, not grow another GUI-only schema.
        if re.search(r'(?<![\w:])(?:integer|boolean|decimal|string)\([^;\n]*,\s*"[^"]+"\s*(?:,\s*(?:-?\d+(?:\.\d+)?|true|false|"[^"]*")\s*)?\)', source):
            errors.append(f"{path.relative_to(ROOT)}: fixed scalar declaration belongs in shared fields")
        if re.search(r'\bchoice\([^;]*?,\s*"[^"]+"\s*,\s*\{\s*"', source):
            errors.append(f"{path.relative_to(ROOT)}: fixed choices belong in shared fields")
    if "WorkshopHistory" in source or re.search(r"\bhistory\.", source):
        errors.append(f"{path.relative_to(ROOT)}: GUI history must use WorkshopAuthoring")
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
