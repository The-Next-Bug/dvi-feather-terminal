#!/usr/bin/env python3
"""Convert docgen's binary PPMs (P6, 8-bit) to PNGs, standard library only.

Usage: ppm2png.py IN.ppm... OUTDIR
Each IN.ppm becomes OUTDIR/IN.png (scanlines filtered with Sub, which
suits the flat colors of a text screen).
"""

import os
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1  # the single whitespace byte after maxval
    if fields[0] != b"P6" or fields[3] != b"255":
        sys.exit(f"{path}: not an 8-bit binary PPM")
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos:pos + width * height * 3]


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def write_png(path, width, height, rgb):
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        row = rgb[y * stride:(y + 1) * stride]
        raw.append(1)  # Sub
        raw += row[:3]
        raw += bytes((row[i] - row[i - 3]) & 0xFF for i in range(3, stride))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out_dir = sys.argv[-1]
    os.makedirs(out_dir, exist_ok=True)
    for src in sys.argv[1:-1]:
        width, height, rgb = read_ppm(src)
        name = os.path.splitext(os.path.basename(src))[0] + ".png"
        write_png(os.path.join(out_dir, name), width, height, rgb)


if __name__ == "__main__":
    main()
