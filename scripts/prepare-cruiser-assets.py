"""Explicit art preparation; builds consume the tracked results, never raw/.

python scripts/prepare-cruiser-assets.py --hull-depth PATH --turret-depth PATH
Image-generated depth is a draft, not geometry. Originals remain unchanged.
"""
import argparse
import copy
import json
import math
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "content/base/assets/cruiser"
PREFIX = "phoenix-base:assets/cruiser/"


def prepare(stem, draft_path):
    source = json.loads((ROOT / f"raw/cruiser/{stem}.sprite.json").read_text())
    albedo = Image.open(ROOT / f"raw/cruiser/{stem}.png").convert("RGBA")
    draft = Image.open(draft_path).convert("L")
    if draft.size != albedo.size:
        raise ValueError("Depth must be registered at the original atlas dimensions")
    depth = Image.new("L", albedo.size, 128)
    normal = Image.new("RGB", albedo.size, (128, 128, 255))
    frames = []
    # Hand-registered nozzle centres; hidden nozzles remain behind the hull.
    nozzles = [[(183, 168), (303, 168)], [(698, 88), (719, 132)],
               [(874, 157), (874, 160)], [(440, 202), (550, 192)],
               [(183, 157), (354, 157)]]
    muzzles = [(266, 140), (43, 88), (30, 90), (33, 85), (255, 50)]
    pdc_points = [
        [(435, 240), (420, 260), (405, 270), (390, 280)],
        [(120, 280), (300, 255), (475, 220), (620, 180)],
        [(100, 210), (350, 215), (585, 212), (780, 210)],
        [(100, 110), (205, 165), (315, 220), (430, 280)],
        [(100, 90), (80, 130), (65, 170), (45, 220)],
    ]
    bank_points = [
        [(412, 148), (72, 148), (460, 160), (440, 130), (24, 160), (44, 130)],
        [(30, 190), (75, 140), (300, 186), (610, 120), (270, 140), (585, 78)],
        [(20, 145), (20, 145), (200, 155), (645, 155), (200, 145), (645, 145)],
        [(30, 80), (45, 60), (70, 105), (235, 160), (90, 75), (260, 125)],
        [(410, 90), (115, 90), (435, 130), (465, 170), (90, 130), (60, 170)],
    ]
    for i, f in enumerate(source["sprite_sheet"]["frames"]):
        angle = f["camera_azimuth_degrees"]
        unique = i if i < 5 else 8 - i
        a = math.radians(angle)
        span = math.hypot(3.3 * math.cos(a), 1.5 * math.sin(a)) if stem == "hull" else 1.8
        frame = dict(name=f["name"], rect=f["rect"], pivot=f["pivot"], azimuth=angle,
                     mirror_x=f.get("mirror_x", False), depth_scale=round(span, 5),
                     depth_offset=0, attachments={})
        x, y, w, h = f["rect"]
        if i < 5:
            box = (x, y, x + w, y + h)
            alpha = albedo.crop(box).getchannel("A")
            field = draft.crop(box)
            values = sorted(d for d, p in zip(field.getdata(), alpha.getdata()) if p > 192)
            if not values:
                raise ValueError("Empty sprite coverage")
            low, high = values[len(values) // 50], values[len(values) * 49 // 50]
            high = max(high, low + 1)
            field = field.point(lambda v: round(max(.04, min(.96, .04 + (v-low)/(high-low)*.92))*255))
            field = field.filter(ImageFilter.GaussianBlur(max(1, min(w, h)/180)))
            depth.paste(field, (x, y))
            p = field.load()
            n = Image.new("RGB", (w, h))
            dest = n.load()
            step = max(2, min(w, h) // 80)
            for py in range(h):
                ya, yb = max(0, py-step), min(h-1, py+step)
                for px in range(w):
                    xa, xb = max(0, px-step), min(w-1, px+step)
                    dx = (p[xb, py]-p[xa, py])/255 * span*h/max(1, xb-xa)
                    dy = (p[px, yb]-p[px, ya])/255 * span*h/max(1, yb-ya)
                    length = math.sqrt(dx*dx + dy*dy + 1)
                    dest[px, py] = tuple(round((v/length*.5+.5)*255) for v in (-dx, dy, 1))
            normal.paste(n, (x, y))
        # Reflection transforms source coordinates at rendering; never reflect twice.
        original = source["sprite_sheet"]["frames"][unique]
        for key, v in original["markers"].items():
            frame["attachments"][key] = dict(x=v[0], y=v[1], layer=v[2], depth_offset=0)
        if stem == "turret":
            mx, my = muzzles[unique]
            frame["attachments"]["muzzle"] = dict(x=mx, y=my, layer=-1 if unique == 4 else 1, depth_offset=0)
        else:
            for side, (mx, my) in zip(("port", "starboard"), nozzles[unique]):
                frame["attachments"]["engine_"+side] = dict(x=mx, y=my,
                    layer=1 if unique >= 3 else -1, depth_offset=0)
            # Projection of physical socket reference positions into each registered view.
            # Per-view image registration is deliberately separate from simulation geometry.
            ua = math.radians(unique*45)
            px, py = original["pivot"]
            def socket(name, forward, port, height=0):
                sx = px + (-forward*math.sin(ua)+port*math.cos(ua))*w/330
                sy = py - height*h/90 - (forward*math.cos(ua)+port*math.sin(ua))*h/850
                frame["attachments"][name] = dict(x=round(max(1,min(w-2,sx)),2),
                    y=round(max(1,min(h-2,sy)),2), layer=1 if port*math.sin(ua)+forward*math.cos(ua)>0 else -1,
                    depth_offset=0)
            for side, port in (("port", 60), ("starboard", -60)):
                for n, forward in enumerate((105, 35, -35, -105), 1):
                    socket(f"pdc_{side}_{n}", forward, port)
                    point = pdc_points[unique][n-1]
                    if side == "starboard":
                        point = (w-point[0], point[1]) if unique in (0, 4) else (point[0], point[1]-22)
                    marker = frame["attachments"][f"pdc_{side}_{n}"]
                    marker.update(x=point[0], y=point[1], layer=1 if side == "port" and unique in (1,2,3) else -1)
                socket(f"bay_{side}", -35, port, -10)
            for bank_index, (bank, forward, port) in enumerate((("bow_port",140,38),("bow_starboard",140,-38),
                    ("port_fore",85,62),("port_aft",-85,62),
                    ("starboard_fore",85,-62),("starboard_aft",-85,-62))):
                for tube in range(4):
                    socket(f"missile_{bank}_{tube+1}",forward+(tube%2)*3,port, (tube//2)*3)
                    marker = frame["attachments"][f"missile_{bank}_{tube+1}"]
                    bx, by = bank_points[unique][bank_index]
                    marker.update(x=bx+(tube%2-.5)*10, y=by+(tube//2-.5)*10)
                    sampled = (depth.getpixel((x+int(marker["x"]),y+int(marker["y"])))/255-.5)*span
                    marker["depth_offset"] = round((forward*math.cos(ua)+port*math.sin(ua))/90-sampled,5)
            # Engine offsets keep inferred far-side exhaust behind the calibrated volume.
            for key, marker in frame["attachments"].items():
                ix, iy = min(w-1,int(marker["x"])), min(h-1,int(marker["y"]))
                sampled = (depth.getpixel((x+ix,y+iy))/255-.5)*span
                if key.startswith("engine_"):
                    marker["depth_offset"] = round(-1.55*math.cos(ua)-sampled,5)
                elif key.startswith("turret_"):
                    forward = 85 if "fore" in key else -85 if "aft" in key else 0
                    marker["depth_offset"] = round(forward*math.cos(ua)/90-sampled,5)
                elif key.startswith("bay_"):
                    port = 60 if key == "bay_port" else -60
                    marker["depth_offset"] = round((-35*math.cos(ua)+port*math.sin(ua))/90-sampled,5)
                elif key.startswith("pdc_"):
                    port = 65 if "starboard" not in key else -65
                    forward = (105,35,-35,-105)[int(key[-1])-1]
                    marker["depth_offset"] = round((forward*math.cos(ua)+port*math.sin(ua))/90-sampled,5)
        if frame["mirror_x"]:
            # The mirrored port-side drawing depicts the physical starboard side.
            # Swap lateral identities, retaining unreflected source coordinates.
            frame["attachments"] = {
                name.replace("starboard", "@side@").replace("port", "starboard").replace("@side@", "port"): marker
                for name, marker in frame["attachments"].items()}
        frames.append(frame)
    OUT.mkdir(parents=True, exist_ok=True)
    # Palette packaging keeps the production base below its bounded archive size.
    # Source registration and original RGBA drawings remain untouched in raw/.
    albedo.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).save(
        OUT / f"{stem}-albedo.png", optimize=True)
    depth.save(OUT / f"{stem}-depth.png", optimize=True)
    normal.save(OUT / f"{stem}-normal.png", optimize=True)
    sources = ROOT / "art/cruiser"
    sources.mkdir(parents=True, exist_ok=True)
    draft.save(sources / f"{stem}-depth-draft.png", optimize=True)
    material = dict(albedo=PREFIX+f"{stem}-albedo.png", depth=PREFIX+f"{stem}-depth.png",
                    normal=PREFIX+f"{stem}-normal.png", frames=frames)
    sidecar = copy.deepcopy(source)
    sidecar.pop("$schema", None)
    sidecar["files"] = dict(sprite=f"{stem}-albedo.png", depth=f"{stem}-depth.png", normal=f"{stem}-normal.png")
    sidecar["depth_encoding"] = dict(channel="r", color_space="linear", white="near", quality="calibrated_draft")
    sidecar["normal_encoding"] = dict(space="view", x="right", y="up", z="toward_viewer", quality="derived_from_calibrated_depth")
    sidecar["billboard_mode"] = "eight"
    sidecar["sprite_sheet"]["frames"] = [dict(name=f["name"], rect=f["rect"], pivot=f["pivot"],
        camera_azimuth_degrees=f["azimuth"], mirror_x=f["mirror_x"], depth_scale=f["depth_scale"], depth_offset=0,
        markers={k:[m["x"],m["y"],m["layer"]] for k,m in f["attachments"].items()},
        attachment_depth_offsets={k:m["depth_offset"] for k,m in f["attachments"].items()}) for f in frames]
    (OUT / f"{stem}.sprite.json").write_text(json.dumps(sidecar, indent=2)+"\n")
    (OUT / f"{stem}.material.json").write_text(json.dumps(material, indent=2)+"\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hull-depth", required=True, type=Path)
    parser.add_argument("--turret-depth", required=True, type=Path)
    args = parser.parse_args()
    prepare("hull", args.hull_depth)
    prepare("turret", args.turret_depth)
