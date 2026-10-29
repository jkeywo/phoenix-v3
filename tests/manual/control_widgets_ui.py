"""Create an independent shared-widget pack for an explicitly invoked UI smoke.

Run from the repository root; writes only build/control-widgets-ui.zip.
"""

import json
import zipfile
from pathlib import Path

source = json.loads(Path("tests/fixtures/content/definitions.json").read_text(encoding="utf-8"))
source["schema"] = 2
source["entities"] = source["entities"][:1]
ship = source["entities"][0]
ship["id"] = "test.widget-ship"
ship.pop("console")
ship["motion_model"] = "vector"
ship["systems"] = {
    "engine": {"kind": "engine", "max_speed_mm_s": 17000, "acceleration_mm_s2": 2000},
    "steering": {"kind": "steering", "turn_mdeg_s": 90000},
}
ship["stations"] = [{"id": "operations", "consoles": [{
    "id": "drive", "definition": "test.console", "systems": [], "feeds": ["engine"]
}]}]
source["control_widgets"] = [{
    "id": "test.drive", "schema": 1, "label": "Propulsion",
    "equipment": {"engine": {}},
    "commands": [{"id": "speed", "action": "speed_mm_s", "initial": 3000, "label_key": "Speed", "consumers": ["$engine"]}],
    "levels": [{"id": "automated", "human": []}, {"id": "direct", "human": ["speed"]}],
    "data": {
        "requested": {"label": "Requested speed", "binding": "command.speed", "placement": "main"},
        "actual": {"label": "Maximum speed", "binding": "system.engine.maximum", "placement": "details"},
    },
    "parts": ["readout", "controls"],
    "part_bindings": {
        "readout": {"data": ["requested", "actual"], "details": True},
        "controls": {"commands": ["speed"]},
    },
}]
ship["control_layout"] = {
    "schema": 2, "default_profile": "one",
    "controllers": [{"id": "propulsion", "console": "drive", "widget": "test.drive",
                     "equipment": {"engine": "engine"},
                     "star_levels": {"1": "automated", "2": "direct", "3": "direct"},
                     "data_placement": {"3": {"actual": "main"}}}],
    "profiles": [{"id": "one", "stars": 1}, {"id": "two", "stars": 2}, {"id": "three", "stars": 3}],
}
console = source["consoles"][0]
console["widgets"] = []
console["layout"] = {
    "schema": 1,
    "root": {"id": "wide", "kind": "row", "min_width": 700, "children": [
        {"id": "status", "kind": "component", "component": "control/propulsion/readout"},
        {"id": "controls", "kind": "component", "component": "control/propulsion/controls"},
    ]},
    "compact": {"id": "compact", "kind": "component", "component": "control/propulsion/complete"},
}
source["maps"][0]["entities"] = []
source["maps"][0]["ship_slots"] = [{"id": "test.slot", "x_mm": 0, "y_mm": 0,
                                    "heading_mdeg": 0, "allowed_ships": ["test.widget-ship"]}]
with zipfile.ZipFile("build/control-widgets-ui.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("manifest.json", json.dumps({"schema": 1, "id": "test-core",
                                                 "base": {"render_mode": "sprites", "interface_style": "current"}}))
    archive.writestr("definitions.json", json.dumps(source))
    archive.write("tests/fixtures/content/pixel.png", "assets/pixel.png")
