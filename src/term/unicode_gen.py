#!/usr/bin/env python3
"""Write src/term/unicode_table.c: what UTF-8 text (bd cvc) shows as.

Usage: unicode_gen.py OUT.c

Run by hand when the glyph set (src/render/font_gen.py's DEC_GRAPHICS and
EXTRAS) or the mappings below change; the output is checked in, so a build
does not depend on the host Python's Unicode version (recorded in the file).

Three tables, all for code points from U+0100 (Latin-1 is the glyph codes
themselves, src/screen/font.h):
- unicode_map: a code point -> one of the 256 glyphs: the glyph set's own
  (DEC Special Graphics, shades, blocks, double lines), box drawing reduced
  to the lines we have (heavy as light, rounded as square corners, dashed
  as solid), blocks to the nearest block, typographic punctuation to its
  ASCII look-alike, letters with accents to their Latin-1 or ASCII base.
- unicode_zero: zero-width code points (combining marks, format
  characters): dropped.
- unicode_wide: East Asian Wide and Fullwidth: two cells.
Anything else shows the replacement glyph.
"""

import os
import sys
import unicodedata

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "render"))
from font_gen import DEC_GRAPHICS, EXTRAS  # noqa: E402

GLYPHS = {}  # character -> glyph code
for i, ch in enumerate(DEC_GRAPHICS):
    GLYPHS.setdefault(ch, 0x01 + i)
for i, ch in enumerate(EXTRAS):
    GLYPHS.setdefault(ch, 0x80 + i)
for code in range(0x20, 0x7F):
    GLYPHS[chr(code)] = code
for code in range(0xA0, 0x100):
    GLYPHS[chr(code)] = code

# Box drawing: arms (up, down, left, right), 1 single, 2 double.
LIGHT = {"─": (0, 0, 1, 1), "│": (1, 1, 0, 0), "┌": (0, 1, 0, 1), "┐": (0, 1, 1, 0),
         "└": (1, 0, 0, 1), "┘": (1, 0, 1, 0), "├": (1, 1, 0, 1), "┤": (1, 1, 1, 0),
         "┬": (0, 1, 1, 1), "┴": (1, 0, 1, 1), "┼": (1, 1, 1, 1)}
DOUBLE = {"═": (0, 0, 2, 2), "║": (2, 2, 0, 0), "╔": (0, 2, 0, 2), "╗": (0, 2, 2, 0),
          "╚": (2, 0, 0, 2), "╝": (2, 0, 2, 0), "╠": (2, 2, 0, 2), "╣": (2, 2, 2, 0),
          "╦": (0, 2, 2, 2), "╩": (2, 0, 2, 2), "╬": (2, 2, 2, 2), "╒": (0, 1, 0, 2),
          "╓": (0, 2, 0, 1), "╕": (0, 1, 2, 0), "╖": (0, 2, 1, 0), "╘": (1, 0, 0, 2),
          "╙": (2, 0, 0, 1), "╛": (1, 0, 2, 0), "╜": (2, 0, 1, 0), "╞": (1, 1, 0, 2),
          "╟": (2, 2, 0, 1), "╡": (1, 1, 2, 0), "╢": (2, 2, 1, 0)}
BY_ARMS = {arms: ch for table in (LIGHT, DOUBLE) for ch, arms in table.items()}
WEIGHTS = {"LIGHT": 1, "SINGLE": 1, "HEAVY": 1, "DOUBLE": 2}
DIRS = {"UP": (0,), "DOWN": (1,), "LEFT": (2,), "RIGHT": (3,), "HORIZONTAL": (2, 3), "VERTICAL": (0, 1)}


def box_arms(name):
    """A BOX DRAWINGS character's arms from its name, or None."""
    words = name.removeprefix("BOX DRAWINGS ").split()
    words = [w for w in words if w not in ("ARC", "DASH", "DOUBLE DASH") and not w.endswith("DASH")]
    text = " ".join(words).replace("DOUBLE DASH", "")
    arms = [0, 0, 0, 0]
    weight = 1
    for part in text.split(" AND "):
        part_words = part.split()
        for w in part_words:
            if w in WEIGHTS:
                weight = WEIGHTS[w]
        for w in part_words:
            if w in DIRS:
                for d in DIRS[w]:
                    arms[d] = weight
            elif w not in WEIGHTS and w not in ("TRIPLE", "QUADRUPLE"):
                return None
    return tuple(arms) if any(arms) else None


def box_glyph(ch):
    name = unicodedata.name(ch, "")
    if "DIAGONAL" in name:
        return {"╱": "/", "╲": "\\", "╳": "X"}[ch]
    arms = box_arms(name)
    if arms is None:
        sys.exit(f"box drawing {ch} U+{ord(ch):04X} {name}: not understood")
    if arms in BY_ARMS:
        return BY_ARMS[arms]
    light = tuple(min(a, 1) for a in arms)
    if light in BY_ARMS:
        return BY_ARMS[light]
    vertical = light[0] or light[1]
    return "│" if vertical and not (light[2] or light[3]) else "─"  # a half line


BLOCKS = {
    "▁": "▄", "▂": "▄", "▃": "▄", "▅": "█", "▆": "█", "▇": "█", "▉": "█", "▊": "█", "▋": "▌",
    "▍": "▌", "▎": "▌", "▏": "▌", "▔": "▀", "▕": "▐", "▖": "▄", "▗": "▄", "▘": "▀", "▝": "▀",
    "▙": "█", "▛": "█", "▜": "█", "▟": "█", "▚": "▒", "▞": "▒",
}

# Look-alikes for text: punctuation, symbols, arrows.
LOOKALIKES = {
    "‐": "-", "‑": "-", "‒": "-", "–": "-", "—": "-", "―": "-", "−": "-", "⁃": "-",
    "‘": "'", "’": "'", "‚": ",", "‛": "'", "′": "'", "‹": "<", "›": ">",
    "“": '"', "”": '"', "„": '"', "‟": '"', "″": '"',
    "•": "·", "‧": "·", "∙": "·", "⋅": "·", "…": ".", "‥": ".",
    "†": "+", "‡": "+", "⁄": "/", "∕": "/", "∖": "\\", "∗": "*", "∣": "|", "∼": "~", "≈": "~",
    "←": "<", "→": ">", "↑": "^", "↓": "v", "⇐": "<", "⇒": ">",
    "♦": "◆", "◇": "◆", "▪": "■", "□": "■", "▫": "■", "◼": "■", "◾": "■",
    "●": "o", "○": "o", "◦": "o",
    " ": " ", " ": " ", " ": " ", " ": " ", " ": " ", " ": " ",
    " ": " ", " ": " ", " ": " ", " ": " ", " ": " ", " ": " ",
    " ": " ", "　": " ",
    "€": "E", "™": "T", "№": "N",
    # Letters without a decomposition.
    "Ł": "L", "ł": "l", "Đ": "Ð", "đ": "d", "Ħ": "H", "ħ": "h", "ı": "i", "Ŧ": "T", "ŧ": "t",
    "ƒ": "f", "Œ": "O", "œ": "o", "ĸ": "k", "ŀ": "l", "Ŀ": "L",
}


def mapping():
    out = {}
    for ch, code in GLYPHS.items():
        if ord(ch) >= 0x100:
            out[ord(ch)] = code
    for cp in range(0x2500, 0x2580):
        out.setdefault(cp, GLYPHS[box_glyph(chr(cp))])
    for ch, to in list(BLOCKS.items()) + list(LOOKALIKES.items()):
        out.setdefault(ord(ch), GLYPHS[to])
    # Letters with marks: their base, kept with its first mark when Latin-1
    # has that (ǘ -> ü), else the base alone (ő -> o).
    for cp in range(0x100, 0x2000):
        ch = chr(cp)
        if cp in out or not unicodedata.category(ch).startswith("L"):
            continue
        nfd = unicodedata.normalize("NFD", ch)
        if len(nfd) < 2 or ord(nfd[0]) >= 0x100:
            continue
        two = unicodedata.normalize("NFC", nfd[:2])
        base = two if len(two) == 1 and ord(two) < 0x100 else nfd[0]
        if base in GLYPHS:
            out[cp] = GLYPHS[base]
    return out


def ranges(pred, start=0x300, end=0x110000):
    out, first = [], None
    for cp in range(start, end + 1):
        if cp < end and pred(cp):
            if first is None:
                first = cp
        elif first is not None:
            out.append((first, cp - 1))
            first = None
    return out


def zero_width(cp):
    if 0xFF01 <= cp <= 0xFF5E:
        return False
    if 0x1160 <= cp <= 0x11FF or 0xD7B0 <= cp <= 0xD7FF:  # Hangul medial and final jamo
        return True
    cat = unicodedata.category(chr(cp))
    return cat in ("Mn", "Me") or (cat == "Cf" and cp != 0x00AD)


def wide(cp):
    return unicodedata.east_asian_width(chr(cp)) in ("W", "F") and not zero_width(cp)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.split("\n\n")[1])
    table = mapping()
    zero = ranges(zero_width)
    wides = ranges(wide)
    cps = sorted(table)
    assert all(cp <= 0xFFFF for cp in cps)
    lines = [
        "// Generated by src/term/unicode_gen.py -- do not edit.",
        f"// Unicode {unicodedata.unidata_version} (Python's unicodedata); the glyph set of src/render/font_gen.py.",
        "",
        '#include "unicode.h"',
        "",
        "const uint16_t unicode_map_cps[] = {",
    ]

    def rows(items, per):
        for i in range(0, len(items), per):
            lines.append("\t" + " ".join(items[i:i + per]))

    rows([f"0x{cp:04x}," for cp in cps], 10)
    lines += ["};", "", "const uint8_t unicode_map_glyphs[] = {"]
    rows([f"0x{table[cp]:02x}," for cp in cps], 12)
    lines += ["};", "", "const unicode_range_t unicode_zero[] = {"]
    rows([f"{{0x{a:x}, 0x{b:x}}}," for a, b in zero], 5)
    lines += ["};", "", "const unicode_range_t unicode_wide[] = {"]
    rows([f"{{0x{a:x}, 0x{b:x}}}," for a, b in wides], 5)
    lines += ["};", "",
              f"const unsigned unicode_map_size = {len(cps)};",
              f"const unsigned unicode_zero_size = {len(zero)};",
              f"const unsigned unicode_wide_size = {len(wides)};", ""]
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(lines))
    print(f"{sys.argv[1]}: {len(cps)} mapped, {len(zero)} zero-width ranges, {len(wides)} wide ranges")


if __name__ == "__main__":
    main()
