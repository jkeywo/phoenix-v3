"""Rasterise the base game's editable vector radar symbols into PNG sprites.

The runtime loads the PNG assets; this explicit art preparation step needs Pillow.
Icons face right at heading zero and rotate with their entity's heading.
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw

OUTPUT = Path(__file__).resolve().parents[1] / "content/base/assets/radar"


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in ("ship", "asteroid", "sun", "contact"):
        image = Image.new("RGBA", (256, 256))
        draw = ImageDraw.Draw(image)

        def path(points):
            draw.line([(128 + x * 104, 128 + y * 104) for x, y in points],
                      fill="white", width=14, joint="curve")

        if name == "ship":
            path([(1, 0), (-.7, -.65), (-.4, 0), (-.7, .65), (1, 0)])
        elif name == "asteroid":
            path([(-.9, -.35), (-.35, -.9), (.45, -.75), (.95, -.05),
                  (.55, .75), (-.3, .9), (-.85, .4), (-.9, -.35)])
            path([(-.45, -.05), (-.1, -.35)])
        else:
            r = .48 if name == "sun" else .5
            path([(r * math.cos(i * math.pi / 32), r * math.sin(i * math.pi / 32))
                  for i in range(65)])
            if name == "sun":
                for i in range(8):
                    x, y = math.cos(i * math.pi / 4), math.sin(i * math.pi / 4)
                    path([(x * .72, y * .72), (x, y)])
        image.resize((64, 64), Image.Resampling.LANCZOS).save(OUTPUT / f"{name}.png")


if __name__ == "__main__":
    main()
