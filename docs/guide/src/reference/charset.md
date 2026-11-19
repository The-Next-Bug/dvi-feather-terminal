# Character set

The screen holds one-byte glyph codes (`src/screen/font.h`); the
character sets, Latin-1 and UTF-8 text map onto them.

| Codes | Glyphs |
|---|---|
| 00h | blank |
| 01h-1Fh | DEC Special Graphics for 60h-7Eh: `◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·` |
| 20h-7Eh | ASCII |
| 7Fh | the replacement character (U+FFFD): UTF-8 the glyphs cannot show |
| 80h-9Fh | shades, blocks and double-line box drawing, for ANSI art: `░▒▓█▀▄▌▐■═║╔╗╚╝╠╣╦╩╬╒╓╕╖╘╙╛╜╞╟╡╢` |
| A0h-FFh | Latin-1 |

With the DEC Special Graphics set selected (`ESC ( 0`, or `ESC F` in VT52
mode), 60h-7Eh print the line drawing at 01h-1Fh. The UK set (`ESC ( A`)
prints # as £. The shades and blocks at 80h-9Fh are what CP-437-style art
uses; a host sends them as the Latin-1 bytes 80h-9Fh only through programs
that know this table, since a VT100 ignores C1 bytes.

Line drawing, blocks and shades join up across rows and cells: their top
and bottom glyph rows are carried into the padding between text rows.

## UTF-8

SET-UP's **Character set** (UTF-8 by default) decides what the host's
bytes from 80h mean: UTF-8, or Latin-1 one byte a character (a VT100's
8-bit text). UTF-8 is for the VT100 in terminal mode; with the VT52 or
ADM-3A, in VT52 mode, and in CP/M mode the bytes are Latin-1 as before
(SET-UP shows the row fixed then). The host can switch too, as xterm:
`ESC % G` UTF-8, `ESC % @` Latin-1, until a reset (RIS) or a SET-UP save
puts the setting back. The host must send UTF-8 for it (on Linux a
`LANG` ending in `.UTF-8`); nothing in the VT100's replies or telnet's
terminal type tells it.

The glyphs stay the 256 above, so a character shows as the nearest of
them:

| Characters | Shown as |
|---|---|
| Latin-1 (U+00A0-00FF) | itself |
| box drawing (U+2500-257F) | the light and double lines; heavy lines as light, rounded corners as square, dashed lines as solid, half lines as whole, diagonals as `/` `\` `X` |
| blocks (U+2580-259F) | the nearest of `█▀▄▌▐░▒▓` |
| `◆ ≤ ≥ π ≠ ·` and the other DEC graphics | themselves |
| typographic punctuation | its ASCII look-alike: dashes `-`, quotes `'` `"`, `•` as `·`, `…` as `.`, arrows as `< > ^ v`, wide spaces as a space |
| letters with marks (Latin Extended) | Latin-1's letter with the same mark, else the base letter: `ǘ` as `ü`, `ő` as `o`, `č` as `c`; `ł` as `l` |
| Fullwidth forms (U+FF01-FF5E) | the ASCII character, in two cells |
| combining marks, zero-width characters | nothing (the letter before them shows) |
| East Asian Wide characters (CJK, emoji) | the replacement glyph and a blank, two cells, so the columns stay where the program expects them |
| anything else (Greek, Cyrillic, ...) | the replacement glyph |

A wide character with only the last column left goes onto the next line,
as in xterm. A malformed sequence (a stray continuation byte, one cut short
by another byte, an overlong form) shows as the replacement glyph; the byte
that cut it short then acts as usual. The tables come from
`src/term/unicode_gen.py` (`src/term/unicode_table.c`, checked in).

## The fonts

| Display | 80 columns | 132 columns |
|---|---|---|
| 1024x600 | Terminus 12x24 and its bold face, 12-pixel cells | misc-fixed 6x12 at double height, in 7-pixel cells (a blank 7th column; line drawing and blocks fill it) |
| 800x600 | Terminus 10x20 and its bold face, 10-pixel cells | misc-fixed 6x12 at double height, 6-pixel cells |

In 132 columns no bold face fits, so bold text is only brighter, as on a
VT100. Glyphs a font lacks (control pictures, scan lines, half blocks,
double-line box drawing in Terminus) are drawn by the font generator,
`src/render/font_gen.py`.

![The glyph codes in Terminus 12x24](../images/generated/charset-80.png)

![The glyph codes in misc-fixed 6x12, 132 columns](../images/generated/charset-132.png)
