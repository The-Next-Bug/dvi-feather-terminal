#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// 8x16 font generated at build time by src/font_gen.py (Terminus Font).
//
// Glyph codes, as stored in the screen buffer:
//   0x00        blank
//   0x01-0x1F   DEC Special Graphics for 0x60-0x7E (◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·)
//   0x20-0x7E   ASCII
//   0x7F        blank
//   0x80-0x9F   shades, blocks and double-line box drawing (░▒▓█▀▄▌▐■═║╔╗╚╝╠╣╦╩╬╒╓╕╖╘╙╛╜╞╟╡╢)
//   0xA0-0xFF   Latin-1
//
// Layout is scanline-major: font_8x16[row * FONT_GLYPHS + glyph], one byte
// per glyph row, least-significant bit = leftmost pixel.

#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define FONT_GLYPHS 256

#define GLYPH_DEC_GRAPHICS(c) ((uint8_t)((c) - 0x5F)) // c in 0x60-0x7E
#define GLYPH_EXTRAS 0x80

extern const uint8_t font_8x16[FONT_HEIGHT * FONT_GLYPHS];

#endif
