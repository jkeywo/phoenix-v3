"""Build schema-2 shared/split synthetic packs for explicit Controller GUI smokes.

Run from the repository root. No shipped craft, tuning or assets are used.
"""

import json
import zipfile
from pathlib import Path


def build(split):
    definitions = json.loads(Path("tests/fixtures/content/definitions.json").read_text(encoding="utf-8"))
    definitions["schema"] = 4
    definitions["entities"] = definitions["entities"][:1]
    ship = definitions["entities"][0]
    ship["id"] = "test.controllable"
    ship.pop("console")
    ship["motion_model"] = "vector"
    ship["systems"] = {
        "port": {"kind": "engine", "max_speed_mm_s": 17000, "acceleration_mm_s2": 2000},
        "starboard": {"kind": "engine", "max_speed_mm_s": 17000, "acceleration_mm_s2": 1000},
        "steering": {"kind": "steering", "turn_mdeg_s": 90000},
    }
    definitions["effects"] = [{"id": "test.propulsion-work", "kind": "process",
                               "native": "propulsion", "label": "Fixture propulsion"}]
    for equipment in ship["systems"].values():
        equipment["processes"] = {"main": {"process": "test.propulsion-work"}}
    ship["stations"] = [{"id": "operations", "consoles": [{
        "id": "drive", "definition": "test.console", "systems": [],
        "feeds": ["port", "starboard", "steering"], "knowledge_roles": ["crew"],
    }]}]
    owners, instruments, shared = [], [], []
    groups = [("port_drive", ["port"]), ("starboard_drive", ["starboard"])] if split else [
        ("propulsion", ["port", "starboard"])]
    for index, (name, engines) in enumerate(groups):
        commands = [{"id": "speed", "action": "speed_mm_s", "initial": 0,
                     "label_key": "Speed", "consumers": ["$engines"]}]
        equipment = {"engines": {"kinds": ["engine"], "multiple": True}}
        fitted = {"engines": engines}
        if index == 0:
            commands.append({"id": "heading", "action": "heading_mdeg", "label_key": "Course",
                             "consumers": ["$steering"]})
            equipment["steering"] = {"kinds": ["steering"]}
            fitted["steering"] = "steering"
        shared.append({
            "id": "test." + name, "schema": 1, "label": name + " controls",
            "equipment": equipment, "commands": commands,
            "levels": [{"id": "automated", "human": []},
                       {"id": "direct", "human": [command["id"] for command in commands]}],
            "data": {"requested": {"label": "Requested speed", "binding": "command.speed", "placement": "main"}},
            "parts": ["readout"], "part_bindings": {"readout": {"data": ["requested"], "details": True,
                "commands": [command["id"] for command in commands]}},
        })
        owners.append({"id": name, "console": "drive", "widget": "test." + name,
                       "equipment": fitted, "star_levels": {"1": "automated", "2": "direct", "3": "direct"}})
        instruments.append({"id": name, "kind": "throttle", "label": name + " throttle", "controller": name,
                            "channel": "speed", "x": index * 180, "y": 0, "width": 180, "height": 1000})
    instruments.append({"id": "course", "kind": "heading", "label": "Course", "controller": owners[0]["id"],
                        "channel": "heading", "x": 360, "y": 200, "width": 200, "height": 500})
    ship["control_layout"] = {
        "schema": 2, "default_profile": "direct", "controllers": owners,
        "profiles": [{"id": "direct", "stars": 2}, {"id": "automatic", "stars": 1}],
    }
    definitions["knowledge"] = {"roles": ["crew"], "schemas": [{
        "id": "engine_report", "producers": ["crew"], "readers": ["crew"], "ship_publishable": True,
        "fields": [{"id": field, "type": "integer", "publishable": True} for field in ("amount", "vx", "vy")],
    }]}
    definitions["messages"] = []
    ship["message_templates"] = ["test.engine_console", "test.engine_public"]
    for public in (False, True):
        definitions["messages"].append({
            "id": "test.engine_public" if public else "test.engine_console", "schema": "engine_report",
            "title_key": "Engine public report" if public else "Engine Console report",
            "body_key": "Port engine requested speed: {amount} mm/s.",
            "destination": {"kind": "ship"} if public else {
                "kind": "controller", "console": "drive", "id": owners[0]["id"]},
            "emergency": public, "urgency": 2 if public else 0, "delay_ticks": 1,
            "expires_after_ticks": 600, "player_command": False,
            "fields": {"amount": {"argument": "amount"},
                       "vx": {"observation": "own.vx"}, "vy": {"observation": "own.vy"}},
            "parameters": {"amount": {"argument": "amount"}},
        })
    # A dedicated, always-AI reporting policy observes permitted machinery state.
    # It never writes the human's commands or treats urgency as public authority.
    shared.append({
        "id": "test.engine-reporter", "schema": 1, "label": "Engine officer",
        "equipment": {"engine": {"kinds": ["engine"]}}, "commands": [],
        "levels": [{"id": "automated", "human": []}], "data": {}, "parts": ["main"],
        "part_bindings": {"main": {"data": []}},
        "policy": {"initial": "wait", "message_templates": ship["message_templates"],
                   "system_bindings": [{"name": "requested", "system": "$engine", "field": "requested"}],
                   "states": [{"id": "wait", "transitions": [{"target": "wait-motion", "conditions": [
                       {"left": {"observation": "requested"}, "op": "gt", "right": 1000}]}]},
                              {"id": "wait-motion", "transitions": [{"target": "report", "conditions": [
                       {"left": {"observation": axis}, "op": "ne", "right": 0}]}
                       for axis in ("own.vx", "own.vy")]},
                              {"id": "report", "entry": [{"action": "send." + template,
                                  "arguments": [{"observation": "requested"}, False, ""]}
                                  for template in ship["message_templates"]]}]},
    })
    owners.append({"id": "engine-reporter", "console": "drive", "widget": "test.engine-reporter",
                   "equipment": {"engine": "port"},
                   "star_levels": {str(stars): "automated" for stars in (1, 2, 3)}})
    definitions["control_widgets"] = shared
    console = definitions["consoles"][0]
    console["widgets"] = instruments
    console["gameplay_panel"] = {"x": 0.57, "y": 0, "width": 0.38, "height": 0.9}
    # The familiar native instruments remain authored presentation controls.
    # Shared widget readouts use the same command owners at both widths.
    children = [{"id": widget["id"], "kind": "component", "component": widget["id"],
                 "rect": {key: widget[key] for key in ("x", "y", "width", "height")}}
                for widget in instruments]
    children.extend({"id": "readout-" + owner["id"], "kind": "component",
                     "component": "control/" + owner["id"] + "/readout",
                     "rect": {"x": 570, "y": 20 + index * 150, "width": 380, "height": 140}}
                    for index, owner in enumerate(owners[:-1]))
    console["layout"] = {"schema": 1, "root": {"id": "instruments", "kind": "instruments", "children": children},
                         "compact": {"id": "compact", "kind": "column", "children": [
                             {"id": "compact-" + child["id"], "kind": "component", "component": child["component"]}
                             for child in children]}}
    definitions["maps"][0]["entities"] = []
    definitions["maps"][0]["ship_slots"] = [{"id": "test.slot", "x_mm": 0, "y_mm": 0,
        "heading_mdeg": 0, "allowed_ships": [ship["id"]]}]
    return definitions


if __name__ == "__main__":
    Path("build").mkdir(exist_ok=True)
    for split in (False, True):
        path = Path("build/controller-ui-" + ("split" if split else "shared") + ".zip")
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", json.dumps({"schema": 1, "id": "test-core",
                "base": {"render_mode": "sprites", "interface_style": "current"}}))
            archive.writestr("definitions.json", json.dumps(build(split)))
            archive.write("tests/fixtures/content/pixel.png", "assets/pixel.png")
