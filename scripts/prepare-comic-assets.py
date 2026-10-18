"""Prepare tracked training materials from the ignored, immutable study inputs.

Requires Pillow. Run explicitly when changing art; ordinary builds use the
packaged PNGs and never need raw inputs or image-processing dependencies.
"""

import json
import math
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "content/training/assets/comic"
PREFIX = "phoenix-training:assets/comic/"


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def material(sidecar_path, stem, depth_span=0):
    source = load_json(sidecar_path)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    albedo = Image.open(sidecar_path.parent / source["files"]["sprite"]).convert("RGBA")
    albedo.save(OUTPUT / f"{stem}-albedo.png", optimize=True)
    sheet = source.get("sprite_sheet")
    frames = sheet["frames"] if sheet else [{
        "name": source["id"], "rect": [0, 0, *source["image_size"]],
        "pivot": source["pivot"], "markers": source.get("markers", {})}]
    result = {"albedo": PREFIX + f"{stem}-albedo.png", "frames": []}
    if stem in ("courier", "asteroid", "sun"):
        result["radar_icon"] = "ship" if stem == "courier" else stem
    depth_atlas = Image.new("L", albedo.size, 128)
    normal_atlas = Image.new("RGB", albedo.size, (128, 128, 255))
    source_depth = (Image.open(sidecar_path.parent / source["files"]["depth"]).convert("L")
                    if source["files"].get("depth") else None)
    for f in frames:
        x, y, w, h = f["rect"]
        angle = f.get("camera_azimuth_degrees", 0)
        span = depth_span
        if stem == "courier":
            # Longitudinal extent dominates bow/stern relief; broadside sees beam.
            a = math.radians(angle)
            span = math.hypot(1.8 * math.cos(a), .75 * math.sin(a))
        frame = {"name": f["name"], "rect": f["rect"], "pivot": f["pivot"],
                 "azimuth": angle, "depth_scale": span if source_depth else 0,
                 "depth_offset": 0, "attachments": {}}
        for name, marker in f["markers"].items():
            frame["attachments"][name] = {"x": marker[0], "y": marker[1],
                                           "layer": marker[2], "depth_offset": 0}
        if source_depth:
            box = (x, y, x+w, y+h)
            draft = source_depth.crop(box)
            alpha = albedo.crop(box).getchannel("A")
            values = sorted(d for d, a in zip(draft.getdata(), alpha.getdata()) if a > 192)
            low, high = values[len(values)//50], values[len(values)*49//50]
            high = max(high, low+1)
            # Calibrate the robust visible range; retain the supplied shape.
            field = draft.point(lambda v: round(max(.04, min(.96, .04+(v-low)/float(high-low)*.92))*255))
            field = field.filter(ImageFilter.GaussianBlur(max(1, min(w, h)/160)))
            depth_atlas.paste(field, (x, y))
            # Derive matching view-space normals from this calibrated field.
            # A wider stencil avoids turning draft-map brush noise into facets.
            pixels = field.load()
            normals = Image.new("RGB", (w, h))
            normals_out = normals.load()
            step = max(2, min(w, h)//80)
            for py in range(h):
                ya, yb = max(0, py-step), min(h-1, py+step)
                for px in range(w):
                    xa, xb = max(0, px-step), min(w-1, px+step)
                    dx = (pixels[xb, py]-pixels[xa, py])/255 * span*h/max(1, xb-xa)
                    dy = (pixels[px, yb]-pixels[px, ya])/255 * span*h/max(1, yb-ya)
                    n = (-dx, dy, 1)
                    length = math.sqrt(sum(v*v for v in n))
                    normals_out[px, py] = tuple(round((v/length*.5+.5)*255) for v in n)
            normal_atlas.paste(normals, (x, y))
            # Inferred hidden nozzles need a depth offset to the aft end of the
            # same calibrated volume, instead of importing a render-layer as Z.
            if stem == "courier":
                for attachment in frame["attachments"].values():
                    current = (pixels[min(w-1, int(attachment["x"])), min(h-1, int(attachment["y"]))]/255-.5)*span
                    aft = -.84*math.cos(math.radians(angle))
                    attachment["depth_offset"] = round(aft-current, 5)
        result["frames"].append(frame)
    if source_depth:
        depth_atlas.save(OUTPUT / f"{stem}-depth.png", optimize=True)
        normal_atlas.save(OUTPUT / f"{stem}-normal.png", optimize=True)
        result.update(depth=PREFIX+f"{stem}-depth.png", normal=PREFIX+f"{stem}-normal.png")
    else:
        result["emissive"] = True
    # Editable, portable sidecars sit beside the packaged maps. Never add
    # metadata to the original art inputs in raw/.
    sidecar = dict(source)
    sidecar.pop("$schema", None)
    sidecar["files"] = {"sprite": f"{stem}-albedo.png"}
    if source_depth:
        sidecar["files"].update(normal=f"{stem}-normal.png", depth=f"{stem}-depth.png")
    sidecar["emissive"] = result.get("emissive", False)
    if "radar_icon" in result:
        sidecar["radar_icon"] = result["radar_icon"]
    sidecar["description"] = f"Packaged {stem} material; calibrated maps and abstract radar symbol. Original source inputs remain in raw/."
    converted_frames = []
    for frame in result["frames"]:
        converted_frames.append({"name": frame["name"], "rect": frame["rect"], "pivot": frame["pivot"],
            "camera_azimuth_degrees": frame["azimuth"], "depth_scale": frame["depth_scale"],
            "depth_offset": frame["depth_offset"], "markers": {
                name: [a["x"], a["y"], a["layer"]] for name, a in frame["attachments"].items()},
            "attachment_depth_offsets": {name: a["depth_offset"] for name, a in frame["attachments"].items()}})
    sidecar["sprite_sheet"] = {"kind": "directions", "frames": converted_frames}
    if source_depth:
        sidecar["normal_encoding"]["quality"] = "derived_from_calibrated_depth"
        sidecar["depth_encoding"]["quality"] = "calibrated_draft"
    (OUTPUT / f"{stem}.sprite.json").write_text(json.dumps(sidecar, indent=2)+"\n", encoding="utf-8")
    return result


def main():
    defs_path = ROOT / "content/training/definitions.json"
    defs = load_json(defs_path)
    defs["schema"] = 3
    courier = material(ROOT/"raw/relighting-v1/courier.sprite.json", "courier", 1)
    asteroid = material(ROOT/"raw/relighting-v1/asteroid.sprite.json", "asteroid", .65)
    sun = material(ROOT/"raw/sun.sprite.json", "sun")
    thrust = material(ROOT/"raw/courier-thrust-eight-angles.sprite.json", "thrust")
    ship = next(e for e in defs["entities"] if e["id"] == "training.survey")
    ship["render"] = {"mode":"eight", "scale":8, "sprite":courier,
                      "engines":{"sprite":thrust, "scale":.4, "range":32, "power":2}}
    # The legacy one-view tug deliberately retains its original presentation.
    tug = next(e for e in defs["entities"] if e["id"] == "training.tug")
    tug["render"].update(sprite=None, engines=None, radar_icon="ship")
    def entity(name, sprite, height, radius):
        return {"id":name, "render":{"mode":"camera", "scale":height,"sprite":sprite},
                "systems":{}, "collision":{"shape":"circle","radius_mm":radius,"mass_grams":0}}
    defs["entities"] = [e for e in defs["entities"] if e["id"] not in ("training.asteroid","training.sun")]
    defs["entities"] += [entity("training.asteroid",asteroid,12,5000), entity("training.sun",sun,14,1000)]
    defs["maps"] = [m for m in defs["maps"] if m["id"] != "training.lighting"]
    defs["maps"].append({"id":"training.lighting", "lighting":{
        "sun_direction":[-.5,.6,.6], "sun_color":[1,.89,.7], "ambient":.14,
        "hatch_thickness":2, "hatch_spacing":6, "shadows":True},
        "entities":[{"id":10,"definition":"training.asteroid","x_mm":-24000,"y_mm":0,"heading_mdeg":0},
                    {"id":11,"definition":"training.sun","x_mm":-40000,"y_mm":-60000,"heading_mdeg":0}],
        "ship_slots":[{"id":"training.player","x_mm":0,"y_mm":0,"heading_mdeg":0,"allowed_ships":["training.survey"]}]})
    defs_path.write_text(json.dumps(defs, indent=2)+"\n", encoding="utf-8")
    print("Prepared calibrated comic training assets; original raw inputs unchanged.")


if __name__ == "__main__":
    main()
