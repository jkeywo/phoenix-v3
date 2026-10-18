"""Generate the six-region workload with synthetic depth-backed comic sprites.

No shipped art, ships or balance data are used. This is manually invoked
performance evidence, never an automatic timing assertion.
"""
import json
import math
from pathlib import Path
import runpy
import struct
import zipfile
import zlib

root = Path(__file__).resolve().parents[2]
runpy.run_path(str(root / "tests/manual/bridge_fixture.py"))
output = root / "build/bridge-smoke"


def png(depth):
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    rows = bytearray()
    for y in range(64):
        rows.append(0)
        for x in range(64):
            qx, qy = (x - 31.5) / 30, (y - 31.5) / 30
            r2 = qx*qx + qy*qy
            value = round(128 + 110*math.sqrt(max(0, 1-r2))) if depth else 210
            rows.extend((value, value, value, 255 if r2 < 1 else 0))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB",64,64,8,6,0,0,0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND",b"")


with zipfile.ZipFile(output / "bridge.zip") as archive:
    files = {name: archive.read(name) for name in archive.namelist()}
definitions = json.loads(files["definitions.json"])
definitions["schema"] = 3
definitions["entities"][0]["render"]["sprite"] = {
    "albedo":"test-core:assets/comic.png", "depth":"test-core:assets/depth.png",
    "frames":[{"name":"sphere", "rect":[0,0,64,64], "pivot":[32,32], "depth_scale":1.0}]}
definitions["maps"][0]["lighting"] = {"sun_direction":[-.6,.6,-.5], "shadows":True,"hatch_thickness":2}
files["definitions.json"] = json.dumps(definitions).encode()
files["assets/comic.png"] = png(False)
files["assets/depth.png"] = png(True)
with zipfile.ZipFile(output / "bridge-comic.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for name, data in files.items():
        archive.writestr(name, data)
print(output / "bridge-comic.zip")
