#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>

// Font tables generated at build time by src/font_gen.py:
//   font_8x16  Terminus Font 8x16, for 80 columns
//   font_8x16_bold  its bold face, for bold text in 80 columns
//   font_132   for 132 columns, by the FONT_132 build option: misc-fixed
//              6x12 or 6x10 (shown at double height), or 6x13
//
// Glyph codes, as stored in the screen buffer (the same in both fonts):
//   0x00        blank
//   0x01-0x1F   DEC Special Graphics for 0x60-0x7E (◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·)
//   0x20-0x7E   ASCII
//   0x7F        blank
//   0x80-0x9F   shades, blocks and double-line box drawing (░▒▓█▀▄▌▐■═║╔╗╚╝╠╣╦╩╬╒╓╕╖╘╙╛╜╞╟╡╢)
//   0xA0-0xFF   Latin-1
//
// Layout is glyph-major: font[glyph * height + row], one byte per glyph
// row, least-significant bit = leftmost pixel; only the low `width` bits
// are used. Glyph-major, so a glyph's rows share XIP cache lines: tables
// marked FONT_FLASH_DATA stay in flash (SRAM is short) and are read through
// XIP, by core 0 only (raster_render_line), after nvflash_retime.

#define FONT_GLYPHS 256
#define FONT_8X16_HEIGHT 16

#if FONT_FLASH
#define FONT_FLASH_DATA __attribute__((section(".flashdata.font")))
#else
#define FONT_FLASH_DATA
#endif

#define GLYPH_DEC_GRAPHICS(c) ((uint8_t)((c) - 0x5F)) // c in 0x60-0x7E

#define GLYPH_SHADE_LIGHT 0x80
#define GLYPH_SHADE_MEDIUM 0x81
#define GLYPH_SHADE_DARK 0x82
#define GLYPH_FULL_BLOCK 0x83
#define GLYPH_SQUARE 0x88
#define GLYPH_DOUBLE_H 0x89
#define GLYPH_DOUBLE_V 0x8A
#define GLYPH_DOUBLE_DR 0x8B // ╔
#define GLYPH_DOUBLE_DL 0x8C // ╗
#define GLYPH_DOUBLE_UR 0x8D // ╚
#define GLYPH_DOUBLE_UL 0x8E // ╝

extern const uint8_t font_8x16[FONT_GLYPHS * FONT_8X16_HEIGHT];
extern const uint8_t font_8x16_bold[FONT_GLYPHS * FONT_8X16_HEIGHT];

// cmake/fonts.cmake (FONT_132) defines FONT_132_HEIGHT, the font's glyph
// rows, and FONT_132_SCALE, the scanlines each is shown on.
#ifdef FONT_132_HEIGHT
extern const uint8_t font_132[FONT_GLYPHS * FONT_132_HEIGHT];
#define FONT_132_NAME \
	(FONT_132_HEIGHT == 12 ? "misc-fixed 6x12" : FONT_132_HEIGHT == 10 ? "misc-fixed 6x10" : "misc-fixed 6x13")
#endif

// Glyphs that join the cells above and below (line drawing, blocks,
// shades): the renderer extends their top and bottom rows into the blank
// space between text rows, so vertical lines have no gaps.
static inline bool glyph_is_connector(uint8_t g) {
	switch (g) {
	case GLYPH_DEC_GRAPHICS('a'):                               // ▒
	case GLYPH_DEC_GRAPHICS('j') ... GLYPH_DEC_GRAPHICS('n'):   // ┘┐┌└┼
	case GLYPH_DEC_GRAPHICS('t') ... GLYPH_DEC_GRAPHICS('x'):   // ├┤┴┬│
		return true;
	default:
		return g >= GLYPH_SHADE_LIGHT && g <= 0x9F && g != GLYPH_SQUARE;
	}
}

#endif
