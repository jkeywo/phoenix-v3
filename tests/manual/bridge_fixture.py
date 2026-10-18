"""Create synthetic manual display evidence; never alter shipped content."""
import copy
import json
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[2]
output = root / "build" / "bridge-smoke"
output.mkdir(parents=True, exist_ok=True)
fixture = root / "tests" / "fixtures" / "content"
data = json.loads((fixture / "definitions.json").read_text(encoding="utf-8"))
data["schema"] = 2
base = data["entities"][0]
base.pop("console")
original = data["consoles"][0]
seats = []
for index, systems in enumerate((["engine"], ["steering"], ["radar"], [], []), 1):
    definition = copy.deepcopy(original)
    definition["id"] = f"test.console{index}"
    definition["widgets"] = [
        widget for widget in definition["widgets"]
        if widget["kind"] == "radar"
        or (widget["kind"] == "throttle" and "engine" in systems)
        or (widget["kind"] == "heading" and "steering" in systems)
    ]
    data["consoles"].append(definition)
    seats.append({"id": f"seat{index}", "definition": definition["id"],
                  "systems": systems, "feeds": [] if "radar" in systems else ["radar"]})
base["stations"] = [{"id": "bridge", "consoles": seats}]
# Populated radar and scene, with well-separated synthetic stationary contacts.
for index in range(2, 102):
    data["maps"][0]["entities"].append({
        "id": index, "definition": "test.variant", "x_mm": (index % 10 - 5) * 10000,
        "y_mm": (index // 10 - 5) * 10000, "heading_mdeg": index * 3000})
with zipfile.ZipFile(output / "bridge.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("manifest.json", json.dumps({"schema": 1, "id": "test-core"}))
    archive.writestr("definitions.json", json.dumps(data))
    archive.write(fixture / "pixel.png", "assets/pixel.png")
regions = []
for index in range(6):
    regions.append({"route": index + 1, "x": index % 3 * 1920, "y": index // 3 * 1080,
                    "width": 1920, "height": 1080, "role": "viewscreen" if index == 0 else "console"})
(output / "six-regions.json").write_text(json.dumps({
    "x": 0, "y": 0, "width": 5760, "height": 2160, "regions": regions}), encoding="utf-8")
print(output / "bridge.zip")

# A trusted local inspector alongside the Viewscreen and one player Console.
(output / "gm-regions.json").write_text(json.dumps({
    "x": -1920, "y": 0, "width": 5760, "height": 1200, "regions": [
        {"route": 1, "x": 0, "y": 0, "width": 1920, "height": 1200, "role": "console"},
        {"route": 2, "x": 1920, "y": 0, "width": 1920, "height": 1080, "role": "viewscreen"},
        {"route": 3, "x": 3840, "y": 0, "width": 1920, "height": 1080, "role": "gm"}
    ]}), encoding="utf-8")
