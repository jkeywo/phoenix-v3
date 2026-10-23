"""Explicit, reproducible proxy-depth bake for the ten formerly flat game sprites.

Requires numpy, scipy and Pillow. Builds consume the tracked PNG/JSON outputs.
Only alpha coverage and authored view geometry are inputs; colour/lighting never
become depth. These are smooth geometric approximations, not recovered meshes.
Run from any directory: python scripts/prepare-gameplay-relief.py
"""

import copy
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "content/base"
PREFIX = "phoenix-base:"

# Half extents in rendered-height units: longitudinal and beam.
# Broadside artwork establishes length/beam; bow/stern establish cross section.
# Depth remains centred on the entity's authored origin, not each view's minimum.
ASSETS = {
    "combat/courier-hull.png": (1.15, .38),
    "combat/courier-turret.png": (.65, .38),
    "combat/pirate-hull.png": (1.40, .85),
    "combat/pirate-turret.png": (.70, .45),
    "combat/missile-atlas.png": (1.25, .30),
    "cruiser/shuttle-albedo.png": (1.40, .75),
    "cruiser/interceptor-albedo.png": (1.65, .75),
    "cruiser/bomber-albedo.png": (1.45, .85),
    "rescue/starbase.png": (1., .35),
    "rescue/planet.png": (.44, .44),
}


def write_json(path, value):
    path.write_text(json.dumps(value, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")


def coverage_bounds(mask):
    yy, xx = np.nonzero(mask)
    if not len(xx):
        raise ValueError("Empty sprite frame")
    return float(xx.min()), float(yy.min()), float(xx.max()), float(yy.max())


def proxy_field(frame, alpha, shape, asset):
    """Intersect camera rays with a yawed ellipsoid, registered to the silhouette.

    For quarter views, the conditional ellipsoid centre varies with screen X:
    the bow is nearer at 45 degrees, the stern at 135 degrees. A monochrome
    albedo or a symmetric radial bump cannot satisfy this relationship.
    """
    height, width = alpha.shape
    yy, xx = np.mgrid[:height, :width].astype(np.float64)
    mask = alpha > 192
    left, top, right, bottom = coverage_bounds(mask)
    # The torpedo's baked flame is not part of the solid proxy. Retain the colour
    # sprite intact, but continue the nozzle plane through its exhaust pixels.
    if asset == "combat/missile-atlas.png":
        bounds = [(350, 35, 680, 310), (280, 20, 755, 270),
                  (75, 40, 835, 275), (240, 15, 745, 275),
                  (350, 10, 680, 300)]
        left, top, right, bottom = bounds[int(frame["azimuth"]) // 45]
    cx, cy = (left + right) / 2, (top + bottom) / 2
    rx, ry = max(1, (right - left) / 2), max(1, (bottom - top) / 2)
    u, v = (xx - cx) / rx, (yy - cy) / ry
    length, beam = shape
    angle = math.radians(frame["azimuth"])
    sine, cosine = math.sin(angle), math.cos(angle)
    screen_radius = math.hypot(length * sine, beam * cosine)
    depth_radius = math.hypot(length * cosine, beam * sine)
    # Covariance of horizontal and camera-facing coordinates after yaw.
    slope = sine * cosine * (beam * beam - length * length) / screen_radius
    centreline = slope * np.clip(u, -1, 1)
    relief_radius = length * beam / screen_radius
    dome = np.sqrt(np.maximum(0, 1 - u * u - v * v))
    front = centreline + relief_radius * dome
    back = centreline - relief_radius * dome
    if asset == "rescue/starbase.png":
        # A side elevation: local silhouette thickness models hub, arms and tower
        # as rounded volumes. No global lighting ramp across this symmetric view.
        distance = distance_transform_edt(mask)
        radius = max(1, float(distance.max()))
        dome = np.sqrt(np.maximum(0, 1 - (1 - np.clip(distance / radius, 0, 1)) ** 2))
        front, back = beam * dome, -beam * dome
    elif asset == "rescue/planet.png":
        # True spherical hemisphere. Land/cloud colours have zero influence.
        # Use opaque coverage bounds, excluding isolated low-alpha stray pixels.
        radius = (rx + ry) / 2
        sphere = np.sqrt(np.maximum(0, radius * radius - (xx - cx) ** 2 - (yy - cy) ** 2))
        front = sphere / height
        back = -front
        depth_radius = radius / height
    if asset == "combat/missile-atlas.png" and frame["azimuth"] in (45, 90, 135):
        # Flame lies to the right of the nozzle in these source drawings.
        flame = xx > right
        nozzle_x = int(right)
        front[flame] = np.broadcast_to(front[:, nozzle_x:nozzle_x + 1], front.shape)[flame]
        back[flame] = front[flame]
    # The shared calibration is a full signed bounding span, not per-frame
    # histogram stretching. Zero offset preserves the physical centre across yaw.
    span = 2 * depth_radius / .92
    return front, back, span, mask


def bake(asset, shape, source):
    albedo_path = BASE / "assets" / asset
    albedo = Image.open(albedo_path).convert("RGBA")
    alpha = np.asarray(albedo)[:, :, 3]
    depth = np.full(alpha.shape, 128, dtype=np.uint8)
    normals = np.full((*alpha.shape, 3), (128, 128, 255), dtype=np.uint8)
    output = copy.deepcopy(source)
    # Mirrored views use the same rectangle and maps. The renderer mirrors X and
    # the normal X component once; never bake a second reflected copy.
    fields = {}
    for frame in output["frames"]:
        x, y, w, h = frame["rect"]
        key = tuple(frame["rect"])
        if key not in fields:
            front, back, span, mask = proxy_field(frame, alpha[y:y+h, x:x+w], shape, asset)
            encoded = np.rint(np.clip(.5 + front / span, 0, 1) * 255).astype(np.uint8)
            # Keep a small padding of the extrapolated proxy around covered pixels
            # for bilinear filtering; unused atlas pixels retain neutral defaults.
            padded = distance_transform_edt(~(alpha[y:y+h, x:x+w] > 0)) <= 3
            target = depth[y:y+h, x:x+w]
            target[padded] = encoded[padded]
            # Derive from the same calibrated continuous field before depth
            # quantisation, avoiding contour rings from differentiating 8-bit Z.
            # +Y is up, +Z toward viewer. Five effective bits/channel keep normal
            # angular error small while fitting the existing pack transfer budget.
            field = gaussian_filter(front, sigma=max(1, min(w, h) / 150))
            dy, dx = np.gradient(field)
            vector = np.stack((-dx * h, dy * h, np.ones_like(dx)), axis=-1)
            vector /= np.linalg.norm(vector, axis=-1, keepdims=True)
            # Symmetric signed bins preserve an exactly neutral X/Y (128).
            rgb = np.rint(128 + np.rint(vector * 15) * (127 / 15)).astype(np.uint8)
            normals[y:y+h, x:x+w][padded] = rgb[padded]
            fields[key] = (front, back, span)
        front, back, span = fields[key]
        frame["depth_scale"] = round(span, 6)
        frame["depth_offset"] = 0
        # Explicitly registered hidden mounts, independent of layer ordering hints.
        hidden = set()
        if asset == "combat/courier-hull.png":
            if frame["azimuth"] == 0:
                hidden.add("turret_top")
            elif frame["azimuth"] in (135, 225):
                hidden.add("turret_bottom")
        elif asset == "combat/pirate-hull.png" and frame["azimuth"] == 180:
            hidden.update(("turret_forward", "turret_aft"))
        for name, marker in frame.get("attachments", {}).items():
            mx, my = min(w-1, max(0, int(marker["x"]))), min(h-1, max(0, int(marker["y"])))
            marker["depth_offset"] = round(float(back[my, mx] - front[my, mx] - .02)
                                           if name in hidden else .015, 6)
    stem = albedo_path.stem.removesuffix("-albedo").removesuffix("-atlas")
    depth_path = albedo_path.with_name(stem + "-depth.png")
    normal_path = albedo_path.with_name(stem + "-normal.png")
    Image.fromarray(depth).save(depth_path, optimize=True)
    Image.fromarray(normals).save(normal_path, optimize=True)
    output["depth"] = PREFIX + depth_path.relative_to(BASE).as_posix()
    output["normal"] = PREFIX + normal_path.relative_to(BASE).as_posix()
    return output


def sidecar(material, asset, scale):
    # Adjacent sidecars are editable/importable copies of the same calibration.
    path = BASE / "assets" / asset
    stem = path.stem.removesuffix("-albedo").removesuffix("-atlas")
    source_path = path.with_name(stem + ".sprite.json")
    data = json.loads(source_path.read_text(encoding="utf-8")) if source_path.exists() else {}
    # Older missile.sprite.json was a canonical material. Do not retain a second,
    # stale top-level frame list beside the source-sidecar representation.
    for key in ("albedo", "normal", "depth", "frames"):
        data.pop(key, None)
    data.update(schema_version=1, coordinate_system="frame_pixels_top_left_y_down",
                id=data.get("id", stem), image_size=list(Image.open(path).size),
                files={"sprite": path.name, "normal": stem + "-normal.png", "depth": stem + "-depth.png"},
                billboard_mode="eight" if len(material["frames"]) == 8 else "camera", render_scale=scale,
                depth_encoding={"channel": "r", "color_space": "linear", "white": "near",
                                "quality": "yawed_geometric_proxy"},
                normal_encoding={"space": "view", "x": "right", "y": "up", "z": "toward_viewer",
                                 "quality": "derived_from_calibrated_depth"})
    for key in ("emissive", "radar_icon"):
        if key in material:
            data[key] = material[key]
    if asset.startswith("cruiser/"):
        data["files"]["radar_icon"] = "../radar/ship.png"
    data["sprite_sheet"] = {"kind": "directions", "frames": [dict(
        name=f["name"], rect=f["rect"], pivot=f["pivot"], camera_azimuth_degrees=f["azimuth"],
        mirror_x=f.get("mirror_x", False), depth_scale=f["depth_scale"], depth_offset=f["depth_offset"],
        **({"forward": f["forward"]} if "forward" in f else {}),
        markers={k: [m["x"], m["y"], m["layer"]] for k, m in f.get("attachments", {}).items()},
        attachment_depth_offsets={k: m["depth_offset"] for k, m in f.get("attachments", {}).items()})
        for f in material["frames"]]}
    write_json(source_path, data)
    write_json(path.with_name(stem + ".material.json"), material)


def main():
    definitions = BASE / "definitions.json"
    original_text = definitions.read_text(encoding="utf-8")
    data = json.loads(original_text)
    for asset, shape in ASSETS.items():
        reference = PREFIX + "assets/" + asset
        entities = [e for e in data["entities"] if (e.get("render") or {}).get("sprite", {}).get("albedo") == reference]
        if not entities:
            raise ValueError("Missing source material: " + reference)
        material = bake(asset, shape, entities[0]["render"]["sprite"])
        sidecar(material, asset, entities[0]["render"]["scale"])
        for entity in entities:
            # Preserve entity-specific attachment sets, radar metadata and pivots.
            original = entity["render"]["sprite"]
            original.update(normal=material["normal"], depth=material["depth"])
            for old, calibrated in zip(original["frames"], material["frames"], strict=True):
                if old["rect"] != calibrated["rect"]:
                    raise ValueError("Shared artwork has incompatible registration")
                old.update(depth_scale=calibrated["depth_scale"], depth_offset=calibrated["depth_offset"])
                for name, marker in old.get("attachments", {}).items():
                    marker["depth_offset"] = calibrated.get("attachments", {}).get(name, {}).get("depth_offset", .015)
        print(f"Prepared {asset}: {len(entities)} direct materials, {len(material['frames'])} views")
    if definitions.read_text(encoding="utf-8") != original_text:
        raise RuntimeError("Definitions changed during baking; rerun against the latest source")
    write_json(definitions, data)


if __name__ == "__main__":
    main()
