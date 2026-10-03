#ifndef RASTER_H
#define RASTER_H

// Rasterise screen lines for the HSTX renderer, and compose scanlines from
// them. Portable C (no pico-sdk), host-tested.
//
// The display is 800x600, in one of three layouts (raster_layout_t): 80
// columns of 8-pixel cells and the 8x16 font (640 px, centred); 80 columns
// of 10-pixel cells and the 10x20 font (the full 800 px, the 80 columns
// setting's full width); 132 columns of 6-pixel cells (792 px, centred)
// and the FONT_132 font: by default 6x12 at double height, each glyph row
// on two scanlines (no 6-pixel-wide font is taller), or 6x10 doubled, or
// 6x13 as is. Every text row is 24 scanlines, so 25 rows (24 + status)
// fill the screen: the glyph is centred with blank padding above and below
// (none for 6x12, which fills the row).
//
// Line-drawing, block and shade glyphs (glyph_is_connector) extend their
// top and bottom rows into that padding so vertical lines join up. Each
// line therefore stores font-height + 2 rows: a top padding row, the glyph
// rows and a bottom padding row; raster_stored_row maps a scanline to the
// stored row that shows it. A stored row is a string of pixel bits, cell c
// from bit c * cell width, so every layout fits the same 100 bytes.
//
// raster_render_line (core 0, when a line changes) applies the attributes:
// per stored row and cell, the cell's pixels (bit i = pixel i, set =
// foreground) with bold, underline and blink already drawn, and per cell
// the colours as shown (reverse video applied). raster_compose (core 1, per
// scanline) turns a stored row into pixels: any colour per cell, so every
// cell is exact in every layout. Core 1's helpers here use no tables (they
// would be const data, in flash).
#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "screen.h"

#ifndef FONT_132_HEIGHT
#error "FONT_132_HEIGHT and FONT_132_SCALE come from cmake/fonts.cmake (FONT_DEFINITIONS)"
#endif

#define RASTER_ACTIVE_WIDTH 800
#define RASTER_ACTIVE_HEIGHT 600
#define RASTER_ROW_HEIGHT 24
#define RASTER_MAX_FONT_ROWS 20
#define RASTER_STORED_ROWS (RASTER_MAX_FONT_ROWS + 2)
#define RASTER_ROW_BYTES (RASTER_ACTIVE_WIDTH / 8) // a stored row's pixel bits
// A composed scanline: two 16-bit pixels per word, the first in the low half.
#define RASTER_LINE_WORDS (RASTER_ACTIVE_WIDTH / 2)

typedef enum {
	RASTER_80,      // 80 columns, 8x16 in 8-pixel cells, centred
	RASTER_80_WIDE, // 80 columns, 10x20 in 10-pixel cells, the full width
	RASTER_132,     // 132 columns, FONT_132 in 6-pixel cells, centred
	RASTER_LAYOUTS
} raster_layout_t;

typedef struct {
	uint8_t bits[RASTER_STORED_ROWS][RASTER_ROW_BYTES]; // per stored row: the cells' pixels
	uint8_t fg[SCREEN_MAX_COLS], bg[SCREEN_MAX_COLS];   // palette indices, as shown
} raster_line_t;

// The layout of a screen `cols` wide; wide80: 80 columns at full width.
static inline raster_layout_t raster_layout(unsigned cols, bool wide80) {
	return cols > 80 ? RASTER_132 : wide80 ? RASTER_80_WIDE : RASTER_80;
}

static inline unsigned raster_cols(raster_layout_t l) {
	return l == RASTER_132 ? 132 : 80;
}

static inline unsigned raster_cell_width(raster_layout_t l) {
	return l == RASTER_132 ? 6 : l == RASTER_80_WIDE ? 10 : 8;
}

// Centred: 80 columns at 80 px (0 at full width), 132 at 4 px. Always
// even, so cells start on whole words of the composed scanline.
static inline unsigned raster_x_offset(raster_layout_t l) {
	return (RASTER_ACTIVE_WIDTH - raster_cols(l) * raster_cell_width(l)) / 2;
}

// Glyph rows in the font, and the scanlines each one is shown on.
static inline unsigned raster_font_rows(raster_layout_t l) {
	return l == RASTER_132 ? FONT_132_HEIGHT : l == RASTER_80_WIDE ? FONT_10X20_HEIGHT : FONT_8X16_HEIGHT;
}

static inline unsigned raster_font_scale(raster_layout_t l) {
	return l == RASTER_132 ? FONT_132_SCALE : 1;
}

// Scanlines the glyph covers, and the padding scanlines above it.
static inline unsigned raster_glyph_height(raster_layout_t l) {
	return raster_font_rows(l) * raster_font_scale(l);
}

static inline unsigned raster_pad_top(raster_layout_t l) {
	return (RASTER_ROW_HEIGHT - raster_glyph_height(l)) / 2;
}

// The stored row shown on scanline sy (0-23) of a text row.
static inline unsigned raster_stored_row(raster_layout_t l, unsigned sy) {
	unsigned top = raster_pad_top(l);
	if (sy < top)
		return 0;
	if (sy < top + raster_glyph_height(l))
		return (sy - top) / raster_font_scale(l) + 1;
	return raster_font_rows(l) + 1;
}

// Cell c's pixels (w of them, up to 10) in a stored row: two bytes hold
// them wherever they start.
static inline unsigned raster_cell_bits(const uint8_t row[RASTER_ROW_BYTES], unsigned w, unsigned c) {
	unsigned bit = c * w;
	unsigned v = row[bit >> 3] | (unsigned)row[(bit >> 3) + 1] << 8;
	return v >> (bit & 7) & ((1u << w) - 1);
}

// Row r of glyph g in the layout's font (bold: its bold face, if it has
// one). For core 0: the fonts may be in flash.
unsigned raster_glyph_row(raster_layout_t l, bool bold, uint8_t g, unsigned r);
bool raster_has_bold_face(raster_layout_t l);

// The layout's font, for the demo screen.
static inline const char *raster_font_name(raster_layout_t l) {
	return l == RASTER_132 ? FONT_132_NAME : l == RASTER_80_WIDE ? "Terminus 10x20" : "Terminus 8x16";
}

// Flags for raster_render_line.
#define RASTER_REVERSE 0x01   // swap every cell's fg and bg (DECSCNM)
#define RASTER_BLINK_OFF 0x02 // blink phase off: hide blinking cells' glyphs

// Render a screen line in layout l. Underlined cells get a line on the
// glyph's bottom row; bold cells use the bold face, or without one (132
// columns) keep the normal glyph: bold is then only the brighter colour. Returns whether any cell blinks, i.e.
// whether the line needs re-rendering when the phase changes.
bool raster_render_line(raster_line_t *out, const screen_line_t *line, raster_layout_t l, unsigned flags);

// Compose stored row sr of line into RASTER_LINE_WORDS words of pixels:
// colours[c] is palette index c as a pixel in both halves of a word (the
// renderer's pixel format). The border is 0 (black). The cell at
// cursor_col (-1 for none) is shown inverted: fg and bg swap pixel by pixel.
void raster_compose(uint32_t *px, const raster_line_t *line, raster_layout_t l, unsigned sr, int cursor_col,
	const uint32_t colours[256]);

#endif
