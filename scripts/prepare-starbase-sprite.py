"""Extract the approved station without changing any source RGB samples.

Requires Pillow, numpy and scipy. Only alpha changes: no resampling, recolouring,
inpainting or generated imagery. Publish the same bytes to the production pack.
"""

import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/art/phoenix-starbase-side-v1.png"
RAW = ROOT / "raw/starbase"
PACK = ROOT / "content/base/assets/rescue"


def save_json(path, value):
    path.write_text(json.dumps(value, indent="\t") + "\n", encoding="utf-8")


def relief(alpha):
    """View-space geometric proxy in source-pixel units, never luminance.

    Ring lies in the horizontal world plane; visible rim is the near half of
    a torus. The spire sits at the hub, behind that rim. Fittings are separate
    rounded volumes. This models the drawing, not a recovered exact 3D mesh.
    """
    height, width = alpha.shape
    yy, xx = np.mgrid[:height, :width].astype(float)
    solid = alpha > 0
    # The silhouette only determines local rounded thickness of the upright.
    thickness = np.minimum(ndimage.distance_transform_edt(solid), 45)
    z = np.sqrt(np.maximum(0, 45**2 - (45 - thickness)**2))
    # Projecting control platforms have different mounting depths, independent
    # of their red/yellow paint and baked shadows.
    for x0, y0, x1, y1, base in [(889, 151, 1070, 225, 85),
                                (804, 269, 1035, 340, 65),
                                (827, 400, 1100, 484, 105),
                                (536, 516, 732, 584, 80)]:
        region = (xx >= x0) & (xx <= x1) & (yy >= y0) & (yy <= y1)
        z[region] += base
    # Near-side toroidal rim. Cross-section and camera-facing curvature vary
    # with position, so ivory paint and black ink have the same surface depth.
    ring = (yy >= 647) & (yy <= 819)
    section = np.sqrt(np.maximum(0, 86**2 - (yy - 733)**2))
    ring_z = np.sqrt(np.maximum(0, (662 + section)**2 - (xx - 768)**2))
    z[ring] = ring_z[ring]
    # Fuel spheres on the visible rim and the hub's short lower service keel.
    for cx, cy, rx, ry in [(148, 797, 40, 47), (438, 804, 50, 52),
                            (1102, 804, 46, 52), (1326, 800, 41, 48),
                            (671, 865, 40, 43)]:
        u, v = (xx - cx) / rx, (yy - cy) / ry
        region = (u*u + v*v) <= 1
        base = np.sqrt(max(0, 662**2 - (cx - 768)**2)) if cy < 850 else 25
        surface = base + rx * np.sqrt(np.maximum(0, 1 - u*u - v*v))
        z[region] = surface[region]
    # Extend the nearest valid surface through transparent pixels before
    # smoothing/gradient calculation, avoiding a false cliff to background.
    nearest = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
    extended = z[tuple(nearest)]
    field = ndimage.gaussian_filter(extended, sigma=1.1)
    dz_dy, dz_dx = np.gradient(field)
    normals = np.stack([-dz_dx, dz_dy, np.ones_like(field)], axis=-1)
    normals /= np.linalg.norm(normals, axis=-1, keepdims=True)
    normal = np.rint((normals * .5 + .5) * 255).clip(0, 255).astype(np.uint8)
    span = 1600.0
    depth = np.rint((.5 + field / span) * 255).clip(0, 255).astype(np.uint8)
    depth[~solid] = 128
    normal[~solid] = [128, 128, 255]
    return depth, normal, span / height


def main():
    RAW.mkdir(parents=True, exist_ok=True)
    source = np.array(Image.open(SOURCE).convert("RGBA"))
    rgb = source[:, :, :3]
    r, g, b = (rgb[:, :, c].astype(np.int16) for c in range(3))
    # Flood only pale paper connected to the exterior. Ivory tanks and panels
    # enclosed by ink contours remain opaque, unlike a global colour key.
    paper_colour = (r >= 220) & (g >= 210) & (b >= 180) & (r >= g) & (g >= b) & (r - g <= 25) & (g - b <= 45)
    seeds = np.zeros(paper_colour.shape, dtype=bool)
    seeds[0, :] = paper_colour[0, :]
    seeds[-1, :] = paper_colour[-1, :]
    seeds[:, 0] = paper_colour[:, 0]
    seeds[:, -1] = paper_colour[:, -1]
    # Visually identified open negative space enclosed by crane/brace outlines.
    # Coordinates are source pixels; do not remove enclosed ivory hull panels.
    for x, y in [(974, 371), (311, 629), (499, 620), (1031, 624)]:
        assert paper_colour[y, x], (x, y)
        seeds[y, x] = True
    paper = ndimage.binary_propagation(seeds, mask=paper_colour)
    result = source.copy()
    result[paper, 3] = 0
    # Keep original RGB even beneath alpha=0. Do not premultiply or decontaminate.
    assert np.array_equal(result[:, :, :3], rgb)
    height, width = result.shape[:2]
    depth, normal, depth_scale = relief(result[:, :, 3])
    pivot = [width / 2, 744]
    frame = dict(name="starbase", rect=[0, 0, width, height], pivot=pivot,
                 azimuth=0, depth_scale=depth_scale, depth_offset=0, attachments={})
    material = dict(albedo="phoenix-base:assets/rescue/starbase.png", frames=[frame],
                    normal="phoenix-base:assets/rescue/starbase-normal.png",
                    depth="phoenix-base:assets/rescue/starbase-depth.png")
    side_frame = dict(name="starbase", rect=frame["rect"], pivot=pivot,
                      camera_azimuth_degrees=0, mirror_x=False, depth_scale=depth_scale,
                      depth_offset=0, markers={}, attachment_depth_offsets={})
    # render.scale is frame height, so this retains the former 400 m width.
    scale = 400 * height / width
    sidecar = dict(schema_version=1, coordinate_system="frame_pixels_top_left_y_down",
                   id="starbase", image_size=[width, height],
                   files=dict(sprite="starbase.png", normal="starbase-normal.png", depth="starbase-depth.png"),
                   billboard_mode="camera", render_scale=scale,
                   depth_encoding=dict(channel="r", color_space="linear", white="near",
                                       quality="side_elevation_geometric_proxy"),
                   normal_encoding=dict(space="view", x="right", y="up", z="toward_viewer",
                                        quality="derived_from_geometric_depth"),
                   sprite_sheet=dict(kind="directions", frames=[side_frame]))
    Image.fromarray(result).save(RAW / "starbase.png")
    saved = np.array(Image.open(RAW / "starbase.png"))
    assert np.array_equal(saved, result)
    (PACK / "starbase.png").write_bytes((RAW / "starbase.png").read_bytes())
    for name, array in [("starbase-depth.png", depth), ("starbase-normal.png", normal)]:
        Image.fromarray(array).save(RAW / name)
        (PACK / name).write_bytes((RAW / name).read_bytes())
    save_json(RAW / "starbase.sprite.json", sidecar)
    save_json(PACK / "starbase.sprite.json", sidecar)
    save_json(PACK / "starbase.material.json", material)

    # Change only this entity's render block; preserve unrelated in-flight edits.
    definitions = ROOT / "content/base/definitions.json"
    text = definitions.read_text(encoding="utf-8")
    albedo = text.index('"albedo": "phoenix-base:assets/rescue/starbase.png"')
    start = text.rfind('\t\t\t"render": {', 0, albedo) + len('\t\t\t"render": ')
    assert start > 0
    render, length = json.JSONDecoder().raw_decode(text[start:])
    assert render["sprite"]["albedo"] == material["albedo"]
    render.update(scale=scale, sprite=material)
    encoded = json.dumps(render, indent="\t").replace("\n", "\n\t\t\t")
    definitions.write_text(text[:start] + encoded + text[start + length:], encoding="utf-8")

    ys, xs = np.nonzero(result[:, :, 3])
    receipt = dict(source=str(SOURCE.relative_to(ROOT)),
                   source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                   output_sha256=hashlib.sha256((RAW / "starbase.png").read_bytes()).hexdigest(),
                   image_size=[width, height], rgb_changed_pixels=0,
                   alpha_cleared_pixels=int(paper.sum()), resampled=False,
                   foreground_bounds=[int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1],
                   method="pale-paper flood from borders and four identified open brace/crane regions; binary alpha only",
                   material="geometric torus, central upright, projecting platforms and rim fuel spheres; no luminance input",
                   depth_scale=depth_scale,
                   depth_samples={"near_rim": int(depth[733, 768]), "upper_spire": int(depth[200, 780])})
    save_json(RAW / "extraction-receipt.json", receipt)
    # Dark composite is a review artifact, never the published sprite.
    preview = Image.new("RGBA", (width, height), "#282b39")
    preview.alpha_composite(Image.fromarray(result))
    preview.convert("RGB").save(RAW / "extraction-preview.png")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
