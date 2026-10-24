"""Generate a rooted, two-Console GUI smoke fixture through the packager.

Explicit manual evidence only; inherits test-only data, never shipped ships.
Run from the repository root after building phoenix_pack.
"""

import copy
import json
from pathlib import Path
import runpy
import subprocess
import zipfile

runpy.run_path("tests/manual/controller_ui.py")
output = Path("build/gui-fixture")
source = output / "source"
source.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile("build/controller-ui-split.zip") as archive:
    definitions = json.loads(archive.read("definitions.json"))
    pixel = archive.read("assets/pixel.png")
entity = definitions["entities"][0]
entity["control_layout"]["controllers"][1]["console"] = "support"
entity["stations"][0]["consoles"].append({
    "id": "support", "definition": "test.console.support", "systems": [],
    "feeds": ["starboard"], "knowledge_roles": ["crew"],
})
for profile in entity["control_layout"]["profiles"]:
    kind = "human" if profile["id"] == "direct" else "ai"
    profile["consoles"] = {"drive": {kind: ["port_drive"]}, "support": {kind: ["starboard_drive"]}}
first = definitions["consoles"][0]
second = copy.deepcopy(first)
second["id"] = "test.console.support"
second["widgets"] = [w for w in second["widgets"] if w.get("controller") == "starboard_drive"]
first["widgets"] = [w for w in first["widgets"] if w.get("controller") != "starboard_drive"]
definitions["consoles"].append(second)
(source / "manifest.json").write_text(json.dumps({
    "schema": 1, "id": "test-core", "base": {"render_mode": "sprites", "interface_style": "current"},
}), encoding="utf-8")
(source / "definitions.json").write_text(json.dumps(definitions), encoding="utf-8")
(source / "assets").mkdir(exist_ok=True)
(source / "assets/pixel.png").write_bytes(pixel)
subprocess.run(["build/native/Release/phoenix_pack.exe", str(source), str(output / "gui.zip")], check=True)
(output / "two-regions.json").write_text(json.dumps({
    "x": 0, "y": 0, "width": 3840, "height": 720,
    "regions": [{"route": index + 1, "x": index * 1280, "y": 0, "width": 1280, "height": 720,
                 "role": "console"} for index in range(2)] + [{"route": 3, "x": 2560, "y": 0, "width": 1280, "height": 720, "role": "viewscreen"}],
}), encoding="utf-8")
