"""Bake registered geometric relief and install the approved Bullfrog Courier.

Pillow/NumPy/SciPy only. Visible albedo pixels are copied, never regenerated. Geometry,
alpha coverage and authored registration drive Z; colour/shadows do not.
"""

import copy
import json
import math
import shutil
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/art/bullfrog-sprites-v1"
RAW = ROOT / "raw/bullfrog"
PACK = ROOT / "content/base/assets/bullfrog"
PREFIX = "phoenix-base:assets/bullfrog/"
FRAME_HEIGHT = 1280
HULL_SCALE = 25  # Same world units/pixel as the previous 15m / 768px material.

# Registered image-space volumes: source coordinates, before atlas padding.
# The hull is a yawed ellipsoid. Pods are separate rounded cylinders/endcaps;
# camera-facing Z comes from their physical forward/port locations, not light.
GEOMETRY = [
    dict(hull=[375, 265, 1165, 730], pods=[
        [1360, 620, 170, 200, -0.12, 0.52],
        [175, 620, 170, 200, -0.12, -0.52]],
        engines=[[1390, 650], [145, 650], [920, 500]],
        top_muzzle=[768, 104], bottom_muzzle=[768, 882],
        cannon=[768, 340], torpedo=[768, 475], sensor=[470, 680]),
    dict(hull=[220, 380, 960, 750], pods=[
        [770, 830, 235, 185, -0.12, 0.52],
        [85, 765, 80, 145, -0.12, -0.52],
        [1110, 565, 130, 160, -0.65, 0]],
        engines=[[975, 820], [210, 735], [1210, 560]],
        top_muzzle=[430, 242], bottom_muzzle=[183, 890],
        cannon=[108, 405], torpedo=[147, 520], sensor=[390, 698]),
    dict(hull=[145, 280, 950, 680], pods=[
        [820, 730, 360, 165, -0.12, 0.52],
        [1310, 435, 205, 160, -0.65, 0]],
        engines=[[1145, 750], [1145, 750], [1500, 450]],
        top_muzzle=[520, 185], bottom_muzzle=[330, 879],
        cannon=[82, 320], torpedo=[125, 420], sensor=[285, 570]),
    dict(hull=[95, 425, 745, 765], pods=[
        [570, 830, 280, 225, -0.12, 0.52],
        [1140, 820, 105, 170, -0.12, -0.52],
        [880, 550, 220, 190, -0.65, 0]],
        engines=[[660, 850], [1160, 805], [980, 560]],
        top_muzzle=[367, 304], bottom_muzzle=[730, 973],
        cannon=[99, 472], torpedo=[78, 555], sensor=[155, 685]),
    dict(hull=[270, 260, 1265, 675], pods=[
        [275, 680, 240, 205, -0.12, 0.52],
        [1265, 680, 240, 205, -0.12, -0.52],
        [768, 405, 235, 195, -0.65, 0]],
        engines=[[275, 680], [1265, 680], [768, 405]],
        top_muzzle=[768, 116], bottom_muzzle=[768, 872],
        cannon=[768, 430], torpedo=[768, 540], sensor=[1120, 685]),
]


def write_json(path, value):
    path.write_text(json.dumps(value, indent="\t") + "\n", encoding="utf-8")


def hull_field(size, angle, geo, offset):
    w, h = size
    yy, xx = np.mgrid[:h, :w].astype(float)
    x0, y0, x1, y1 = geo["hull"]
    cx, cy = (x0 + x1) / 2 + offset[0], (y0 + y1) / 2 + offset[1]
    u, v = (xx - cx) / ((x1 - x0) / 2), (yy - cy) / ((y1 - y0) / 2)
    a, b = .70, .42
    sine, cosine = math.sin(angle), math.cos(angle)
    horizontal_radius = math.hypot(a * sine, b * cosine)
    slope = sine * cosine * (b * b - a * a) / horizontal_radius
    field = slope * np.clip(u, -1, 1) + a * b / horizontal_radius * np.sqrt(np.maximum(0, 1 - u*u - v*v))
    for px, py, rx, ry, forward, port in geo["pods"]:
        eu, ev = (xx - px - offset[0]) / rx, (yy - py - offset[1]) / ry
        radius2 = eu*eu + ev*ev
        weight = np.clip((1 - radius2) * 12, 0, 1)
        centre = forward * cosine + port * sine
        # Separate rounded casing around a recessed aft endplate. The glowing
        # orange centre is deliberately DEEPER than its dark metal nozzle lip.
        casing = centre + .17 + .11 * np.sqrt(np.maximum(0, 1 - radius2))
        if cosine < -.35:
            casing -= .10 * np.exp(-radius2 / .20)
        field = field * (1 - weight) + casing * weight
    return field


def turret_field(alpha, frame, angle):
    h, w = alpha.shape
    yy, xx = np.mgrid[:h, :w].astype(float)
    solid = alpha > 0
    thickness = np.minimum(ndimage.distance_transform_edt(solid), 32)
    rounded = np.sqrt(np.maximum(0, 32**2 - (32 - thickness)**2)) / h
    # Fore/aft depth varies along the actual barrel, not along baked shading.
    px, py = frame["pivot"]
    direction = -math.cos(angle) * math.sin(angle)
    field = rounded + direction * (xx - px) / h * .8
    return field - field[int(py), int(px)]


def encode(field, alpha, span):
    solid = alpha > 0
    near_indices = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    # Extend before smoothing so transparent holes do not make fake black pits
    # or bright silhouette rims in the normal map.
    smooth = ndimage.gaussian_filter(field[tuple(near_indices)], 1.4)
    dy, dx = np.gradient(smooth)
    normals = np.stack((-dx * alpha.shape[0], dy * alpha.shape[0], np.ones_like(field)), axis=-1)
    normals /= np.linalg.norm(normals, axis=-1, keepdims=True)
    normal_rgb = np.rint((normals * .5 + .5) * 255).clip(0, 255).astype(np.uint8)
    encoded = .5 + field / span
    assert encoded[solid].min() > 0 and encoded[solid].max() < 1, "Clipped geometric relief"
    depth = np.rint(encoded * 255).clip(0, 255).astype(np.uint8)
    normal_rgb[~solid] = [128, 128, 255]
    # Retain the proxy through mount holes: CPU attachment sampling reads Z
    # even when albedo at the pivot is transparent.
    return depth, normal_rgb


def point(source, offset):
    return [source[k] + offset[k] for k in (0, 1)]


def nonvisual(entity):
    value = copy.deepcopy(entity)
    value.pop("render", None)
    for system in value.get("systems", {}).values():
        for key in ("visual", "marker", "scale_permille", "flip_y"):
            system.pop(key, None)
    return value


def material(sidecar):
    frames = []
    for f in sidecar["sprite_sheet"]["frames"]:
        frames.append(dict(name=f["name"], rect=f["rect"], pivot=f["pivot"], azimuth=f["camera_azimuth_degrees"],
                           mirror_x=f.get("mirror_x", False), depth_scale=f["depth_scale"], depth_offset=0,
                           attachments={k: dict(x=p[0], y=p[1], layer=p[2], depth_offset=f["attachment_depth_offsets"].get(k, 0))
                                        for k, p in f["markers"].items()}))
    result = dict(albedo=PREFIX + sidecar["files"]["sprite"], normal=PREFIX + sidecar["files"]["normal"],
                  depth=PREFIX + sidecar["files"]["depth"], frames=frames)
    if sidecar["id"].endswith("hull"):
        result["radar_icon"] = "ship"
        result["radar_sprite"] = "phoenix-base:assets/radar/ship.png"
    return result


def main():
    RAW.mkdir(parents=True, exist_ok=True)
    PACK.mkdir(parents=True, exist_ok=True)
    definitions_path = ROOT / "content/base/definitions.json"
    original_text = definitions_path.read_text(encoding="utf-8")
    definitions = json.loads(original_text)
    courier = next(e for e in definitions["entities"] if e["id"] == "base.courier")
    before = nonvisual(courier)
    receipt = json.loads((SOURCE / "extraction-receipt.json").read_text())
    hull_sidecar = json.loads((SOURCE / "bullfrog-hull.sprite.json").read_text())
    all_materials = {}
    fields = {}
    registration = []
    preview = Image.new("RGB", (5 * 460, 3 * 430), "#393649")
    for part in ("hull", "top-turret", "bottom-turret"):
        stem = "bullfrog-" + part
        sidecar = json.loads((SOURCE / (stem + ".sprite.json")).read_text())
        image = Image.open(SOURCE / (stem + ".png")).convert("RGBA")
        depth_atlas = Image.new("L", image.size, 128)
        normal_atlas = Image.new("RGB", image.size, (128, 128, 255))
        unique = []
        for i, f in enumerate(sidecar["sprite_sheet"]["frames"][:5]):
            x, y, w, h = f["rect"]
            alpha = np.array(image.crop((x, y, x+w, y+h)))[:, :, 3]
            angle = math.radians(f["camera_azimuth_degrees"])
            offset = receipt["frames"][i]["integer_offset"]
            geo = GEOMETRY[i]
            span = 3.2 if part == "hull" else 1.8
            field = hull_field((w, h), angle, geo, offset) if part == "hull" else turret_field(alpha, f, angle)
            depth, normals = encode(field, alpha, span)
            depth_atlas.paste(Image.fromarray(depth), (x, y))
            normal_atlas.paste(Image.fromarray(normals), (x, y))
            f.update(depth_scale=span, depth_offset=0)
            markers, offsets = {}, {}
            if part == "hull":
                for kind, forward in (("top", -.08), ("bottom", .17)):
                    name = "turret_" + kind
                    markers[name] = f["markers"][name].copy()
                    markers[name][2] = 1
                    mx, my = markers[name][:2]
                    # At a level camera these centreline mounts sit on Z=F*cos.
                    # Calibrate against the QUANTISED map sampled by runtime,
                    # keeping physical depth independent from painter's layer.
                    sampled = (float(depth[int(my), int(mx)]) / 255 - .5) * span
                    offsets[name] = round(forward * math.cos(angle) - sampled, 7)
                # Original auxiliary mechanisms retain their simulation and
                # use small, separately registered legacy equipment visuals.
                for name, source_point in (("sensor_starboard", geo["sensor"]),
                                           ("aux_beam", geo["cannon"]), ("aux_pdc", [geo["sensor"][0], geo["sensor"][1] + 40])):
                    mx, my = point(source_point, offset)
                    markers[name] = [mx, my, 2]
                    offsets[name] = .015
                markers["muzzle"] = point(geo["cannon"], offset) + [1]
                markers["torpedo"] = point(geo["torpedo"], offset) + [1]
                for j, name in enumerate(("engine_port", "engine_starboard", "engine_stern")):
                    mx, my = point(geo["engines"][j], offset)
                    port = (.68, -.68, 0)[j]
                    forward = (-.35, -.35, -.90)[j]
                    target_z = forward * math.cos(angle) + port * math.sin(angle)
                    sampled = (float(depth[int(my), int(mx)]) / 255 - .5) * span
                    layer = 1 if target_z > 0 else -1
                    markers[name] = [mx, my, layer]
                    offsets[name] = round(target_z - sampled, 7)
                fields[i] = (field, depth, normals)
                registration.append(dict(frame=f["name"], markers=markers, attachment_depth_offsets=offsets))
            else:
                kind = "top" if part == "top-turret" else "bottom"
                mount = hull_sidecar["sprite_sheet"]["frames"][i]["markers"]["turret_" + kind]
                source_mount = [mount[k] - offset[k] for k in (0, 1)]
                source_muzzle = geo[kind + "_muzzle"]
                markers["muzzle"] = [source_muzzle[k] - source_mount[k] + f["pivot"][k] for k in (0, 1)] + [1]
                offsets["muzzle"] = .004
                # Attachment root has zero local depth despite byte 128 being
                # slightly above exact 0.5 in the runtime's 8-bit conversion.
                px, py = map(int, f["pivot"])
                f["depth_offset"] = round(-((float(depth[py, px]) / 255 - .5) * span), 7)
            f["markers"], f["attachment_depth_offsets"] = markers, offsets
            unique.append(copy.deepcopy(f))
        for i, f in enumerate(sidecar["sprite_sheet"]["frames"]):
            source_index = i if i < 5 else 8 - i
            keep = {k: f[k] for k in ("name", "camera_azimuth_degrees", "mirror_x")}
            f.update(copy.deepcopy(unique[source_index]))
            f.update(keep)
            if part == "hull" and i >= 5:
                # Reflection changes which physical side is near the camera.
                for values in (f["markers"], f["attachment_depth_offsets"]):
                    values["engine_port"], values["engine_starboard"] = values["engine_starboard"], values["engine_port"]
        sidecar["files"].update(normal=stem + "-normal.png", depth=stem + "-depth.png")
        sidecar["render_scale"] = HULL_SCALE if part == "hull" else HULL_SCALE * 256 / FRAME_HEIGHT
        sidecar["depth_encoding"] = dict(channel="r", color_space="linear", white="near", quality="registered_geometric_proxy_not_luminance")
        sidecar["normal_encoding"] = dict(space="view", x="right", y="up", z="toward_viewer", quality="gradient_of_continuous_geometric_depth")
        # source sidecars import depth_offset per frame; canonical generation
        # below preserves it as well.
        shutil.copyfile(SOURCE / (stem + ".png"), RAW / (stem + ".png"))
        depth_atlas.save(RAW / (stem + "-depth.png"), optimize=True)
        normal_atlas.save(RAW / (stem + "-normal.png"), optimize=True)
        write_json(RAW / (stem + ".sprite.json"), sidecar)
        canonical = material(sidecar)
        for original, converted in zip(sidecar["sprite_sheet"]["frames"], canonical["frames"]):
            converted["depth_offset"] = original["depth_offset"]
        all_materials[part] = canonical
        write_json(RAW / (stem + ".material.json"), canonical)
        for suffix in (".png", "-depth.png", "-normal.png", ".sprite.json", ".material.json"):
            shutil.copyfile(RAW / (stem + suffix), PACK / (stem + suffix))
        assert (SOURCE / (stem + ".png")).read_bytes() == (RAW / (stem + ".png")).read_bytes()
        # Clearing invisible RGB under alpha=0 saves several MiB of paper grain
        # in the bounded source pack. Visible RGB and every alpha sample stay
        # exact, while the complete original byte stream remains in raw/.
        original_rgba = np.array(image)
        compact = original_rgba.copy()
        compact[compact[:, :, 3] == 0, :3] = 0
        Image.fromarray(compact).save(PACK / (stem + ".png"), optimize=True, compress_level=9)
        saved = np.array(Image.open(PACK / (stem + ".png")).convert("RGBA"))
        visible = original_rgba[:, :, 3] > 0
        assert np.array_equal(saved[:, :, 3], original_rgba[:, :, 3])
        assert np.array_equal(saved[:, :, :3][visible], original_rgba[:, :, :3][visible])
    courier["render"].update(scale=HULL_SCALE, sprite=all_materials["hull"])
    thrust = copy.deepcopy(next(e for e in definitions["entities"] if e["id"] == "base.cruiser")["render"]["engines"])
    thrust.update(scale=.35, range=35, power=1.3)
    courier["render"]["engines"] = thrust
    for kind in ("top", "bottom"):
        entity_id = "base.courier-" + kind + "-turret"
        visual = dict(id=entity_id, render=dict(mode="eight", scale=5, sprite=all_materials[kind + "-turret"]),
                      collision=dict(shape="circle", radius_mm=1, mass_grams=0), systems={})
        existing = next((e for e in definitions["entities"] if e["id"] == entity_id), None)
        if existing:
            existing.update(visual)
        else:
            definitions["entities"].append(visual)
        courier["systems"][kind].update(visual=entity_id, scale_permille=1000, flip_y=False)
    for system, marker in (("beam-turret", "aux_beam"), ("pdc-turret", "aux_pdc")):
        courier["systems"][system].update(marker=marker, scale_permille=90)
    courier["systems"]["sensor-turret"]["scale_permille"] = 90
    assert nonvisual(courier) == before, "Art migration changed simulation data"
    # Old mission-instance frame arrays override inherited arrays wholesale.
    # Let Courier instances inherit the new material/mount registration instead
    # of combining the new Systems with obsolete frame rectangles and sockets.
    for map_definition in definitions.get("maps", []):
        for instance in map_definition.get("entities", []):
            if instance.get("definition") == "base.courier":
                render = instance.get("overrides", {}).get("render", {})
                for key in ("sprite", "engines", "scale"):
                    render.pop(key, None)
    child_path = ROOT / "content/test-missions/definitions.json"
    child_text = child_path.read_text(encoding="utf-8")
    child = json.loads(child_text)
    for map_definition in child.get("maps", []):
        for instance in map_definition.get("entities", []):
            if instance.get("definition") == "base.courier":
                render = instance.get("overrides", {}).get("render", {})
                for key in ("sprite", "engines", "scale"):
                    render.pop(key, None)
    if child_path.read_text(encoding="utf-8") != child_text:
        raise RuntimeError("Test Missions changed during bake; rerun")
    write_json(child_path, child)
    if definitions_path.read_text(encoding="utf-8") != original_text:
        raise RuntimeError("Definitions changed during bake; rerun against the new version")
    write_json(definitions_path, definitions)
    # The replaced hull is no longer referenced. Archive its editable originals
    # outside the bounded production pack instead of shipping duplicate art.
    archive = RAW / "previous-courier-hull"
    archive.mkdir(exist_ok=True)
    for suffix in (".png", "-depth.png", "-normal.png", ".sprite.json", ".material.json"):
        old = ROOT / "content/base/assets/combat" / ("courier-hull" + suffix)
        if old.is_file():
            destination = archive / old.name
            if destination.exists() and destination.read_bytes() != old.read_bytes():
                raise RuntimeError("Refusing to overwrite a different archived hull: " + str(destination))
            shutil.copyfile(old, destination)
            old.unlink()
    write_json(RAW / "geometry-registration.json", dict(geometry=GEOMETRY, mounts=registration,
               depth_source="geometry and alpha only; RGB, brightness and shadows are not inputs"))
    write_json(PACK / "verification.json", dict(raw_albedo_bytes_unchanged=True, visible_albedo_pixels_unchanged=True,
               alpha_unchanged=True, invisible_rgb_cleared_for_lossless_pack_compaction=True, simulation_data_unchanged=True,
               engine_system_count=1, exhaust_marker_count=3, marker_frames=registration,
               depth_source="registered geometric proxies, not colour or shadow", normal_source="continuous geometric Z gradients"))
    # Review all three channels on five bearings. Preview resizing never
    # touches source/production maps.
    for i in range(5):
        x, y, w, h = hull_sidecar["sprite_sheet"]["frames"][i]["rect"]
        for row, suffix in enumerate((".png", "-depth.png", "-normal.png")):
            im = Image.open(PACK / ("bullfrog-hull" + suffix)).convert("RGBA").crop((x, y, x+w, y+h))
            if row:
                im.putalpha(Image.open(PACK / "bullfrog-hull.png").crop((x, y, x+w, y+h)).getchannel("A"))
            im.thumbnail((450, 365))
            bg = Image.new("RGBA", im.size, "#393649")
            bg.alpha_composite(im)
            preview.paste(bg.convert("RGB"), (i*460 + (460-im.width)//2, row*430 + 40))
    preview.save(RAW / "material-preview.png")
    preview.save(ROOT / "docs/art/phoenix-bullfrog-material-preview.png")
    print("Installed Bullfrog materials; visible RGBA identical; raw originals intact; simulation unchanged; three exhausts, one drive.")


if __name__ == "__main__":
    main()
