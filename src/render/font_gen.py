#!/usr/bin/env python3
"""Generate a firmware font table from a PSF or BDF font.

Usage: font_gen.py [--scale-y N] [--pad-x P] [--flash] SOURCE WIDTH HEIGHT NAME OUT.c

SOURCE is a PSF console font (optionally .gz) or a BDF font; its glyphs must
be WIDTH - P pixels wide (WIDTH up to 16) and HEIGHT / N pixels high
(--scale-y repeats each row N times). --pad-x adds P blank columns on the
right of each glyph; line drawing, blocks and shades carry on into them, so
they still join the next cell (6x12 in the 1024-wide layout's 7-pixel
cells). The output defines
`const uint8_t NAME[FONT_GLYPHS * HEIGHT]` (uint16_t for fonts wider than 8),
with --flash in the firmware's flash (FONT_FLASH_DATA, src/screen/font.h).

The table has 256 glyphs, indexed by the glyph codes the screen buffer stores
(see src/screen/font.h for the layout). It is stored glyph-major:
NAME[glyph * HEIGHT + row], one element per glyph row, least-significant bit
= leftmost pixel.

Glyphs are looked up by Unicode code point. The few that some fonts lack
(DEC control pictures and scan lines, half blocks) are drawn here.
"""

import argparse
import gzip
import struct
import sys

N_GLYPHS = 256

# 0x01-0x1F: DEC Special Graphics for 0x60-0x7E, in order.
DEC_GRAPHICS = "◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·"
# 0x80-0x9F: shades, blocks and double-line box drawing, for ANSI art.
EXTRAS = "░▒▓█▀▄▌▐■═║╔╗╚╝╠╣╦╩╬╒╓╕╖╘╙╛╜╞╟╡╢"
FALLBACK = {"◆": "♦"}

# 3x5 capitals for the control-picture glyphs (two letters, stacked).
MINI = {
    "C": ["111", "100", "100", "100", "111"],
    "F": ["111", "100", "110", "100", "100"],
    "H": ["101", "101", "111", "101", "101"],
    "L": ["100", "100", "100", "100", "111"],
    "N": ["111", "101", "101", "101", "101"],
    "R": ["110", "101", "110", "101", "101"],
    "T": ["111", "010", "010", "010", "010"],
    "V": ["101", "101", "101", "101", "010"],
}
CONTROL_PICTURES = {"␉": "HT", "␌": "FF", "␍": "CR", "␊": "LF", "␤": "NL", "␋": "VT"}

# The double and mixed box-drawing glyphs: line weights (0 none, 1 single,
# 2 double) of their up, down, left and right arms. Some fonts (the
# console-setup Terminus PSFs) map these to the single-line glyphs; then
# they are drawn here.
BOX_ARMS = {
    "═": (0, 0, 2, 2), "║": (2, 2, 0, 0), "╔": (0, 2, 0, 2), "╗": (0, 2, 2, 0),
    "╚": (2, 0, 0, 2), "╝": (2, 0, 2, 0), "╠": (2, 2, 0, 2), "╣": (2, 2, 2, 0),
    "╦": (0, 2, 2, 2), "╩": (2, 0, 2, 2), "╬": (2, 2, 2, 2), "╒": (0, 1, 0, 2),
    "╓": (0, 2, 0, 1), "╕": (0, 1, 2, 0), "╖": (0, 2, 1, 0), "╘": (1, 0, 0, 2),
    "╙": (2, 0, 0, 1), "╛": (1, 0, 2, 0), "╜": (2, 0, 1, 0), "╞": (1, 1, 0, 2),
    "╟": (2, 2, 0, 1), "╡": (1, 1, 2, 0), "╢": (2, 2, 1, 0),
}


def load_psf(path, width, height):
    """Return {char: rows}, each row an int with the MSB of `width` bits leftmost."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as f:
        data = f.read()

    if data[:4] == b"\x72\xb5\x4a\x86":  # PSF2
        _, _, hdr, flags, n, size, h, w = struct.unpack("<8I", data[:32])
        glyphs = [data[hdr + i * size:hdr + (i + 1) * size] for i in range(n)]
        table = data[hdr + n * size:] if flags & 1 else b""
        # Characters before the first 0xFE (sequence start) map to the glyph.
        entries = [c.split(b"\xfe")[0].decode("utf-8") for c in table.split(b"\xff")[:n]]
    elif data[:2] == b"\x36\x04":  # PSF1
        mode, h, w = data[2], data[3], 8
        n = 512 if mode & 1 else 256
        glyphs = [data[4 + i * h:4 + (i + 1) * h] for i in range(n)]
        entries = [""] * n
        if mode & 2:
            words = struct.unpack(f"<{(len(data) - 4 - n * h) // 2}H", data[4 + n * h:])
            g, in_seq = 0, False
            for word in words:
                if word == 0xFFFF:
                    g, in_seq = g + 1, False
                elif word == 0xFFFE:
                    in_seq = True
                elif not in_seq and g < n:
                    entries[g] += chr(word)
    else:
        sys.exit(f"{path}: not a PSF font")

    if (w, h) != (width, height):
        sys.exit(f"{path}: glyphs are {w}x{h}, need {width}x{height}")
    if not any(entries):
        sys.exit(f"{path}: font has no Unicode table")
    stride = (w + 7) // 8
    chars = {}
    for glyph, codepoints in zip(glyphs, entries):
        rows = [int.from_bytes(glyph[r * stride:(r + 1) * stride], "big") >> (stride * 8 - w)
                for r in range(h)]
        for ch in codepoints:
            chars.setdefault(ch, rows)
    return chars


def load_bdf(path, width, height):
    """Return {char: rows} for a BDF font whose cells are width x height."""
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    ascent = descent = None
    chars = {}
    i = 0
    while i < len(lines):
        words = lines[i].split()
        if words and words[0] == "FONT_ASCENT":
            ascent = int(words[1])
        elif words and words[0] == "FONT_DESCENT":
            descent = int(words[1])
        elif words and words[0] == "STARTCHAR":
            enc = bbx = None
            i += 1
            while not lines[i].startswith("BITMAP"):
                w = lines[i].split()
                if w[0] == "ENCODING":
                    enc = int(w[1])
                elif w[0] == "BBX":
                    bbx = tuple(int(v) for v in w[1:5])
                i += 1
            bw, bh, bx, by = bbx
            bitmap = [int(lines[i + 1 + r], 16) for r in range(bh)]
            i += bh + 1
            if enc is None or enc < 0:
                continue
            nbytes = (bw + 7) // 8
            rows = [0] * height
            top = ascent - (by + bh)  # first bitmap row, counted from the cell top
            for r, bits in enumerate(bitmap):
                y = top + r
                if 0 <= y < height:
                    # Bitmap rows are left-aligned in whole bytes.
                    row = bits >> (nbytes * 8 - bw)
                    shift = width - bw - bx
                    rows[y] = (row << shift if shift >= 0 else row >> -shift) & ((1 << width) - 1)
            chars[chr(enc)] = rows
        i += 1
    if ascent is None or ascent + descent != height:
        sys.exit(f"{path}: cell height is not {height}")
    return chars


def control_picture(pair, width, height):
    # Two 3x5 letters: one top-left, one lower-right.
    rows = [0] * height
    positions = ((height // 8, 0), (height // 8 + 7 * height // 16, min(4, width - 3)))
    for (top, left), letter in zip(positions, pair):
        for dy, bits in enumerate(MINI[letter]):
            if top + dy < height:
                rows[top + dy] |= int(bits, 2) << (width - 3 - left)
    return rows


def draw_box(arms, cx, cy, width, height):
    """A box-drawing glyph from its arms (BOX_ARMS) around the single lines'
    column cx and row cy (counted from the left and the top): a double line
    is two strokes, at cx +- 1 or cy +- 1, meeting the other arms' strokes
    as in the fonts that have these glyphs."""
    up, down, left, right = arms
    rows = [0] * height

    def dot(x, y):
        if 0 <= x < width and 0 <= y < height:
            rows[y] |= 1 << (width - 1 - x)

    def hline(y, x0, x1):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            dot(x, y)

    def vline(x, y0, y1):
        for y in range(min(y0, y1), max(y0, y1) + 1):
            dot(x, y)

    # Where a horizontal stroke from the side `sign` (-1 left, +1 right)
    # stops, at cx moved towards that side (near) or away from it (far).
    def h_end(sign, own, other):
        # own: the vertical arm on this stroke's side of the row (up for the
        # upper stroke), other: the opposite one.
        if own == 2:
            return cx + sign  # near stroke of the double vertical
        if own == 0 and other == 2:
            return cx - sign  # a corner: round the far stroke
        return cx

    def v_end(sign, own, other):
        if own == 2:
            return cy + sign
        if own == 0 and other == 2:
            return cy - sign
        return cy

    vertical = max(up, down)
    horizontal = max(left, right)
    for sign, weight, edge in ((-1, left, 0), (1, right, width - 1)):
        if weight == 1:
            if vertical == 2:
                stop = cx + sign if up == 2 and down == 2 else cx - sign
            else:
                stop = cx
            hline(cy, edge, stop)
        elif weight == 2:
            hline(cy - 1, edge, h_end(sign, up, down))
            hline(cy + 1, edge, h_end(sign, down, up))
    for sign, weight, edge in ((-1, up, 0), (1, down, height - 1)):
        if weight == 1:
            if horizontal == 2:
                stop = cy + sign if left == 2 and right == 2 else cy - sign
            else:
                stop = cy
            vline(cx, edge, stop)
        elif weight == 2:
            vline(cx - 1, edge, v_end(sign, left, right))
            vline(cx + 1, edge, v_end(sign, right, left))
    return rows


def build(chars, width, height):
    full = (1 << width) - 1
    blank = [0] * height

    def glyph(ch):
        return chars.get(ch) or chars.get(FALLBACK.get(ch, ""))

    # Scan lines 1/3/5/7/9 sit around the font's own horizontal line (scan 5).
    mid = next(r for r, bits in enumerate(glyph("─")) if bits)
    scan = {"⎺": 1, "⎻": (1 + mid) // 2, "⎼": (mid + height - 2) // 2, "⎽": height - 2}

    half, left = height // 2, (width + 1) // 2
    shade = glyph("░")
    drawn = {
        "▀": [full] * half + [0] * (height - half),
        "▄": [0] * half + [full] * (height - half),
        "▌": [full & ~((1 << (width - left)) - 1)] * height,
        "▐": [(1 << (width - left)) - 1] * height,
        "▓": shade and [b ^ full for b in shade],
    }

    # No double lines (the font maps them to the single ones): draw them.
    if glyph("═") is None or glyph("═") == glyph("─"):
        cx = width - 1 - max(b for b in range(width) if glyph("│")[height // 2] >> b & 1)
        cy = mid
        for ch, arms in BOX_ARMS.items():
            drawn[ch] = draw_box(arms, cx, cy, width, height)
        chars = {ch: rows for ch, rows in chars.items() if ch not in BOX_ARMS}

    table = [blank] * N_GLYPHS
    missing = []

    def place(code, ch):
        g = glyph(ch) or drawn.get(ch)
        if g is None and ch in scan:
            g = [full if r == scan[ch] else 0 for r in range(height)]
        if g is None and ch in CONTROL_PICTURES:
            g = control_picture(CONTROL_PICTURES[ch], width, height)
        if g is None:
            missing.append(ch)
            g = blank
        table[code] = g

    for i, ch in enumerate(DEC_GRAPHICS):
        place(0x01 + i, ch)
    for code in range(0x20, 0x7F):
        place(code, chr(code))
    for i, ch in enumerate(EXTRAS):
        place(0x80 + i, ch)
    for code in range(0xA0, 0x100):
        place(code, chr(code))
    if missing:
        sys.exit("font lacks glyphs for: " + "".join(missing))
    return table


def joins_right(ch):
    """Whether the glyph's right edge continues into the next cell."""
    return 0x2500 <= ord(ch) <= 0x259F or ch in "⎺⎻⎼⎽"


def pad_right(chars, width, pad):
    """Widen width-pixel glyphs by pad columns on the right (rows MSB-left)."""
    out = {}
    for ch, rows in chars.items():
        wide = []
        for row in rows:
            row <<= pad
            if joins_right(ch):
                # Each new column repeats the one a period to its left: 2
                # for the shades' checkered patterns, else the edge column.
                period = 2 if ch in "░▒▓" else 1
                for x in range(width, width + pad):
                    if row >> (width + pad - 1 - (x - period)) & 1:
                        row |= 1 << (width + pad - 1 - x)
            wide.append(row)
        out[ch] = wide
    return out


def lsb_left(row, width):
    """Mirror a width-bit row so the leftmost pixel is bit 0."""
    return int(f"{row:0{width}b}"[::-1], 2)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scale-y", type=int, default=1)
    ap.add_argument("--pad-x", type=int, default=0)
    ap.add_argument("--flash", action="store_true")
    ap.add_argument("source")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("name")
    ap.add_argument("out")
    args = ap.parse_args()
    if not 1 <= args.width <= 16 or args.height % args.scale_y or not 0 <= args.pad_x < args.width:
        sys.exit("width must be 1-16 and height a multiple of --scale-y, --pad-x less than width")

    src_height = args.height // args.scale_y
    load = load_bdf if args.source.endswith(".bdf") else load_psf
    chars = load(args.source, args.width - args.pad_x, src_height)
    if args.pad_x:
        chars = pad_right(chars, args.width - args.pad_x, args.pad_x)
    chars = {ch: [r for r in rows for _ in range(args.scale_y)] for ch, rows in chars.items()}
    table = build(chars, args.width, args.height)

    src = args.source.split("/")[-1]
    ctype, digits = ("uint8_t", 2) if args.width <= 8 else ("uint16_t", 4)
    lines = [
        f"// Generated by src/render/font_gen.py from {src} -- do not edit.",
        '#include "font.h"',
        "",
        f"const {ctype} {args.name}[FONT_GLYPHS * {args.height}]{' FONT_FLASH_DATA' if args.flash else ''} = {{",
    ]
    for g in range(N_GLYPHS):
        values = [lsb_left(table[g][row], args.width) for row in range(args.height)]
        lines.append(f"\t/* 0x{g:02x} */ " + " ".join(f"0x{v:0{digits}x}," for v in values))
    lines.append("};")
    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
