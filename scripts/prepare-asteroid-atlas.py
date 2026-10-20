"""Pack existing V3 asteroid maps into a two-entry training atlas.

The second entry uses the same source artwork in a rotated atlas tile. Normal
vectors rotate with the tile. This demonstrates entry selection without adding
new generated artwork or a dependency on V2 assets.
"""
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "content/base/assets/comic"
OUTPUT = ROOT / "content/test/assets"
OUTPUT.mkdir(parents=True, exist_ok=True)

for kind in ("albedo", "normal", "depth"):
    source = Image.open(SOURCE / f"asteroid-{kind}.png").convert("RGBA")
    variant = source.transpose(Image.Transpose.ROTATE_180)
    if kind == "normal":
        r, g, b, a = variant.split()
        variant = Image.merge("RGBA", (ImageChops.invert(r), ImageChops.invert(g), b, a))
    atlas = Image.new("RGBA", (source.width * 2, source.height))
    atlas.paste(source, (0, 0))
    atlas.paste(variant, (source.width, 0))
    atlas.save(OUTPUT / f"asteroid-atlas-{kind}.png", optimize=True)
