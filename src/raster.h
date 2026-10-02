#ifndef RASTER_H
#define RASTER_H

// Rasterise screen lines for the HSTX renderer, and compose scanlines from
// them. Portable C (no pico-sdk), host-tested.
//
// The display is 800x600. 80 columns use 8-pixel cells and the 8x16 font
// (640 px, centred); 132 columns use 6-pixel cells and the 6x13 font
// (792 px, centred). Every text row is 24 scanlines, so 25 rows (24 +
// status) fill the screen: the glyph is centred with blank padding above
// and below.
//
// Line-drawing, block and shade glyphs (glyph_is_connector) extend their
// top and bottom rows into that padding so vertical lines join up. Each
// line therefore stores font-height + 2 rows: a top padding row, the glyph
// rows and a bottom padding row; raster_stored_row maps a scanline to the
// stored row that shows it.
//
// raster_render_line (core 0, when a line changes) applies the attributes:
// per stored row and cell, a byte of the cell's pixels (bit i = pixel i, set
// = foreground) with bold, underline and blink already drawn, and per cell
// the colours as shown (reverse video applied). raster_compose (core 1, per
// scanline) turns a stored row into pixels: any colour per cell, so every
// cell is exact in both widths.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"

#define RASTER_ACTIVE_WIDTH 800
#define RASTER_ACTIVE_HEIGHT 600
#define RASTER_ROW_HEIGHT 24
#define RASTER_MAX_FONT_ROWS 16
#define RASTER_STORED_ROWS (RASTER_MAX_FONT_ROWS + 2)
// A composed scanline: two 16-bit pixels per word, the first in the low half.
#define RASTER_LINE_WORDS (RASTER_ACTIVE_WIDTH / 2)

typedef struct {
	uint8_t bits[RASTER_STORED_ROWS][SCREEN_MAX_COLS]; // per stored row and cell
	uint8_t fg[SCREEN_MAX_COLS], bg[SCREEN_MAX_COLS];  // palette indices, as shown
} raster_line_t;

static inline unsigned raster_cell_width(unsigned cols) {
	return cols > 80 ? 6 : 8;
}

// Centred: 80 columns at 80 px, 132 at 4 px. Always even, so cells start
// on whole words of the composed scanline.
static inline unsigned raster_x_offset(unsigned cols) {
	return (RASTER_ACTIVE_WIDTH - cols * raster_cell_width(cols)) / 2;
}

static inline unsigned raster_font_rows(unsigned cols) {
	return cols > 80 ? 13 : 16;
}

static inline unsigned raster_pad_top(unsigned cols) {
	return (RASTER_ROW_HEIGHT - raster_font_rows(cols)) / 2;
}

// The stored row shown on scanline sy (0-23) of a text row.
static inline unsigned raster_stored_row(unsigned cols, unsigned sy) {
	unsigned rows = raster_font_rows(cols), top = raster_pad_top(cols);
	if (sy < top)
		return 0;
	if (sy < top + rows)
		return sy - top + 1;
	return rows + 1;
}

// Flags for raster_render_line.
#define RASTER_REVERSE 0x01   // swap every cell's fg and bg (DECSCNM)
#define RASTER_BLINK_OFF 0x02 // blink phase off: hide blinking cells' glyphs

// Render a screen line of `cols` cells (80 or 132). Underlined cells get a
// line on the glyph's bottom row; bold cells are emboldened (each glyph row
// ORed with itself shifted a pixel right, within the cell). Returns whether
// any cell blinks, i.e. whether the line needs re-rendering when the phase
// changes.
bool raster_render_line(raster_line_t *out, const screen_line_t *line, unsigned cols, unsigned flags);

// Compose stored row sr of line into RASTER_LINE_WORDS words of pixels:
// colours[c] is palette index c as a pixel in both halves of a word (the
// renderer's pixel format). The border is 0 (black). The cell at
// cursor_col (-1 for none) is shown inverted: fg and bg swap pixel by pixel.
void raster_compose(uint32_t *px, const raster_line_t *line, unsigned cols, unsigned sr, int cursor_col,
	const uint32_t colours[256]);

#endif
