"""Render registration evidence from tracked materials; no raw inputs required."""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "content/base/assets/cruiser"
OUT = ROOT / "build/cruiser-markers"
OUT.mkdir(parents=True, exist_ok=True)
font = ImageFont.load_default(size=10)
banks = ["bow_port", "bow_starboard", "port_fore", "port_aft", "starboard_fore", "starboard_aft"]
turrets = ["fore_dorsal", "fore_ventral", "aft_dorsal", "aft_ventral", "mid_dorsal"]
for stem in ("hull", "turret"):
    sheet = Image.open(ASSETS / f"{stem}-albedo.png").convert("RGBA")
    material = json.loads((ASSETS / f"{stem}.material.json").read_text())
    for frame in material["frames"]:
        x, y, w, h = frame["rect"]
        crop = sheet.crop((x, y, x+w, y+h))
        if frame["mirror_x"]:
            crop = ImageOps.mirror(crop)
        canvas = Image.new("RGBA", (w+60, h+85), (10, 16, 24, 255))
        canvas.alpha_composite(crop, (30, 30))
        draw = ImageDraw.Draw(canvas)
        if "--grid" in sys.argv:
            for gx in range(0, w, 50):
                draw.line((30+gx, 30, 30+gx, 30+h), fill="#30404d")
                draw.text((30+gx, 19), str(gx), fill="white", font=font)
            for gy in range(0, h, 50):
                draw.line((30, 30+gy, 30+w, 30+gy), fill="#30404d")
                draw.text((2, 30+gy), str(gy), fill="white", font=font)
        for name, point in frame["attachments"].items():
            colour, label = "orange", name
            if name.startswith("turret_"):
                label = "T" + str(turrets.index(name[7:])+1)
            elif name.startswith("pdc_"):
                colour, label = "deepskyblue", "P" + name.split("_")[1][0] + name[-1]
            elif name.startswith("missile_"):
                colour = "lime"
                label = "M" + str(banks.index(name[8:-2])+1) if name.endswith("_1") else ""
            elif name.startswith("engine_"):
                colour, label = "tomato", "E" + name[7:][0]
            elif name.startswith("bay_"):
                colour, label = "violet", "B" + name[4:][0]
            px = 30 + (w-point["x"] if frame["mirror_x"] else point["x"])
            py = 30 + point["y"]
            draw.ellipse((px-3, py-3, px+3, py+3), outline=colour, width=1,
                         fill=colour if point["layer"] >= 0 else None)
            if label:
                draw.text((px+5, py-12), label, fill=colour, font=font)
        draw.text((8, 8), f"{stem}: {frame['name']} ({frame['azimuth']} degrees)", fill="white", font=font)
        draw.text((8, h+36), "T turrets / P PDC / M missile banks / E engines / B bays", fill="white", font=font)
        draw.text((8, h+52), "Filled: visible. Hollow: inferred hidden. p: port / s: starboard.", fill="white", font=font)
        canvas.convert("RGB").save(OUT / f"{stem}-{frame['name']}.png")
print(f"Marker previews: {OUT}")
