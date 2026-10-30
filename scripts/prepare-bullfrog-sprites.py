"""Losslessly separate five approved Bullfrog drawings into three sprite atlases.

Only coverage, cropping, and integer placement change. Retained RGB pixels are
copied exactly; hidden hull/turret surfaces are never reconstructed.
"""

import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/art/bullfrog-sprites-v1"
CELL = (1536, 1280)
TURRET_CELL = (512, 256)
TOP_PIVOT = (320, 200)
BOTTOM_PIVOT = (256, 64)

# Polygons follow the visible turret outlines, in original image coordinates.
# A mount point belongs to both parts' coordinate systems, not both RGB masks.
VIEWS = [
    dict(name="bow", source="front-v2", azimuth=0, centre=[768, 555], cutoff=938,
         top_pivot=[768, 180], bottom_pivot=[768, 843],
         top=[(658,147),(684,144),(689,127),(705,116),(731,111),(733,78),(749,57),(774,53),(794,69),(807,94),(807,113),(835,117),(850,137),(865,143),(890,145),(893,167),(869,172),(858,180),(689,180),(678,171),(657,170)],
         bottom=[(735,840),(801,840),(803,851),(820,862),(824,891),(813,908),(792,918),(788,929),(750,929),(744,917),(729,912),(718,895),(720,870),(733,856)]),
    dict(name="bow_port", source="mission-front45-v2", azimuth=45, centre=[640, 690], cutoff=None,
         top_pivot=[698, 308], bottom_pivot=[207, 868],
         top=[(418,239),(427,230),(450,230),(462,237),(544,242),(553,232),(566,232),(577,242),(609,244),(613,235),(642,235),(654,242),(671,234),(684,228),(699,228),(704,236),(724,235),(734,242),(742,249),(751,251),(762,268),(779,268),(786,280),(785,289),(764,294),(751,294),(750,306),(666,308),(660,297),(637,292),(624,276),(608,273),(601,265),(452,254),(435,255),(420,249)],
         bottom=[(175,869),(226,869),(234,876),(247,879),(253,897),(244,915),(235,923),(225,930),(222,941),(201,946),(182,938),(178,925),(163,916),(157,899),(159,882)]),
    dict(name="port", source="side-v5", azimuth=90, centre=[760, 590], cutoff=944,
         top_pivot=[797, 228], bottom_pivot=[546, 846],
         top=[(508,177),(518,169),(540,169),(552,174),(630,174),(635,168),(649,168),(653,172),(686,173),(691,168),(739,167),(748,158),(781,155),(797,152),(807,149),(821,152),(833,159),(843,163),(852,173),(872,173),(886,183),(888,200),(875,209),(861,209),(857,224),(835,230),(732,230),(715,224),(714,213),(695,210),(692,202),(552,201),(545,205),(517,204),(508,200)],
         bottom=[(468,844),(594,844),(599,850),(620,848),(630,858),(631,878),(616,891),(599,896),(587,916),(566,935),(514,936),(496,924),(494,912),(481,911),(475,900),(446,897),(443,890),(354,890),(349,884),(322,884),(319,879),(319,870),(328,865),(354,867),(359,872),(443,872),(446,864),(467,862)]),
    dict(name="stern_port", source="mission-rear45-v2", azimuth=135, centre=[700, 670], cutoff=None,
         top_pivot=[622, 341], bottom_pivot=[836, 930],
         top=[(354,295),(364,287),(384,287),(389,294),(463,291),(467,284),(480,283),(484,287),(516,284),(522,276),(548,275),(553,265),(587,262),(599,257),(625,256),(632,247),(649,246),(658,258),(682,262),(695,276),(695,284),(716,281),(730,288),(736,303),(728,318),(698,323),(687,334),(674,342),(575,346),(558,338),(553,327),(527,326),(520,319),(390,315),(385,320),(365,320),(354,311)],
         bottom=[(775,929),(860,928),(868,935),(901,938),(911,941),(920,945),(919,971),(905,978),(871,978),(866,997),(849,1008),(807,1015),(783,1006),(778,996),(759,994),(747,985),(744,981),(720,981),(717,971),(721,958),(744,958),(748,953),(775,949)]),
    dict(name="stern", source="rear-v1", azimuth=180, centre=[768, 565], cutoff=942,
         top_pivot=[768, 173], bottom_pivot=[768, 839],
         top=[(688,122),(706,119),(712,100),(740,93),(748,90),(751,73),(758,67),(760,32),(765,26),(776,26),(782,33),(782,69),(787,75),(787,88),(800,93),(830,98),(843,112),(846,120),(866,124),(868,145),(849,150),(844,164),(828,172),(709,174),(701,161),(700,152),(687,148)],
         bottom=[(707,839),(822,839),(827,848),(851,849),(856,852),(892,851),(897,847),(918,848),(923,853),(964,853),(973,858),(974,876),(965,883),(926,883),(919,888),(897,887),(890,882),(853,883),(842,894),(828,899),(824,913),(807,923),(730,924),(709,913),(706,900),(691,894),(685,885),(650,883),(645,889),(624,887),(621,883),(582,883),(577,887),(550,887),(541,880),(541,860),(548,852),(577,852),(585,857),(621,857),(626,850),(647,850),(651,855),(686,855),(695,849)]),
]


def polygon_mask(size, points):
    mask = Image.new("1", size)
    ImageDraw.Draw(mask).polygon(points, fill=1)
    return np.asarray(mask, dtype=bool)


def coverage(rgb, cutoff):
    # Flood pale paper from all exterior borders. Opaque ivory armour enclosed
    # by the source's black outline is retained, rather than colour-keyed away.
    r, g, b = (rgb[:, :, c].astype(np.int16) for c in range(3))
    pale = (r >= 205) & (g >= 185) & (b >= 135) & (r >= g - 6) & (g >= b - 6) & (r - b <= 110)
    seeds = np.zeros(pale.shape, dtype=bool)
    seeds[0, :] = pale[0, :]
    seeds[-1, :] = pale[-1, :]
    seeds[:, 0] = pale[:, 0]
    seeds[:, -1] = pale[:, -1]
    paper = ndimage.binary_propagation(seeds, mask=pale)
    result = ~paper
    if cutoff is not None:
        result[cutoff:, :] = False  # Remove the original drawing's caption.
    # Remove detached paper-grain specks, retaining genuine thin ship fittings.
    labels, count = ndimage.label(result)
    areas = np.bincount(labels.ravel())
    good = areas >= 12
    good[0] = False
    return good[labels]


def bounds(mask):
    y, x = np.nonzero(mask)
    return [int(x.min()), int(y.min()), int(x.max()) + 1, int(y.max()) + 1]


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def verify_saved(receipts):
    sheets = {}
    results = []
    for part in ("hull", "top-turret", "bottom-turret"):
        data = json.loads((OUT / f"bullfrog-{part}.sprite.json").read_text())
        image = Image.open(OUT / data["files"]["sprite"]).convert("RGBA")
        assert list(image.size) == data["image_size"]
        entries = data["sprite_sheet"]["frames"]
        assert len(entries) == 8 and len({tuple(f["rect"]) for f in entries}) == 5
        assert sorted(f["camera_azimuth_degrees"] for f in entries) == list(range(0, 360, 45))
        assert image.getpixel((0, 0))[3] == 0
        for frame in entries:
            x, y, w, h = frame["rect"]
            assert x >= 0 and y >= 0 and x + w <= image.width and y + h <= image.height
        sheets[part] = (data, image)
    for i, view in enumerate(VIEWS):
        rgb = np.array(Image.open(ROOT / receipts[i]["source"]).convert("RGB"))
        expected = coverage(rgb, view["cutoff"])
        recomposed = np.zeros(expected.shape, dtype=np.uint8)
        for part, (data, image) in sheets.items():
            frame = data["sprite_sheet"]["frames"][i]
            x, y, w, h = frame["rect"]
            local = np.array(image.crop((x, y, x + w, y + h)))
            info = receipts[i]["parts"][part]
            left, top, right, bottom = info["source_bounds"]
            dx, dy = info["placement"]
            region = local[dy:dy + bottom - top, dx:dx + right - left]
            visible = region[:, :, 3] > 0
            assert np.array_equal(region[:, :, :3][visible], rgb[top:bottom, left:right][visible])
            recomposed[top:bottom, left:right] += visible
            if part != "hull":
                kind = "top" if part == "top-turret" else "bottom"
                hull_frame = sheets["hull"][0]["sprite_sheet"]["frames"][i]
                marker = hull_frame["markers"]["turret_" + kind]
                assert marker[:2] == [view[kind + "_pivot"][k] + receipts[i]["integer_offset"][k] for k in (0, 1)]
        assert np.array_equal(recomposed, expected.astype(np.uint8))
        results.append(dict(frame=view["name"], saved_rgb_identity=True,
                            exact_visible_recomposition=True, no_overlapping_layers=True))
    save_json(OUT / "verification.json", dict(sidecar_dimensions_and_bearings=True,
                                              transparent_backgrounds=True, pixel_resampling=False, frames=results))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    atlases = {
        "hull": Image.new("RGBA", (CELL[0] * 5, CELL[1])),
        "top-turret": Image.new("RGBA", (TURRET_CELL[0] * 5, TURRET_CELL[1])),
        "bottom-turret": Image.new("RGBA", (TURRET_CELL[0] * 5, TURRET_CELL[1])),
    }
    frames = {part: [] for part in atlases}
    receipts = []
    previews = Image.new("RGB", (5 * 460, 3 * 430), "#393649")
    for i, view in enumerate(VIEWS):
        source = ROOT / f"docs/art/phoenix-bullfrog-{view['source']}.png"
        im = Image.open(source).convert("RGB")
        rgb = np.array(im)
        cover = coverage(rgb, view["cutoff"])
        # Include the full ink/antialias fringe around the traced contour; the
        # paper mask still excludes the surrounding background. Stop the top
        # selection at its bearing seam so the bridge stays with the hull.
        top = ndimage.binary_dilation(polygon_mask(im.size, view["top"]), iterations=7) & cover
        top[view["top_pivot"][1] + 3:, :] = False
        bottom = ndimage.binary_dilation(polygon_mask(im.size, view["bottom"]), iterations=5) & cover
        bottom[:view["bottom_pivot"][1] - 8, :] = False
        # A cut can detach a few original contour pixels just outside a traced
        # polygon. Keep these with the nearest turret, rather than leaving
        # floating ink shards on the hull or throwing original pixels away.
        remaining = cover & ~top & ~bottom
        labels, _ = ndimage.label(remaining)
        areas = np.bincount(labels.ravel())
        areas[0] = 0
        main_hull = int(areas.argmax())
        distances = [ndimage.distance_transform_edt(~top), ndimage.distance_transform_edt(~bottom)]
        for label in np.flatnonzero(areas):
            if label == main_hull:
                continue
            fragment = labels == label
            nearest = [float(distance[fragment].min()) for distance in distances]
            if min(nearest) < 45:
                (top if nearest[0] < nearest[1] else bottom)[fragment] = True
        assert not np.any(top & bottom)
        masks = {"hull": cover & ~top & ~bottom, "top-turret": top, "bottom-turret": bottom}
        assert np.array_equal(masks["hull"] | top | bottom, cover)
        offset = [(CELL[0] - im.width) // 2, (CELL[1] - im.height) // 2]
        frame_receipt = dict(source=source.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                             source_size=list(im.size), name=view["name"], integer_offset=offset,
                             source_turret_polygons={"top": view["top"], "bottom": view["bottom"]}, parts={})
        for row, (part, mask) in enumerate(masks.items()):
            rgba = np.dstack((rgb, mask.astype(np.uint8) * 255))
            layer = Image.fromarray(rgba)
            box = bounds(mask)
            if part == "hull":
                size, shift = CELL, offset
                pivot = [view["centre"][k] + shift[k] for k in (0, 1)]
                markers = {"turret_top": [view["top_pivot"][k] + shift[k] for k in (0, 1)] + [1],
                           "turret_bottom": [view["bottom_pivot"][k] + shift[k] for k in (0, 1)] + [1]}
            else:
                size = TURRET_CELL
                kind = "top" if part == "top-turret" else "bottom"
                pivot = list(TOP_PIVOT if kind == "top" else BOTTOM_PIVOT)
                shift = [pivot[k] - view[kind + "_pivot"][k] for k in (0, 1)]
                markers = {}
            local = Image.new("RGBA", size)
            # Source rectangles and placements are integer-only; paste has no
            # mask so Pillow copies RGBA bytes rather than alpha-compositing.
            crop = layer.crop(box)
            dest = [box[k] + shift[k] for k in (0, 1)]
            assert dest[0] >= 0 and dest[1] >= 0, (view["name"], part, dest)
            assert dest[0] + crop.width <= size[0] and dest[1] + crop.height <= size[1], (view["name"], part, dest, crop.size)
            local.paste(crop, dest)
            # Verify every retained RGB pixel against its source, not a golden.
            copied = np.array(local.crop((dest[0], dest[1], dest[0] + crop.width, dest[1] + crop.height)))
            visible = np.array(crop)[:, :, 3] > 0
            assert np.array_equal(copied[:, :, :3][visible], rgb[box[1]:box[3], box[0]:box[2]][visible])
            atlases[part].paste(local, (i * size[0], 0))
            frames[part].append(dict(name=view["name"], rect=[i * size[0], 0, *size], pivot=pivot,
                                     camera_azimuth_degrees=view["azimuth"], mirror_x=False,
                                     depth_scale=0, depth_offset=0, markers=markers))
            frame_receipt["parts"][part] = dict(source_bounds=box, placement=dest[:2],
                                               retained_pixels=int(mask.sum()), rgb_identity=True)
            preview = local.copy()
            preview.thumbnail((450, 365))  # Review image only, never an atlas.
            bg = Image.new("RGBA", preview.size, "#393649")
            bg.alpha_composite(preview)
            px = i * 460 + (460 - preview.width) // 2
            py = row * 430 + 42 + (365 - preview.height) // 2
            previews.paste(bg.convert("RGB"), (px, py))
            ImageDraw.Draw(previews).text((i * 460 + 8, row * 430 + 12), view["name"] + " / " + part, fill="white")
        receipts.append(frame_receipt)
    for part, atlas in atlases.items():
        stem = "bullfrog-" + part
        atlas.save(OUT / (stem + ".png"))
        # Five source regions, eight explicit directional entries, with the
        # reverse-side bearings referencing existing rectangles by reflection.
        entries = list(frames[part])
        for index, name, bearing in [(3, "stern_starboard", 225), (2, "starboard", 270), (1, "bow_starboard", 315)]:
            frame = dict(frames[part][index])
            frame.update(name=name, camera_azimuth_degrees=bearing, mirror_x=True)
            entries.append(frame)
        sidecar = dict(schema_version=1, coordinate_system="frame_pixels_top_left_y_down", id=stem,
                       image_size=list(atlas.size), files={"sprite": stem + ".png"}, billboard_mode="eight",
                       render_scale=1 if part == "hull" else TURRET_CELL[1] / CELL[1],
                       sprite_sheet={"kind": "directions", "frames": entries})
        save_json(OUT / (stem + ".sprite.json"), sidecar)
    previews.save(OUT / "extraction-preview.png")
    save_json(OUT / "extraction-receipt.json", dict(rgb_modified=False, resampling=False, inpainting=False,
                                                  background="border-connected pale paper removed by alpha mask",
                                                  frames=receipts))
    verify_saved(receipts)
    print("Created three atlases and sidecars. All 15 layers retain exact source RGB pixels.")
    print(OUT)


if __name__ == "__main__":
    main()
