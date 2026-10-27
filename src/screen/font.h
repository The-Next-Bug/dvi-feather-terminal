#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <stdint.h>

// Font tables generated at build time by src/render/font_gen.py, all in flash
// (FONT_FLASH_DATA): raster.c copies the active layout's pair into SRAM
// when the layout changes, so rendering never waits on XIP.
//   font_10x20, font_10x20_bold  Terminus 10x20 and its bold face, for 80
//              columns (16-bit rows)
//   font_132   misc-fixed 6x12 for 132 columns, shown at double height
//              (no bold face fits 6 pixels)
// and for the 1024-pixel-wide display (RASTER_ACTIVE_WIDTH, bd cpx.4):
//   font_12x24, font_12x24_bold  Terminus 12x24 and its bold face, for 80
//              columns (16-bit rows)
//   font_132_7 font_132 widened to 7 pixels: a blank 7th column, which line
//              drawing, blocks and shades carry on into
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
// row (16 bits for the 10x20 and 12x24 fonts), least-significant bit = leftmost
// pixel; only the low `width` bits are used.

#define FONT_GLYPHS 256
#define FONT_10X20_HEIGHT 20
#define FONT_132_HEIGHT 12
#define FONT_132_SCALE 2 // scanlines per glyph row: 6x12 fills the 24-scanline rows
#define FONT_12X24_HEIGHT 24

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

extern const uint16_t font_10x20[FONT_GLYPHS * FONT_10X20_HEIGHT];
extern const uint16_t font_10x20_bold[FONT_GLYPHS * FONT_10X20_HEIGHT];
extern const uint8_t font_132[FONT_GLYPHS * FONT_132_HEIGHT];
extern const uint16_t font_12x24[FONT_GLYPHS * FONT_12X24_HEIGHT];
extern const uint16_t font_12x24_bold[FONT_GLYPHS * FONT_12X24_HEIGHT];
extern const uint8_t font_132_7[FONT_GLYPHS * FONT_132_HEIGHT];

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
