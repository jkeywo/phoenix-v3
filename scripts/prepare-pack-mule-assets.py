"""Extract Pack Mule without resampling/recolouring; bake registered camera Z.

Explicit art preparation only (Pillow, NumPy, SciPy). No image generation.
RGB is copied exactly, even under cleared alpha. Depth comes from registered
box planes, axial cylinders and component positions, never colour/luminance.
"""

import hashlib
import json
import math
import shutil
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "raw/pack-mule"
PACK = ROOT / "content/base/assets/pack-mule"
PREFIX = "phoenix-base:assets/pack-mule/"
SPAN = 4.0
SCALE = 15

# Coordinates in the original, immutable source illustrations. Pivots are on
# the longitudinal support rail. Rectangle heights normalise the same visible
# vertical ship extent without changing a single source texel.
VIEWS = [
    dict(name="bow", source="front-v1", angle=0, cutoff=1255,
         pivot=[572, 700], engine=[572, 560, 320, 230],
         cargo=[[(270, 725), (875, 725), (875, 1190), (270, 1190)]],
         cabin=[(335, 320), (800, 320), (815, 640), (335, 640)],
         nozzle=[572, 570], seeds=[[572, 250], [572, 210]]),
    dict(name="bow_port", source="front-45-v1", angle=45, cutoff=None,
         pivot=[900, 475], engine=[1615, 465, 145, 155],
         cargo=[[(390, 575), (670, 525), (670, 805), (390, 850)],
                [(690, 535), (960, 505), (960, 765), (690, 800)],
                [(990, 510), (1230, 475), (1230, 730), (990, 770)],
                [(1270, 475), (1505, 440), (1505, 685), (1270, 730)]],
         cabin=[(20, 405), (340, 385), (365, 630), (20, 650)],
         nozzle=[1720, 440], seeds=[]),
    dict(name="port", source="side-v1", angle=90, cutoff=770,
         pivot=[887, 420], engine=[1550, 465, 205, 130],
         cargo=[[(270, 450), (570, 450), (570, 720), (270, 720)],
                [(590, 450), (850, 450), (850, 720), (590, 720)],
                [(880, 450), (1145, 450), (1145, 720), (880, 720)],
                [(1170, 450), (1435, 450), (1435, 720), (1170, 720)]],
         cabin=[(15, 315), (325, 315), (325, 510), (15, 510)],
         nozzle=[1748, 455], seeds=[[800, 245], [650, 300], [1100, 295], [420, 310], [1180, 290]]),
    dict(name="stern_port", source="rear-45-v1", angle=135, cutoff=None,
         pivot=[855, 440], engine=[1545, 590, 215, 225],
         cargo=[[(175, 405), (390, 430), (390, 650), (175, 615)],
                [(415, 450), (655, 475), (655, 710), (415, 680)],
                [(690, 490), (930, 535), (930, 775), (690, 735)],
                [(965, 535), (1240, 575), (1240, 820), (965, 780)]],
         cabin=[(45, 265), (215, 265), (215, 445), (45, 445)],
         nozzle=[1655, 610], seeds=[]),
    dict(name="stern", source="rear-v1", angle=180, cutoff=1255,
         pivot=[572, 740], engine=[572, 550, 240, 240],
         cargo=[[(285, 770), (870, 770), (870, 1190), (285, 1190)]],
         cabin=None, nozzle=[572, 570], seeds=[]),
]


def write_json(path, value):
    path.write_text(json.dumps(value, indent="\t") + "\n", encoding="utf-8")


def poly_mask(shape, points):
    image = Image.new("1", (shape[1], shape[0]))
    ImageDraw.Draw(image).polygon([tuple(p) for p in points], fill=1)
    return np.asarray(image, dtype=bool)


def extract(source, view):
    rgba = np.array(source.convert("RGBA"))
    if rgba[:, :, 3].min() == 255:
        r, g, b = [rgba[:, :, k].astype(np.int16) for k in range(3)]
        pale = (r >= 205) & (g >= 185) & (b >= 145) & (r >= g - 6) & (g >= b - 6) & (r-b <= 100)
        seeds = np.zeros(pale.shape, bool)
        seeds[0] = pale[0]
        seeds[-1] = pale[-1]
        seeds[:, 0] = pale[:, 0]
        seeds[:, -1] = pale[:, -1]
        for x, y in view["seeds"]:
            if pale[y, x]:
                seeds[y, x] = True
        if view["name"] == "port":
            # Registered open gantry interior, including enclosed cross-brace
            # holes. No cream cargo panels lie within this region.
            seeds[225:395, 440:1320] |= pale[225:395, 440:1320]
        paper = ndimage.binary_propagation(seeds, mask=pale)
        alpha = ~paper
        if view["cutoff"]:
            alpha[view["cutoff"]:] = False
        labels, _ = ndimage.label(alpha)
        sizes = np.bincount(labels.ravel())
        good = sizes >= 12
        good[0] = False
        rgba[~good[labels], 3] = 0
    assert np.array_equal(rgba[:, :, :3], np.array(source.convert("RGB")))
    return rgba


def geometric_depth(alpha, view):
    """Registered 2.5D visible surfaces, in units of original visible height.

    F is bow-positive, P port-positive; camera Z=F*cos(a)+P*sin(a).
    This visible-surface proxy cannot recover hidden structure from concept art.
    """
    h, w = alpha.shape
    yy, xx = np.mgrid[:h, :w].astype(float)
    solid = alpha > 0
    ysolid, _ = np.nonzero(solid)
    height = float(ysolid.max() - ysolid.min() + 1)
    angle = math.radians(view["angle"])
    cosine, sine = math.cos(angle), math.sin(angle)
    # Background fittings lie on the narrow centreline support, with a very
    # small rounded cross-section. Longitudinal Z reverses between bow/stern.
    slope = -cosine / max(abs(sine), .45) if abs(sine) > .1 else 0
    origin = view["pivot"][0]
    z = slope * (xx-origin) / height
    thickness = np.minimum(ndimage.distance_transform_edt(solid), 10)
    z += np.sqrt(np.maximum(0, 10**2 - (10-thickness)**2)) / height
    if abs(sine) < .1:
        # Exposed upper cap is the near end of the longitudinal gantry.
        z[:] = 1.1
        z[(yy > 180) & (yy < 315)] = .8
    # Separate cargo boxes. Each side is a physical plane; paint and black
    # scratches receive exactly the same Z. Chamfered boundaries are recessed.
    for i, points in enumerate(view["cargo"]):
        mask = poly_mask(alpha.shape, points)
        cx = sum(p[0] for p in points) / len(points)
        forward = (.78 - i*.52) if len(view["cargo"]) > 1 else (1.1 if cosine > 0 else -1.1)
        centre_z = forward*cosine + .31*abs(sine)
        plane = centre_z + slope*(xx-cx)/height
        edge = ndimage.distance_transform_edt(mask)
        plane -= np.clip(10-edge, 0, 10) / height
        z[mask] = plane[mask]
    # Forward cabin projects ahead of the cargo file.
    if view["cabin"]:
        mask = poly_mask(alpha.shape, view["cabin"])
        cx = sum(p[0] for p in view["cabin"]) / len(view["cabin"])
        cabin = 1.25*cosine + .22*abs(sine) + slope*(xx-cx)/height
        z[mask] = cabin[mask]
    # Cylindrical engine barrel has an axial position at the stern. Its
    # nozzle is a recessed bowl, so the bright orange core is FARTHER than lip.
    cx, cy, rx, ry = view["engine"]
    u, v = (xx-cx)/rx, (yy-cy)/ry
    engine = (u*u+v*v <= 1) if abs(sine) < .1 else ((abs(u) <= 1) & (abs(v) <= 1))
    if view["angle"] == 0:
        # Engine is BEHIND the front cabin, not a dark disk painted across it.
        # Only its exposed shoulders can contribute a visible depth sample.
        engine &= ~poly_mask(alpha.shape, view["cabin"])
        engine &= ~poly_mask(alpha.shape, view["cargo"][0])
    cylinder = -1.22*cosine + .32*abs(sine)*np.sqrt(np.maximum(0, 1-v*v)) + slope*(xx-cx)/height
    if cosine < -.1:
        nx, ny = view["nozzle"]
        radius2 = ((xx-nx)/(rx*max(abs(cosine), .25)))**2 + ((yy-ny)/ry)**2
        bowl = -.22*np.exp(-radius2/.28)
        cylinder += bowl
    z[engine] = cylinder[engine]
    # Continue each visible field into neighbouring transparent texels before
    # differentiation; transparent windows are NOT deep black holes.
    nearest = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    field = ndimage.gaussian_filter(z[tuple(nearest)], 1.2)
    if np.max(np.abs(field[solid])) >= SPAN*.49:
        raise RuntimeError("Depth span clips registered geometry")
    dy, dx = np.gradient(field)
    normal = np.stack([-dx*height, dy*height, np.ones_like(field)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    normal_rgb = np.rint((normal*.5+.5)*255).clip(0, 255).astype(np.uint8)
    depth = np.rint((.5+field/SPAN)*255).clip(0, 255).astype(np.uint8)
    depth[~solid] = 128
    normal_rgb[~solid] = [128, 128, 255]
    return depth, normal_rgb, field, height


def main():
    RAW.mkdir(parents=True, exist_ok=True)
    PACK.mkdir(parents=True, exist_ok=True)
    prepared, receipts = [], []
    for view in VIEWS:
        source_path = ROOT / ("docs/art/phoenix-pack-mule-" + view["source"] + ".png")
        source = Image.open(source_path)
        rgba = extract(source, view)
        alpha = rgba[:, :, 3]
        ys, xs = np.nonzero(alpha)
        box = [int(xs.min()), int(ys.min()), int(xs.max())+1, int(ys.max())+1]
        # Retain full individual-source coordinates for edits and pixel audits.
        stem = "pack-mule-" + view["name"]
        Image.fromarray(rgba).save(RAW / (stem + ".png"), optimize=True)
        depth, normals, field, visible_h = geometric_depth(alpha, view)
        Image.fromarray(depth).save(RAW / (stem + "-depth.png"), optimize=True)
        Image.fromarray(normals).save(RAW / (stem + "-normal.png"), optimize=True)
        margin = max(8, round(visible_h*.025))
        x0, y0, x1, y1 = box
        crop = (x0, y0, x1, y1)
        frame_w, frame_h = x1-x0+2*margin, y1-y0+2*margin
        pivot = [view["pivot"][0]-x0+margin, view["pivot"][1]-y0+margin]
        # Runtime Z is measured in render.scale (rectangle height) units.
        depth_scale = SPAN * visible_h/frame_h
        engine = [view["nozzle"][0]-x0+margin, view["nozzle"][1]-y0+margin, 1 if view["angle"] >= 90 else -1]
        nx, ny = view["nozzle"]
        sample = (float(depth[ny, nx])/255-.5)*depth_scale
        target_z = -1.35*math.cos(math.radians(view["angle"])) * visible_h/frame_h
        frame = dict(name=view["name"], rect=[0, 0, frame_w, frame_h], pivot=pivot,
                     camera_azimuth_degrees=view["angle"], mirror_x=False,
                     depth_scale=depth_scale, depth_offset=0,
                     markers={"engine_stern": engine},
                     attachment_depth_offsets={"engine_stern": round(target_z-sample, 7)})
        prepared.append(dict(frame=frame, crop=crop, margin=margin, rgba=rgba, depth=depth, normal=normals))
        receipt = dict(source=str(source_path.relative_to(ROOT)), sha256=hashlib.sha256(source_path.read_bytes()).hexdigest(),
                       source_size=list(source.size), crop=box, frame_size=[frame_w, frame_h], retained_pixels=int((alpha>0).sum()),
                       rgb_identity=True, resampled=False, depth_min=float(field[alpha>0].min()), depth_max=float(field[alpha>0].max()))
        receipts.append(receipt)
    atlas_w = sum(p["frame"]["rect"][2]+8 for p in prepared)
    atlas_h = max(p["frame"]["rect"][3] for p in prepared)
    assert atlas_w <= 8192 and atlas_h <= 8192 and atlas_w*atlas_h <= 16777216
    albedo = Image.new("RGBA", (atlas_w, atlas_h))
    depth_atlas = Image.new("L", albedo.size, 128)
    normal_atlas = Image.new("RGB", albedo.size, (128, 128, 255))
    x = 0
    for p, receipt in zip(prepared, receipts):
        f, m = p["frame"], p["margin"]
        f["rect"][0] = x
        for dest, key in ((albedo, "rgba"), (depth_atlas, "depth"), (normal_atlas, "normal")):
            dest.paste(Image.fromarray(p[key]).crop(p["crop"]), (x+m, m))
        receipt["atlas_integer_translation"] = [x+m-p["crop"][0], m-p["crop"][1]]
        x += f["rect"][2]+8
    stem = "pack-mule"
    albedo.save(RAW / (stem+".png"), optimize=True)
    depth_atlas.save(RAW / (stem+"-depth.png"), optimize=True)
    normal_atlas.save(RAW / (stem+"-normal.png"), optimize=True)
    frames = [p["frame"] for p in prepared]
    for index, name, angle in ((3, "stern_starboard", 225), (2, "starboard", 270), (1, "bow_starboard", 315)):
        frame = json.loads(json.dumps(frames[index]))
        frame.update(name=name, camera_azimuth_degrees=angle, mirror_x=True)
        frames.append(frame)
    sidecar = dict(schema_version=1, id="pack-mule", description="Pixel-preserved Pack Mule, registered geometric camera-depth proxy. Five source views, eight bearings with mirrored starboard views.",
                   files=dict(sprite=stem+".png", normal=stem+"-normal.png", depth=stem+"-depth.png"),
                   image_size=list(albedo.size), coordinate_system="frame_pixels_top_left_y_down", sprite_color_space="srgb",
                   billboard_mode="eight", render_scale=SCALE, emissive=False, pivot=None, markers={},
                   normal_encoding=dict(space="view", axes=dict(x="right", y="up", z="toward_viewer"), color_space="linear", decode="normalize(rgb * 2 - 1)", quality="gradient_of_registered_geometric_Z"),
                   depth_encoding=dict(channel="r", color_space="linear", white="near", quality="registered_component_geometry_not_shading"),
                   sprite_sheet=dict(kind="directions", frames=frames))
    write_json(RAW / (stem+".sprite.json"), sidecar)
    canonical = dict(albedo=PREFIX+stem+".png", normal=PREFIX+stem+"-normal.png", depth=PREFIX+stem+"-depth.png",
                     radar_sprite="phoenix-base:assets/radar/ship.png", frames=[])
    for f in frames:
        canonical["frames"].append(dict(name=f["name"], rect=f["rect"], pivot=f["pivot"], azimuth=f["camera_azimuth_degrees"],
            mirror_x=f["mirror_x"], depth_scale=f["depth_scale"], depth_offset=f["depth_offset"],
            attachments={k:dict(x=p[0], y=p[1], layer=p[2], depth_offset=f["attachment_depth_offsets"][k]) for k,p in f["markers"].items()}))
    write_json(RAW / (stem+".material.json"), canonical)
    write_json(RAW / "geometry-registration.json", dict(depth_basis="Z=F*cos(azimuth)+P*sin(azimuth); registered boxes, cabin, gantry and recessed axial cylinder", views=VIEWS, span=SPAN))
    write_json(RAW / "extraction-receipt.json", dict(imagegen_used=False, rgb_modified=False, resampling=False, frames=receipts))
    # Read back saved atlas and compare retained colour bytes to each source.
    saved = np.array(Image.open(RAW / (stem+".png")))
    for p, receipt in zip(prepared, receipts):
        x0, y0, x1, y1 = p["crop"]
        dx, dy = receipt["atlas_integer_translation"]
        read = saved[y0+dy:y1+dy, x0+dx:x1+dx]
        source = p["rgba"][y0:y1, x0:x1]
        assert np.array_equal(read, source), "Saved atlas changed source RGBA"
    for suffix in (".png", "-depth.png", "-normal.png", ".sprite.json", ".material.json"):
        shutil.copyfile(RAW / (stem+suffix), PACK / (stem+suffix))
    write_json(PACK / "verification.json", dict(imagegen_used=False, retained_rgb_identical=True, resampling=False,
               depth_source="registered camera-space component geometry; no RGB or luminance input", views=receipts))
    # Review thumbnails never replace or resample production maps.
    preview = Image.new("RGB", (1500, 900), "#393649")
    for i, p in enumerate(prepared):
        for row, key in enumerate(("rgba", "depth", "normal")):
            im = Image.fromarray(p[key]).convert("RGBA")
            im.putalpha(Image.fromarray(p["rgba"][:, :, 3]))
            im = im.crop(p["crop"])
            im.thumbnail((290, 285))
            bg = Image.new("RGBA", im.size, "#393649")
            bg.alpha_composite(im)
            preview.paste(bg.convert("RGB"), (i*300+(300-im.width)//2, row*300+(300-im.height)//2))
    preview.save(RAW / "material-preview.png")
    print(f"Pack Mule: {len(frames)} bearings, {albedo.size}, preserved RGB; geometric depth and normals")


if __name__ == "__main__":
    main()
