#!/usr/bin/env python3
"""Generate the firmware's startup logo from a PNG.

Usage: logo_gen.py [--name NAME] SOURCE.png OUT.c

SOURCE is a non-interlaced PNG, 8 bits per channel (grayscale, RGB,
palette, with or without alpha; alpha is composited over black), at most
800 x 576 pixels and a multiple of 8 pixels wide. It is shown as is: scale
it beforehand.

The display has 4 levels (RGB222) per color plane. Each channel is rounded
to the nearest level, and per 8-pixel group to at most two levels (the pair
with the least squared error against the source): the PicoDVI encoder
needed that, the HSTX renderer does not (bd follow-up). The planes that are
not all black must show the same levels (a single-hue or gray logo): the
firmware decodes one plane's levels and shows them, tinted, in each plane
of `planes`.

The output defines `const splash_image_t NAME` (see src/render/splash.h): the
levels, row by row, run-length encoded as bytes `level << 6 | (run - 1)`.
Compiled with LOGO_IN_FLASH=1 (firmware), the encoded data goes in a
.flashdata section, which the SDK's copy_to_ram link leaves in flash, so it
is read through XIP at startup instead of taking up SRAM for good.
"""

import argparse
import struct
import sys
import zlib

MAX_WIDTH = 800
MAX_HEIGHT = 576
LEVELS = (0, 85, 170, 255)
PLANE_NAMES = ("blue", "green", "red")


def read_png(path):
    """Return (width, height, rows of (r, g, b) tuples), alpha over black."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, palette, trns = 8, b"", None, None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace:
        sys.exit(f"{path}: need a non-interlaced PNG with 8 bits per channel "
                 f"(got depth {depth}, color type {color}, interlace {interlace})")

    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[pos], bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xff
            elif kind == 2:
                line[i] = (line[i] + b) & 0xff
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xff
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pred) & 0xff
        prev = line

        pixels = []
        for x in range(width):
            px = line[x * channels:(x + 1) * channels]
            if color == 0:
                rgb, alpha = (px[0],) * 3, 255
            elif color == 4:
                rgb, alpha = (px[0],) * 3, px[1]
            elif color == 2:
                rgb, alpha = tuple(px), 255
            elif color == 6:
                rgb, alpha = tuple(px[:3]), px[3]
            else:
                rgb = palette[px[0]]
                alpha = trns[px[0]] if trns and px[0] < len(trns) else 255
            pixels.append(tuple(v * alpha // 255 for v in rgb))
        rows.append(pixels)
    return width, height, rows


def nearest(v, choices):
    # Ties go to the lower level, as in the firmware (raster.c picks_hi).
    return min(choices, key=lambda lv: (abs(LEVELS[lv] - v), lv))


def quantise_row(values):
    """Levels for one row of 8-bit values, at most two per 8-pixel group."""
    out = []
    for g in range(0, len(values), 8):
        group = values[g:g + 8]
        levels = [nearest(v, range(4)) for v in group]
        if len(set(levels)) > 2:
            best = None
            for lo in range(4):
                for hi in range(lo + 1, 4):
                    err = sum((LEVELS[nearest(v, (lo, hi))] - v) ** 2 for v in group)
                    if best is None or err < best[0]:
                        best = (err, lo, hi)
            levels = [nearest(v, best[1:]) for v in group]
        out.extend(levels)
    return out


def rle(levels):
    out, i = bytearray(), 0
    while i < len(levels):
        run = 1
        while i + run < len(levels) and levels[i + run] == levels[i] and run < 64:
            run += 1
        out.append(levels[i] << 6 | (run - 1))
        i += run
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--name", default="splash_logo")
    ap.add_argument("source")
    ap.add_argument("out")
    args = ap.parse_args()

    width, height, rows = read_png(args.source)
    if width > MAX_WIDTH or height > MAX_HEIGHT or width % 8:
        sys.exit(f"{args.source}: {width}x{height}; need at most {MAX_WIDTH}x{MAX_HEIGHT} "
                 "and a width that is a multiple of 8")

    # Planes 0 blue, 1 green, 2 red, as in RGB222 (and TMDS lanes 0-2).
    rounded = [[[nearest(px[2 - p], range(4)) for px in row] for row in rows] for p in range(3)]
    planes = [p for p in range(3) if any(any(r) for r in rounded[p])]
    if any(rounded[p] != rounded[planes[0]] for p in planes[1:]):
        sys.exit(f"{args.source}: planes {', '.join(PLANE_NAMES[p] for p in planes)} differ; the logo "
                 "must be a single hue (or gray), black elsewhere")
    source = planes[0] if planes else 1
    levels = []
    for row in rows:
        levels.extend(quantise_row([px[2 - source] for px in row]))
    data = rle(levels)
    mask = sum(1 << p for p in planes)

    with open(args.out, "w") as f:
        f.write(f"// Generated by src/render/logo_gen.py from {args.source.split('/')[-1]}: do not edit.\n")
        f.write(f"// {width}x{height}, planes {', '.join(PLANE_NAMES[p] for p in planes) or 'none'}, "
                f"{len(data)} bytes run-length encoded.\n\n")
        f.write('#include "splash.h"\n\n')
        f.write("#if LOGO_IN_FLASH\n"
                "#define LOGO_DATA __attribute__((section(\".flashdata.logo\")))\n"
                "#else\n"
                "#define LOGO_DATA\n"
                "#endif\n\n")
        f.write(f"static const uint8_t data[{len(data)}] LOGO_DATA = {{\n")
        for i in range(0, len(data), 16):
            f.write("\t" + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write(f"const splash_image_t {args.name} = {{\n")
        f.write(f"\t.width = {width},\n\t.height = {height},\n\t.planes = 0x{mask:x},\n")
        f.write("\t.data = data,\n\t.size = sizeof data,\n};\n")
    print(f"{args.name}: {width}x{height}, planes 0x{mask:x}, {len(data)} bytes")


if __name__ == "__main__":
    main()
