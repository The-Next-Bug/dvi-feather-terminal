#ifndef RASTER_H
#define RASTER_H

// Rasterize screen lines for the HSTX renderer, and compose scanlines from
// them. Portable C (no pico-sdk), host-tested.
//
// The display is 800x600, in one of two layouts (raster_layout_t): 80
// columns of 10-pixel cells and Terminus 10x20 (the full 800 px), or 132
// columns of 6-pixel cells (792 px, centered) and misc-fixed 6x12 at double
// height, each glyph row on two scanlines (no 6-pixel-wide font is
// taller). Every text row is 24 scanlines, so 25 rows (24 + status) fill
// the screen: the glyph is centered with blank padding above and below
// (none for 6x12, which fills the row).
//
// Or, built with RASTER_ACTIVE_WIDTH 1024 (the 1024x600 mode, bd cpx.4),
// 1024x600: 80 columns of 12-pixel cells and Terminus 12x24, which fills
// the rows (960 px, centered), or 132 of 7-pixel cells (924 px, centered)
// and 6x12 widened by a column (font_132_7). Cells of an odd width start
// on whole words in pairs: raster_compose draws two at a time.
//
// Line-drawing, block and shade glyphs (glyph_is_connector) extend their
// top and bottom rows into that padding so vertical lines join up. Each
// line therefore stores font-height + 2 rows: a top padding row, the glyph
// rows and a bottom padding row; raster_stored_row maps a scanline to the
// stored row that shows it. A stored row is a string of pixel bits, cell c
// from bit c * cell width, so both layouts fit the same 100 bytes.
//
// raster_render_line (core 0, when a line changes) applies the attributes:
// per stored row and cell, the cell's pixels (bit i = pixel i, set =
// foreground) with bold, underline and blink already drawn, and per cell
// the colors as shown (reverse video applied). It replaces each cell's
// bits in place, so core 1 composing the line meanwhile sees each cell old
// or new, never blank. Its fonts are copies in SRAM of the layout's flash
// tables (raster_select_font). raster_compose (core 1, per scanline) turns
// a stored row into pixels: any color per cell, so every cell is exact in
// both layouts. Core 1's helpers here use no tables (they would be const
// data, in flash).
#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "screen.h"

#ifndef RASTER_ACTIVE_WIDTH
#define RASTER_ACTIVE_WIDTH 800
#endif
#define RASTER_ACTIVE_HEIGHT 600
#define RASTER_ROW_HEIGHT 24
#if RASTER_ACTIVE_WIDTH == 800
#define RASTER_80_CELL 10  // pixels per cell, 80 columns
#define RASTER_132_CELL 6  // and 132
#define RASTER_80_FONT_ROWS FONT_10X20_HEIGHT
#elif RASTER_ACTIVE_WIDTH == 1024
#define RASTER_80_CELL 12
#define RASTER_132_CELL 7
#define RASTER_80_FONT_ROWS FONT_12X24_HEIGHT
#else
#error "RASTER_ACTIVE_WIDTH: 800 or 1024"
#endif
#define RASTER_MAX_FONT_ROWS RASTER_80_FONT_ROWS
#define RASTER_STORED_ROWS (RASTER_MAX_FONT_ROWS + 2)
// A stored row's pixel bits: the wider layout's cells (raster_cell_bits
// and put_cell_bits touch the byte after a cell's first).
#define RASTER_ROW_BYTES ((80 * RASTER_80_CELL > 132 * RASTER_132_CELL ? 80 * RASTER_80_CELL : 132 * RASTER_132_CELL) / 8)
// A composed scanline: two 16-bit pixels per word, the first in the low half.
#define RASTER_LINE_WORDS (RASTER_ACTIVE_WIDTH / 2)

typedef enum {
	RASTER_80,  // 80 columns, 10x20 in 10-pixel cells, the full width (12x24 in 12)
	RASTER_132, // 132 columns, 6x12 doubled in 6-pixel cells, centered (7)
	RASTER_LAYOUTS
} raster_layout_t;

typedef struct {
	uint8_t bits[RASTER_STORED_ROWS][RASTER_ROW_BYTES]; // per stored row: the cells' pixels
	uint8_t fg[SCREEN_MAX_COLS], bg[SCREEN_MAX_COLS];   // palette indices, as shown
} raster_line_t;

// The layout of a screen `cols` wide.
static inline raster_layout_t raster_layout(unsigned cols) {
	return cols > 80 ? RASTER_132 : RASTER_80;
}

static inline unsigned raster_cols(raster_layout_t l) {
	return l == RASTER_132 ? 132 : 80;
}

static inline unsigned raster_cell_width(raster_layout_t l) {
	return l == RASTER_132 ? RASTER_132_CELL : RASTER_80_CELL;
}

// The cells centered (800 wide: 80 columns fill it, 132 start at 4 px).
// Always even, so cells (pairs of odd-width ones) start on whole words of
// the composed scanline.
static inline unsigned raster_x_offset(raster_layout_t l) {
	return (RASTER_ACTIVE_WIDTH - raster_cols(l) * raster_cell_width(l)) / 2;
}

// Glyph rows in the font, and the scanlines each one is shown on.
static inline unsigned raster_font_rows(raster_layout_t l) {
	return l == RASTER_132 ? FONT_132_HEIGHT : RASTER_80_FONT_ROWS;
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
	if (sy < top) {
		return 0;
	}
	if (sy < top + raster_glyph_height(l)) {
		return (sy - top) / raster_font_scale(l) + 1;
	}
	return raster_font_rows(l) + 1;
}

// Cell c's pixels (w of them, up to 12) in a stored row: two bytes hold
// them wherever they start (at most 7 + 7 or 4 + 12 bits).
static inline unsigned raster_cell_bits(const uint8_t row[RASTER_ROW_BYTES], unsigned w, unsigned c) {
	unsigned bit = c * w;
	unsigned v = row[bit >> 3] | (unsigned)row[(bit >> 3) + 1] << 8;
	return v >> (bit & 7) & ((1u << w) - 1);
}

// Copy layout l's fonts (normal and bold) from flash into the SRAM tables
// raster_render_line reads; nothing if they are there already. Core 0 only,
// once the flash reads at the final clock (nvflash_retime). The render does
// it itself; calling it first keeps the copy out of a render's time.
void raster_select_font(raster_layout_t l);

// Row r of glyph g in the layout's font (bold: its bold face, if it has
// one), from the flash tables: for tests and checks, not the render.
unsigned raster_glyph_row(raster_layout_t l, bool bold, uint8_t g, unsigned r);
bool raster_has_bold_face(raster_layout_t l);

// The layout's font, for the demo screen.
const char *raster_font_name(raster_layout_t l);

// Flags for raster_render_line.
#define RASTER_REVERSE 0x01   // swap every cell's fg and bg (DECSCNM)
#define RASTER_BLINK_OFF 0x02 // blink phase off: hide blinking cells' glyphs

// Render a screen line in layout l over what `out` held (any layout).
// Underlined cells get a line on the glyph's bottom row; bold cells use the
// bold face, or without one (132 columns) keep the normal glyph: bold is
// then only the brighter color. Returns whether any cell blinks, i.e.
// whether the line needs re-rendering when the phase changes.
bool raster_render_line(raster_line_t *out, const screen_line_t *line, raster_layout_t l, unsigned flags);

// Make line a blank line all in palette index color, at once: every stored
// row's bits clear, every cell's fg and bg color. For a slot a scroll
// reuses, until raster_render_line draws it properly.
void raster_blank_line(raster_line_t *line, uint8_t color);

// Compose stored row sr of line into RASTER_LINE_WORDS words of pixels:
// colors[c] is palette index c as a pixel in both halves of a word (the
// renderer's pixel format). The border is 0 (black). The cell at
// cursor_col (-1 for none) is shown inverted: fg and bg swap pixel by pixel.
void raster_compose(uint32_t *px, const raster_line_t *line, raster_layout_t l, unsigned sr, int cursor_col,
	const uint32_t colors[256]);

#endif
