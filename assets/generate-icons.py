#!/usr/bin/env python3
"""Regenerate platform icons from beamer-icon.svg (requires rsvg-convert)."""

import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


assets = Path(__file__).resolve().parent
svg = assets / "beamer-icon.svg"
rsvg = shutil.which("rsvg-convert")
if not rsvg:
    sys.exit("rsvg-convert is required to regenerate icons")


def render(size: int, destination: Path) -> bytes:
    data = subprocess.check_output([rsvg, "-w", str(size), "-h", str(size), str(svg)])
    destination.write_bytes(data)
    return data


render(512, assets / "beamer-icon.png")

# Windows ICO supports PNG-encoded images, preserving clean alpha at every size.
sizes = (16, 24, 32, 48, 64, 128, 256)
with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    images = [render(size, temporary / f"{size}.png") for size in sizes]
    offset = 6 + 16 * len(images)
    with (assets / "beamer-icon.ico").open("wb") as output:
        output.write(struct.pack("<HHH", 0, 1, len(images)))
        for size, data in zip(sizes, images):
            output.write(struct.pack("<BBBBHHII", size if size < 256 else 0,
                                     size if size < 256 else 0, 0, 0, 1, 32,
                                     len(data), offset))
            offset += len(data)
        for data in images:
            output.write(data)

    # Modern ICNS icon records contain PNG data at standard and Retina sizes.
    records = []
    for kind, size in ((b"icp4", 16), (b"icp5", 32), (b"icp6", 64),
                       (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
                       (b"ic10", 1024)):
        data = render(size, temporary / f"mac-{size}.png")
        records.append(kind + struct.pack(">I", len(data) + 8) + data)
    with (assets / "beamer-icon.icns").open("wb") as output:
        output.write(b"icns" + struct.pack(">I", 8 + sum(map(len, records))))
        output.writelines(records)
