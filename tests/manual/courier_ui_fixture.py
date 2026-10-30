"""Create a disposable Courier GUI smoke scenario from a verified production pack.

This is graphical evidence, not a balance fixture or a production-content golden.
It exposes the rescue mission immediately and puts its contact within local radar
range. All ship fittings, authority, staffing and physical admission are unchanged.
"""

import copy
import json
import sys
import zipfile


def main():
    source, destination = sys.argv[1:]
    with zipfile.ZipFile(source) as original:
        definitions = json.loads(original.read("definitions.json"))
        scenario = copy.deepcopy(
            next(row for row in definitions["maps"] if row["id"] == "base.rescue-under-threat")
        )
        scenario["id"] = "smoke.courier-ui"
        scenario["ship_slots"][0].update(x_mm=0, y_mm=0)
        next(row for row in scenario["entities"] if row["id"] == 1).update(x_mm=0, y_mm=90000)
        for mission in scenario["away_missions"]:
            mission["reveal_event"] = "mission.rescue.rescue.intro"
        next(row for row in scenario["conversations"] if row["id"] == "rescue").pop("start_event", None)
        definitions["maps"].append(scenario)
        with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as output:
            for entry in original.infolist():
                output.writestr(
                    entry,
                    json.dumps(definitions).encode()
                    if entry.filename == "definitions.json"
                    else original.read(entry.filename),
                )
    print(f"Graphical smoke scenario smoke.courier-ui: {destination}")


if __name__ == "__main__":
    main()
