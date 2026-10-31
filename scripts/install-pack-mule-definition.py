"""One-time migration of Morrow to an independent, unarmed Pack Mule fitting.

Reuses common engineering templates, never entity inheritance from Courier.
Reruns refresh only the freighter material; mission work stays scenario-owned.
"""

import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "content/base/definitions.json"
KEEP = {"drive", "helm", "radar", "team-1", "team-2", "reactor",
        "shield-cell", "shield-fore", "shield-aft", "comms-radio"}
ASSEMBLY_SYSTEMS = {"ftl.drive", "ftl.supply", "ftl.cooling"}
CONTROL = KEEP | ASSEMBLY_SYSTEMS | {"external-comms", "personnel", "bridge", "navigation.orders"}


def merge_patch(base, patch):
    if not isinstance(patch, dict):
        return copy.deepcopy(patch)
    result = copy.deepcopy(base) if isinstance(base, dict) else {}
    for k, v in patch.items():
        if v is None:
            result.pop(k, None)
        else:
            result[k] = merge_patch(result.get(k), v)
    return result


def replace_power(value):
    if isinstance(value, str):
        return value.replace("combat-power.", "freighter-power.")
    if isinstance(value, list):
        return [replace_power(v) for v in value]
    if isinstance(value, dict):
        return {k: replace_power(v) for k, v in value.items()}
    return value


def clean_fit(entity, occupation):
    keep = KEEP | ({"occupation"} if occupation else set())
    controls = CONTROL | ({"occupation"} if occupation else set())
    if "systems" in entity:
        entity["systems"] = {k: v for k, v in entity["systems"].items() if k in keep and v is not None}
    if "stations" in entity:
        for station in entity["stations"]:
            station["consoles"] = [c for c in station["consoles"] if c["id"] != "tactical"]
            for console in station["consoles"]:
                for key in ("feeds", "systems"):
                    if key in console:
                        console[key] = [s for s in console[key] if s in keep | ASSEMBLY_SYSTEMS]
    if "control_layout" in entity:
        layout = entity["control_layout"]
        layout["controllers"] = [c for c in layout["controllers"] if c["id"] in controls
                                 and not (c["console"] == "tactical" and c["id"] in ("personnel", "bridge"))]
        for c in layout["controllers"]:
            if c["console"] == "tactical":
                c["console"] = "support"
        layout.pop("station_complexity", None)
    if "personnel" in entity:
        people = entity["personnel"]
        people["duties"] = [d for d in people["duties"]
                            if d.get("controller", "personnel") in controls
                            and d.get("team", "team-1") in keep
                            and "tactical" not in d["id"]]
        for d in people["duties"]:
            if d.get("console") == "tactical":
                d["console"] = "support"
            if d.get("department") == "tactical" and d.get("console") == "helm":
                d["department"] = "sailing"
        valid_duties = {d["id"] for d in people["duties"]}
        for officer in people["officers"]:
            if officer.get("duty") not in valid_duties:
                officer.pop("duty", None)
                officer["traits"] = []
        for department in people["departments"]:
            if department["id"] == "tactical":
                department.update(label="Deck crew", station="support")
    # Freighter power has no magazine/weapon store, feed or budget group.
    for key in ("resources", "bridge"):
        entity.pop(key, None)
    return replace_power(entity)


def freighter_consoles(definitions, freighter):
    """Own the civilian layouts; never edit the shared player console layouts."""
    available = {(c["console"], c["id"]) for c in freighter["control_layout"]["controllers"]}

    def prune(node, console):
        component = node.get("component", "")
        if component.startswith("control/"):
            parts = component.split("/")
            target = (parts[1], parts[2]) if len(parts) == 4 else (console, parts[1])
            if target not in available:
                return None
        result = copy.deepcopy(node)
        if "children" in result:
            result["children"] = [v for child in node["children"] if (v := prune(child, console)) is not None]
            if not result["children"]:
                return None
        return result

    mapping = {}
    for station in freighter["stations"]:
        for console in station["consoles"]:
            new_id = "base.console.pack-mule." + console["id"]
            original_id = console["definition"]
            source = copy.deepcopy(next(c for c in definitions["consoles"] if c["id"] == original_id))
            source["id"] = new_id
            # Player Courier help enumerates weapon pages removed below. This
            # NPC fitting must not inherit guidance for absent mechanisms.
            source["help"] = None
            source["layout"]["root"] = prune(source["layout"]["root"], console["id"])
            if "compact" in source["layout"]:
                source["layout"]["compact"] = prune(source["layout"]["compact"], console["id"])
            existing = next((c for c in definitions["consoles"] if c["id"] == new_id), None)
            if existing is None:
                definitions["consoles"].append(source)
            else:
                existing.update(source)
            console["definition"] = new_id
            mapping[original_id] = new_id
    for scene in definitions["maps"]:
        for entity in scene.get("entities", []):
            if entity.get("definition") == freighter["id"]:
                for station in entity.get("overrides", {}).get("stations", []):
                    for console in station["consoles"]:
                        console["definition"] = mapping.get(console["definition"], console["definition"])
                        if console["id"] == "support" and "occupation" in entity.get("overrides", {}).get("systems", {}):
                            # Rescue adds a scenario-owned occupation team.
                            # Its operator belongs on this mission's Support,
                            # not on a removed Courier Tactical console.
                            definition = copy.deepcopy(next(c for c in definitions["consoles"] if c["id"] == "base.console.pack-mule.support"))
                            definition["id"] = "base.console.pack-mule.rescue-support"
                            node = dict(id="occupation", kind="component", component="control/occupation/complete", label="Occupation", min_height=180)
                            page = dict(id="page-occupation", kind="column", label="Occupation", children=[node])
                            for layout_key in ("root", "compact"):
                                if layout_key in definition["layout"]:
                                    definition["layout"][layout_key]["children"].append(copy.deepcopy(page))
                            prior = next((c for c in definitions["consoles"] if c["id"] == definition["id"]), None)
                            if prior is None:
                                definitions["consoles"].append(definition)
                            else:
                                prior.update(definition)
                            console["definition"] = definition["id"]


def main():
    before = PATH.read_text(encoding="utf-8")
    definitions = json.loads(before)
    material = json.loads((ROOT / "raw/pack-mule/pack-mule.material.json").read_text())
    freighter = next((e for e in definitions["entities"] if e["id"] == "base.pack-mule"), None)
    if freighter is None:
        courier = next(e for e in definitions["entities"] if e["id"] == "base.courier")
        morrow = next(e for m in definitions["maps"] for e in m.get("entities", []) if e.get("overrides", {}).get("name") == "Morrow")
        # Freeze the previous mission's ordinary engineering/crew contract into
        # an explicit freighter entity. Later Courier edits cannot change it.
        healthy = clean_fit(merge_patch(courier, morrow["overrides"]), False)
        for key in ("faction", "boarding_missions", "extends"):
            healthy.pop(key, None)
        healthy.update(id="base.pack-mule", name="Pack Mule", class_key="Pack Mule", tags=["ship", "freighter"])
        for deck in healthy["combat"]["decks"]:
            for compartment in deck["compartments"]:
                compartment.pop("initial_hp", None)
        for officer in healthy["personnel"]["officers"]:
            officer.pop("condition", None)
        healthy["logistics"] = dict(controller="team-1", units_per_second=40, stores=[dict(
            id="cargo-hold", controller="team-1", compartment="stern-starboard", main=True,
            stock=dict(capacity_tag="cargo", capacity=400, filter=dict(tags=["cargo"]), initial={}))])
        # Standalone civilian energy assembly: same main/defensive systems, no
        # weapon battery or energy link concealed under a military template.
        power = copy.deepcopy(next(a for a in definitions["assemblies"] if a["id"] == "base.assembly.combat-power"))
        power["id"] = "base.assembly.freighter-power"
        resources = power["resources"]
        resources["stores"] = [s for s in resources["stores"] if s["id"] != "weapons"]
        resources["links"] = [s for s in resources["links"] if s["id"] != "weapon-feed"]
        resources["groups"] = [s for s in resources["groups"] if s["id"] != "weapons"]
        definitions["assemblies"].append(power)
        healthy["assemblies"][0] = dict(id="freighter-power", assembly=power["id"])
        # Override shared equipment defaults that name the common combat grid.
        healthy["systems"]["reactor"].update(generator="freighter-power.reactor", power_store="freighter-power.main")
        healthy["systems"]["shield-cell"]["power_store"] = "freighter-power.shields"
        healthy["render"] = dict(mode="eight", scale=15, sprite=material,
                                  engines=copy.deepcopy(courier["render"]["engines"]))
        healthy["render"]["engines"].update(scale=.3, range=30, power=1)
        definitions["entities"].append(healthy)
        freighter = healthy
        # Preserve mission-owned damage, crew injuries, dialogue, occupation and
        # objectives. Strip obsolete weapon controllers/feeds at this level too.
        overrides = clean_fit(morrow["overrides"], True)
        overrides.pop("render", None)
        overrides.pop("logistics", None)
        morrow.update(definition="base.pack-mule", overrides=overrides)
    else:
        freighter["render"].update(mode="eight", scale=15, sprite=material)
    freighter_consoles(definitions, freighter)
    # Refuse accidental visual or runtime inheritance from the player Courier.
    assert "base.courier" not in json.dumps(freighter)
    assert not any(s.get("kind") in ("weapon", "turret") for s in freighter["systems"].values())
    if PATH.read_text(encoding="utf-8") != before:
        raise RuntimeError("Definitions changed during migration; rerun against current content")
    PATH.write_text(json.dumps(definitions, indent="\t") + "\n", encoding="utf-8")
    print("Morrow now uses independent base.pack-mule: civilian power/cargo, no weapons/turrets/ammunition.")


if __name__ == "__main__":
    main()
