# Character set

The screen holds one-byte glyph codes (`src/screen/font.h`); the
character sets and Latin-1 text map onto them.

| Codes | Glyphs |
|---|---|
| 00h | blank |
| 01h-1Fh | DEC Special Graphics for 60h-7Eh: `◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·` |
| 20h-7Eh | ASCII |
| 7Fh | blank |
| 80h-9Fh | shades, blocks and double-line box drawing, for ANSI art: `░▒▓█▀▄▌▐■═║╔╗╚╝╠╣╦╩╬╒╓╕╖╘╙╛╜╞╟╡╢` |
| A0h-FFh | Latin-1 |

With the DEC Special Graphics set selected (`ESC ( 0`, or `ESC F` in VT52
mode), 60h-7Eh print the line drawing at 01h-1Fh. The UK set (`ESC ( A`)
prints # as £. The shades and blocks at 80h-9Fh are what CP-437-style art
uses; a host sends them as the Latin-1 bytes 80h-9Fh only through programs
that know this table, since a VT100 ignores C1 bytes.

Line drawing, blocks and shades join up across rows and cells: their top
and bottom glyph rows are carried into the padding between text rows.

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
