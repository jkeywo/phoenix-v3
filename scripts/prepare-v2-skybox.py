"""Bake the development skybox without modifying the historical V2 source."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path, help="V2 phoenix_space_cubemap.png")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1] / "content/test-3d"
destination = root / "assets/skyboxes/phoenix_space_cubemap.png"
with Image.open(args.source) as image:
    if image.size != (2048, 12288):
        raise ValueError("Expected V2's 2048 by 12288 vertical cube strip")
    result = Image.new("RGB", (512, 3072))
    for face in range(6):
        tile = image.crop((0, face * 2048, 2048, (face + 1) * 2048))
        result.paste(tile.resize((512, 512), Image.Resampling.LANCZOS), (0, face * 512))
    destination.parent.mkdir(parents=True, exist_ok=True)
    result.save(destination)
sources = root / "sources.json"
data = json.loads(sources.read_text(encoding="utf8"))
record = next(row for row in data if row.get("asset") == "assets/skyboxes/phoenix_space_cubemap.png")
record["sha256"] = hashlib.sha256(args.source.read_bytes()).hexdigest()
record["asset_sha256"] = hashlib.sha256(destination.read_bytes()).hexdigest()
sources.write_text(json.dumps(data, indent="\t") + "\n", encoding="utf8")
