"""Extract approved celestial illustrations and bake camera-facing surface Z.

Explicit offline art preparation: Pillow, NumPy, SciPy. No image generation,
resampling, recolouring or luminance-to-depth conversion. Ordinary builds only
consume packaged PNGs. Sources stay in ignored docs/art and raw/celestial.
"""

import argparse
import copy
import difflib
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "raw/celestial"
PACK = ROOT / "content/base/assets/celestial"
PREFIX = "phoenix-base:assets/celestial/"
ASTEROIDS = [
    ("common-01", "phoenix-generic-asteroid-01-v1.png", 20),
    ("common-02", "phoenix-generic-asteroid-02-v1.png", 20),
    ("common-03", "phoenix-generic-asteroid-03-v1.png", 20),
    ("common-04", "phoenix-generic-asteroid-04-v1.png", 20),
    ("uncommon-01", "phoenix-uncommon-asteroid-01-layered-v1.png", 4),
    ("uncommon-02", "phoenix-uncommon-asteroid-02-saddle-v1.png", 4),
    ("uncommon-03", "phoenix-uncommon-asteroid-03-offset-v1.png", 4),
    ("uncommon-04", "phoenix-uncommon-asteroid-04-scarred-v1.png", 4),
    ("rare-01", "phoenix-asteroid-01-crag-v1.png", 1),
    ("rare-02", "phoenix-asteroid-02-splinter-v1.png", 1),
    ("rare-03", "phoenix-asteroid-03-peanut-v1.png", 1),
    ("rare-04", "phoenix-asteroid-04-arch-v1.png", 1),
]
# Natural negative space enclosed by ink. Coordinates are normalised to canvas.
PAPER_SEEDS = {"rare-04": [(0.49, 0.37)],
               "sun": [(0.18, 0.19), (0.86, 0.74)]}


def write_json(path, data):
    path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")


def extract(source, name):
    """Border-connected paper flood, with registered enclosed background seeds.

    Clear alpha only. Even RGB underneath removed paper is preserved exactly.
    Ivory minerals/clouds/plasma behind their ink outlines remain untouched.
    """
    result = np.array(source.convert("RGBA"))
    r, g, b = [result[:, :, k].astype(np.int16) for k in range(3)]
    pale = ((r >= 215) & (g >= 200) & (b >= 160) & (r >= g - 3)
            & (g >= b - 3) & (r - g <= 30) & (g - b <= 65))
    seeds = np.zeros(pale.shape, bool)
    seeds[0] = pale[0]
    seeds[-1] = pale[-1]
    seeds[:, 0] = pale[:, 0]
    seeds[:, -1] = pale[:, -1]
    h, w = pale.shape
    for u, v in PAPER_SEEDS.get(name, []):
        x, y = round(u * w), round(v * h)
        if not pale[y, x]:
            raise ValueError(f"{name}: registered background seed is not paper")
        seeds[y, x] = True
    paper = ndimage.binary_propagation(seeds, mask=pale)
    result[paper, 3] = 0
    assert np.array_equal(result[:, :, :3], np.array(source.convert("RGB")))
    return result


def surface(alpha, name):
    """Return camera-facing Z in canvas-height units, independent of colour.

    Positive Z points towards the camera. Asteroids use the coverage's rounded
    volume plus registered geological depressions/ridges. These are authored
    2.5D proxies, not recovered hidden meshes or light/shadow intensity maps.
    """
    solid = alpha > 0
    h, w = solid.shape
    yy, xx = np.mgrid[:h, :w].astype(float)
    xs, ys = xx / w, yy / h
    sy, sx = np.nonzero(solid)
    if name == "planet":
        cx, cy = (sx.min() + sx.max()) / 2, (sy.min() + sy.max()) / 2
        radius = ((sx.max() - sx.min()) + (sy.max() - sy.min())) / 4
        z = np.sqrt(np.maximum(0, radius**2 - (xx-cx)**2 - (yy-cy)**2)) / h
        # The night-side ink, ocean, land and cloud pixels share this sphere.
        return z, 2 * radius / h / .92
    distance = ndimage.distance_transform_edt(solid)
    radius = max(1., distance.max())
    t = np.clip(ndimage.gaussian_filter(distance, 2.5) / radius, 0, 1)
    z = .30 * np.sqrt(np.maximum(0, 1 - (1-t)**2))
    # Subtle spatial rock undulations, not source-pixel brightness or noise.
    z += (.009*np.sin(19*xs + 8*ys) + .006*np.cos(11*xs - 23*ys)) * t
    # Elliptical bowls are registered to visible craters/scars in the drawing.
    bowls = {
        "common-01": [(.29, .20, .075, .055, .025), (.26, .32, .05, .04, .02)],
        "rare-01": [(.37, .30, .27, .18, .15)],
        "rare-03": [(.28, .37, .10, .09, .04), (.76, .17, .07, .055, .035)],
        "uncommon-04": [(.55, .37, .22, .065, .065)],
    }
    for cx, cy, rx, ry, depth in bowls.get(name, []):
        a = np.deg2rad(30 if name == "uncommon-04" else 0)
        u = ((xs-cx)*np.cos(a) + (ys-cy)*np.sin(a)) / rx
        v = (-(xs-cx)*np.sin(a) + (ys-cy)*np.cos(a)) / ry
        q = u*u + v*v
        z -= depth * np.maximum(0, 1-q)**2
        z += .012 * np.exp(-((q-1)/.22)**2)
    if name == "uncommon-01":
        # Two broad stepped strata registered across the middle of the rock.
        for row in (.43, .59):
            z += .018 * np.tanh((ys - row + .16*(xs-.5)) / .012) * t
    return z, .9


def maps(alpha, name):
    z, span = surface(alpha, name)
    solid = alpha > 0
    # Continue nearest surface into transparent texels before derivatives;
    # holes do not generate false cliffs to a black background depth.
    nearest = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    field = ndimage.gaussian_filter(z[tuple(nearest)], 1.2)
    dy, dx = np.gradient(field)
    h = alpha.shape[0]
    vector = np.stack((-dx*h, dy*h, np.ones_like(field)), axis=-1)
    vector /= np.linalg.norm(vector, axis=-1, keepdims=True)
    # Five effective signed bits/channel, as in the existing relief pipeline.
    normal = np.rint(128 + np.rint(vector*15)*(127/15)).clip(0, 255).astype(np.uint8)
    depth = np.rint((.5 + field/span)*255).clip(0, 255).astype(np.uint8)
    padded = ndimage.distance_transform_edt(~solid) <= 3
    depth[~padded] = 128
    normal[~padded] = [128, 128, 255]
    if np.max(np.abs(field[solid])) >= span*.49:
        raise ValueError(f"{name}: depth span clips geometry")
    return depth, normal, span


def bake(name, source_name, scale, radar=None):
    source_path = ROOT / "docs/art" / source_name
    source = Image.open(source_path).convert("RGBA")
    rgba = extract(source, name)
    h, w = rgba.shape[:2]
    stem = "asteroid-" + name if name not in ("planet", "sun") else name
    Image.fromarray(rgba).save(PACK / f"{stem}.png", optimize=True)
    frame = dict(name=stem, rect=[0, 0, w, h], pivot=[w/2, h/2],
                 azimuth=0, depth_scale=0, depth_offset=0, attachments={})
    material = dict(albedo=PREFIX+f"{stem}.png", frames=[frame])
    if radar:
        material["radar_sprite"] = "phoenix-base:assets/radar/" + radar + ".png"
    channels = {"albedo": Image.fromarray(rgba)}
    if name == "sun":
        material["emissive"] = True
    else:
        depth, normal, span = maps(rgba[:, :, 3], name)
        for suffix, array in [("depth", depth), ("normal", normal)]:
            im = Image.fromarray(array)
            im.save(PACK / f"{stem}-{suffix}.png", optimize=True)
            channels[suffix] = im
            material[suffix] = PREFIX + f"{stem}-{suffix}.png"
        frame["depth_scale"] = round(span, 6)
    side_frame = copy.deepcopy(frame)
    side_frame["camera_azimuth_degrees"] = side_frame.pop("azimuth")
    side_frame["markers"] = side_frame.pop("attachments")
    files = {"sprite": stem+".png"}
    if name != "sun":
        files.update(normal=stem+"-normal.png", depth=stem+"-depth.png")
    if radar:
        files["radar_icon"] = "../radar/" + radar + ".png"
    sidecar = dict(schema_version=1, coordinate_system="frame_pixels_top_left_y_down",
                   id=stem, image_size=[w, h], files=files, billboard_mode="camera",
                   render_scale=scale, emissive=name == "sun",
                   sprite_sheet=dict(kind="directions", frames=[side_frame]))
    if name != "sun":
        sidecar.update(depth_encoding=dict(channel="r", color_space="linear", white="near",
                       quality="geometric_sphere" if name == "planet" else "silhouette_volume_registered_geology"),
                       normal_encoding=dict(space="view", x="right", y="up", z="toward_viewer",
                       quality="derived_from_continuous_geometric_Z"))
    write_json(PACK / f"{stem}.sprite.json", sidecar)
    write_json(PACK / f"{stem}.material.json", material)
    receipt = dict(source=source_path.relative_to(ROOT).as_posix(),
                   source_sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(),
                   foreground_rgb_changed_pixels=0, all_rgb_preserved=True,
                   resampled=False, alpha_cleared_pixels=int((rgba[:, :, 3] == 0).sum()),
                   image_size=[w, h], depth_scale=frame["depth_scale"])
    return material, channels, receipt


def replace_array(text, key, transforms, additions=()):
    """Edit only selected top-level JSON array entries, preserving other work."""
    decoder = json.JSONDecoder()
    cursor = text.index("[", text.index('"'+key+'"')) + 1
    replacements = []
    while True:
        while text[cursor].isspace() or text[cursor] == ",":
            cursor += 1
        if text[cursor] == "]":
            closing = cursor
            break
        value, length = decoder.raw_decode(text[cursor:])
        if value["id"] in transforms:
            changed = transforms[value["id"]]
            encoded = json.dumps(changed, indent="\t", ensure_ascii=False).replace("\n", "\n\t\t")
            replacements.append((cursor, cursor+length, encoded))
        cursor += length
    if additions:
        encoded = ",\n\t\t" + ",\n\t\t".join(json.dumps(v, indent="\t", ensure_ascii=False).replace("\n", "\n\t\t") for v in additions) + "\n\t"
        # Insert after the last entry, before its original closing whitespace.
        end = len(text[:closing].rstrip())
        replacements.append((end, closing, encoded))
    for start, end, encoded in sorted(replacements, reverse=True):
        text = text[:start] + encoded + text[end:]
    json.loads(text)
    return text


def install(materials):
    types = [dict(definition="base.asteroid-"+name, weight=weight) for name, _, weight in ASTEROIDS]
    assert sum(t["weight"] for t in types) == 100
    changes = []
    for folder in ("base", "test-missions"):
        path = ROOT / "content" / folder / "definitions.json"
        before = path.read_text(encoding="utf-8")
        data = json.loads(before)
        entities = {e["id"]: e for e in data.get("entities", [])}
        changed, added = {}, []
        if folder == "base":
            entities["base.asteroid"]["render"]["sprite"] = materials["common-01"]
            changed["base.asteroid"] = entities["base.asteroid"]
            for name, _, _ in ASTEROIDS:
                eid = "base.asteroid-"+name
                entity = dict(id=eid, extends="base.asteroid",
                              render=dict(sprite=materials[name]))
                if eid in entities:
                    changed[eid] = entity
                else:
                    added.append(entity)
            for eid, key in [("base.sun", "sun"), ("base.planet-billboard", "planet"), ("base.system-star", "sun")]:
                render = entities[eid]["render"]
                render.pop("star", None)
                render.update(mode="camera", sprite=materials[key])
                changed[eid] = entities[eid]
        maps_changed = {}
        for scene in data.get("maps", []):
            for region in scene.get("regions", []):
                field = region.get("asteroids")
                if not field:
                    continue
                for key in ("types", "decorative_types"):
                    old = field.get(key, [])
                    if any(t["definition"].startswith("base.asteroid") for t in old):
                        field[key] = copy.deepcopy(types)
                        maps_changed[scene["id"]] = scene
                        changes.append(dict(pack=folder, map=scene["id"], region=region["id"], layer=key))
        after = replace_array(before, "entities", changed, added) if changed else before
        after = replace_array(after, "maps", maps_changed) if maps_changed else after
        if path.read_text(encoding="utf-8") != before:
            raise RuntimeError("Definitions changed during install; rerun against latest data")
        patch = "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                fromfile=f"a/content/{folder}/definitions.json", tofile=f"b/content/{folder}/definitions.json"))
        (RAW / f"{folder}-install.patch").write_text(patch, encoding="utf-8")
        path.write_text(after, encoding="utf-8")
    return changes


def retire_superseded():
    """Keep old art locally; do not inflate the pack with unused replacements."""
    backup = RAW / "retired"
    backup.mkdir(exist_ok=True)
    definitions = (ROOT / "content/base/definitions.json").read_text(encoding="utf-8")
    patterns = [("comic", "asteroid*"), ("comic", "sun*"), ("rescue", "planet*")]
    for folder, pattern in patterns:
        for path in (ROOT / "content/base/assets" / folder).glob(pattern):
            reference = "phoenix-base:" + path.relative_to(ROOT / "content/base").as_posix()
            if reference in definitions:
                raise ValueError("Retired asset still referenced: " + reference)
            shutil.copyfile(path, backup / (folder+"-"+path.name))
            path.unlink()


def previews(items):
    sheet = Image.new("RGB", (4*410, 3*290), "#252a35")
    for i, (name, channels) in enumerate(items[:12]):
        ox, oy = i % 4 * 410, i // 4 * 290
        ImageDraw.Draw(sheet).text((ox+8, oy+6), name+" | colour / depth / normal", fill="white")
        for key, x, y, size in [("albedo", 0, 25, 270), ("depth", 275, 25, 130), ("normal", 275, 155, 130)]:
            im = channels[key].convert("RGBA")
            im.putalpha(channels["albedo"].getchannel("A"))
            im.thumbnail((size, size))
            bg = Image.new("RGBA", im.size, "#252a35")
            bg.alpha_composite(im)
            sheet.paste(bg.convert("RGB"), (ox+x, oy+y))
    sheet.save(RAW / "asteroid-materials-preview.png")
    sheet = Image.new("RGB", (1600, 430), "#252a35")
    for i, (name, channels) in enumerate(items[12:]):
        for j, (key, value) in enumerate(channels.items()):
            im = value.convert("RGBA")
            im.putalpha(channels["albedo"].getchannel("A"))
            im.thumbnail((350, 350))
            bg = Image.new("RGBA", im.size, "#252a35")
            bg.alpha_composite(im)
            x = 1200 if name == "sun" else j*400
            sheet.paste(bg.convert("RGB"), (x, 45))
            ImageDraw.Draw(sheet).text((x+8, 10), name+" / "+key, fill="white")
    sheet.save(RAW / "planet-sun-materials-preview.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", action="store_true")
    args = parser.parse_args()
    RAW.mkdir(parents=True, exist_ok=True)
    PACK.mkdir(parents=True, exist_ok=True)
    materials, receipts, items = {}, {}, []
    for name, source, _ in ASTEROIDS + [("planet", "phoenix-planet-v1.png", 0), ("sun", "phoenix-sun-v1.png", 0)]:
        scale = 1400 if name == "planet" else 14 if name == "sun" else 12
        radar = None if name == "planet" else "sun" if name == "sun" else "asteroid"
        material, channels, receipt = bake(name, source, scale, radar)
        materials[name], receipts[name] = material, receipt
        items.append((name, channels))
        print("Prepared", name, flush=True)
    changes = install(materials) if args.install else []
    if args.install:
        retire_superseded()
    receipt = dict(method="pale-paper flood; alpha-only edits; no resampling",
               geometry="camera-facing Z from silhouette volumes, registered geology and analytic planet hemisphere; no RGB inputs",
               assets=receipts, fields=changes)
    write_json(RAW / "extraction-receipt.json", receipt)
    write_json(PACK / "registration.json", receipt)
    previews(items)
    print("Saved 14 albedos, 13 depth maps, 13 normal maps and sidecars;", len(changes), "field layers updated")


if __name__ == "__main__":
    main()
