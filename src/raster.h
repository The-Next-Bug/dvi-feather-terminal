#ifndef RASTER_H
#define RASTER_H

// Rasterise screen lines into the pixel format the DVI renderer encodes.
// Portable C (no pico-sdk), host-tested.
//
// The display is 800x600. 80 columns use 8-pixel cells and the 8x16 font
// (640 px, centred); 132 columns use 6-pixel cells and the 6x13 font
// (792 px). Every text row is 24 scanlines, so 25 rows (24 + status) fill
// the screen: the glyph is centred with blank padding above and below.
//
// Line-drawing, block and shade glyphs (glyph_is_connector) extend their
// top and bottom rows into that padding so vertical lines join up. Each
// line therefore stores font-height + 2 scanlines: a top padding row, the
// glyph rows and a bottom padding row; raster_stored_row maps a scanline to
// the stored row that shows it.
//
// PicoDVI's tmds_encode_font_2bpp encoder takes, per colour plane, one byte
// of 8 pixels per 8-pixel group plus a 2-bit "set" level and 2-bit "clear"
// level per group. Each line therefore holds, for every stored row and plane
// (0 = blue, 1 = green, 2 = red), a row of pixel bits, and per plane one
// level pair for each group.
//
// Per group and plane, the pair is the highest and lowest level the
// overlapping cells show there (their bg, and their fg where the glyph has
// ink), and each pixel's bit picks whichever of the two is nearer its
// colour. That is exact whenever a group's cells show at most two levels per
// plane: always for 80 columns (one cell per group), and in 132 columns for
// the usual cases (one background, adjacent inked cells of one level).
//
// The encoder works in 64-pixel units, so it encodes RASTER_WIDTH (832)
// pixels per line. Only the 800 displayed pixels are stored per row; for the
// last 32 it reads 4 bytes past the row, and the TMDS buffers have slack for
// the output, which is never displayed.

#include <stdint.h>

#include "screen.h"

#define RASTER_ACTIVE_WIDTH 800
#define RASTER_ACTIVE_HEIGHT 600
#define RASTER_WIDTH 832
#define RASTER_BYTES (RASTER_ACTIVE_WIDTH / 8)
#define RASTER_GROUPS (RASTER_WIDTH / 8)
#define RASTER_COLOUR_WORDS (RASTER_GROUPS / 8) // 8 groups of 4 bits per word
#define RASTER_PLANES 3
#define RASTER_ROW_HEIGHT 24
#define RASTER_MAX_FONT_ROWS 16
#define RASTER_STORED_ROWS (RASTER_MAX_FONT_ROWS + 2)

typedef struct {
	uint8_t bits[RASTER_STORED_ROWS][RASTER_PLANES][RASTER_BYTES];
	uint32_t colour[RASTER_PLANES][RASTER_COLOUR_WORDS]; // per group: set level | clear level << 2
} raster_line_t;

static inline unsigned raster_cell_width(unsigned cols) {
	return cols > 80 ? 6 : 8;
}

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

// Render a screen line of `cols` cells (80 or 132).
void raster_render_line(raster_line_t *out, const screen_line_t *line, unsigned cols);

#endif
