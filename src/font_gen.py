#!/usr/bin/env python3
"""Generate the firmware's 8x16 font table from a PSF console font.

Usage: font_gen.py FONT.psf[.gz] OUT.c

The table has 256 glyphs, indexed by the glyph codes the screen buffer stores
(see src/font.h for the layout). It is stored scanline-major, as the PicoDVI
tmds_encode_font_2bpp encoder wants: font[row * 256 + glyph], one byte per
glyph row, least-significant bit = leftmost pixel.

Glyphs are looked up by Unicode code point through the PSF's Unicode table.
The few DEC Special Graphics characters that console fonts lack (control
pictures and scan lines) are drawn here.
"""

import gzip
import struct
import sys

WIDTH, HEIGHT, N_GLYPHS = 8, 16, 256

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


def load_psf(path):
    """Return ({char: glyph rows as ints, MSB = leftmost}, height)."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as f:
        data = f.read()

    if data[:4] == b"\x72\xb5\x4a\x86":  # PSF2
        _, _, hdr, flags, n, size, height, width = struct.unpack("<8I", data[:32])
        if width != WIDTH:
            sys.exit(f"{path}: glyphs are {width} wide, need {WIDTH}")
        glyphs = [data[hdr + i * size:hdr + (i + 1) * size] for i in range(n)]
        table = data[hdr + n * size:] if flags & 1 else b""
        entries = []
        for chunk in table.split(b"\xff")[:n]:
            # Characters before the first 0xFE (sequence start) map to the glyph.
            entries.append(chunk.split(b"\xfe")[0].decode("utf-8"))
    elif data[:2] == b"\x36\x04":  # PSF1
        mode, height = data[2], data[3]
        n = 512 if mode & 1 else 256
        glyphs = [data[4 + i * height:4 + (i + 1) * height] for i in range(n)]
        entries = [""] * n
        if mode & 2:
            words = struct.unpack(f"<{(len(data) - 4 - n * height) // 2}H", data[4 + n * height:])
            g, in_seq = 0, False
            for w in words:
                if w == 0xFFFF:
                    g, in_seq = g + 1, False
                elif w == 0xFFFE:
                    in_seq = True
                elif not in_seq and g < n:
                    entries[g] += chr(w)
    else:
        sys.exit(f"{path}: not a PSF font")

    if height != HEIGHT:
        sys.exit(f"{path}: glyphs are {height} high, need {HEIGHT}")
    if not any(entries):
        sys.exit(f"{path}: font has no Unicode table")
    chars = {}
    for glyph, codepoints in zip(glyphs, entries):
        for ch in codepoints:
            chars.setdefault(ch, list(glyph))
    return chars


def hline(row):
    return [0xFF if r == row else 0 for r in range(HEIGHT)]


def control_picture(pair):
    rows = [0] * HEIGHT
    for (top, left), letter in zip(((2, 0), (9, 4)), pair):
        for dy, bits in enumerate(MINI[letter]):
            rows[top + dy] |= int(bits, 2) << (5 - left)
    return rows


def build(chars):
    blank = [0] * HEIGHT

    def glyph(ch):
        return chars.get(ch) or chars.get(FALLBACK.get(ch, ""))

    # Scan lines 1/3/5/7/9 sit around the font's own horizontal line (scan 5).
    mid = next(r for r, bits in enumerate(glyph("─")) if bits)
    scan = {"⎺": 1, "⎻": (1 + mid) // 2, "⎼": (mid + HEIGHT - 2) // 2, "⎽": HEIGHT - 2}

    half = HEIGHT // 2
    shade = glyph("░")
    drawn = {
        "▀": [0xFF] * half + [0] * half,
        "▄": [0] * half + [0xFF] * half,
        "▌": [0xF0] * HEIGHT,
        "▐": [0x0F] * HEIGHT,
        "▓": shade and [b ^ 0xFF for b in shade],
    }

    table = [blank] * N_GLYPHS
    missing = []

    def place(code, ch):
        g = glyph(ch) or drawn.get(ch)
        if g is None and ch in scan:
            g = hline(scan[ch])
        if g is None and ch in CONTROL_PICTURES:
            g = control_picture(CONTROL_PICTURES[ch])
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


def reverse_bits(b):
    return int(f"{b:08b}"[::-1], 2)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1:]
    table = build(load_psf(src))
    lines = [
        f"// Generated by src/font_gen.py from {src.split('/')[-1]} -- do not edit.",
        "// Derived from Terminus Font; see assets/fonts/Terminus-OFL.txt.",
        '#include "pico.h"',
        '#include "font.h"',
        "",
        "const uint8_t __not_in_flash(\"font\") font_8x16[FONT_HEIGHT * FONT_GLYPHS] = {",
    ]
    for row in range(HEIGHT):
        lines.append(f"\t// row {row}")
        values = [reverse_bits(table[g][row]) for g in range(N_GLYPHS)]
        for i in range(0, N_GLYPHS, 16):
            lines.append("\t" + " ".join(f"0x{v:02x}," for v in values[i:i + 16]))
    lines.append("};")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
