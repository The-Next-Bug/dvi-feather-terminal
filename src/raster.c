#include "raster.h"

#include <stdbool.h>
#include <string.h>

#include "font.h"

unsigned raster_glyph_row(raster_layout_t l, bool bold, uint8_t g, unsigned r) {
	switch (l) {
	case RASTER_80_WIDE:
		return (bold ? font_10x20_bold : font_10x20)[g * FONT_10X20_HEIGHT + r];
	case RASTER_80:
		return (bold ? font_8x16_bold : font_8x16)[g * FONT_8X16_HEIGHT + r];
	default:
		return font_132[g * FONT_132_HEIGHT + r];
	}
}

bool raster_has_bold_face(raster_layout_t l) {
	return l != RASTER_132;
}

// Row sr (0 .. rows + 1) of the stored rows for glyph g: the padding rows
// repeat a connector's edge row and are blank for everything else.
static unsigned stored_glyph_row(raster_layout_t l, bool bold, unsigned rows, unsigned sr, uint8_t g) {
	if (sr == 0)
		return glyph_is_connector(g) ? raster_glyph_row(l, bold, g, 0) : 0;
	if (sr > rows)
		return glyph_is_connector(g) ? raster_glyph_row(l, bold, g, rows - 1) : 0;
	return raster_glyph_row(l, bold, g, sr - 1);
}

// OR cell c's pixels m (w of them) into a stored row (raster_cell_bits).
static inline void put_cell_bits(uint8_t row[RASTER_ROW_BYTES], unsigned w, unsigned c, unsigned m) {
	unsigned bit = c * w, v = m << (bit & 7);
	row[bit >> 3] |= (uint8_t)v;
	row[(bit >> 3) + 1] |= (uint8_t)(v >> 8);
}

bool raster_render_line(raster_line_t *out, const screen_line_t *line, raster_layout_t l, unsigned flags) {
	const unsigned w = raster_cell_width(l), rows = raster_font_rows(l), cols = raster_cols(l);
	const bool bold_face = raster_has_bold_face(l);
	const unsigned cell_mask = (1u << w) - 1;
	const bool reverse = flags & RASTER_REVERSE;

	memset(out->bits, 0, (rows + 2) * sizeof out->bits[0]);
	bool any_blink = false;
	for (unsigned c = 0; c < cols; ++c) {
		out->fg[c] = reverse ? line->bg[c] : line->fg[c];
		out->bg[c] = reverse ? line->fg[c] : line->bg[c];
		uint8_t g = line->glyph[c], attr = line->attr[c];
		bool blink = attr & SCREEN_ATTR_BLINK;
		any_blink |= blink;
		bool hidden = blink && (flags & RASTER_BLINK_OFF);
		bool underline = (attr & SCREEN_ATTR_UNDERLINE) && !hidden;
		// Line drawing keeps its shape in bold, so it still joins.
		bool bold = (attr & SCREEN_ATTR_BOLD) && !glyph_is_connector(g);
		for (unsigned sr = 0; sr < rows + 2; ++sr) {
			// No bold face (132 columns): the glyph as it is; bold shows
			// only as the brighter colour term.c gives it. At 6 pixels a
			// smear closes the gaps of m, w, # and the like.
			unsigned m = hidden ? 0 : stored_glyph_row(l, bold && bold_face, rows, sr, g);
			if (underline && sr == rows) // the glyph's bottom row
				m = cell_mask;
			put_cell_bits(out->bits[sr], w, c, m & cell_mask);
		}
	}
	return any_blink;
}

void raster_compose(uint32_t *px, const raster_line_t *line, raster_layout_t l, unsigned sr, int cursor_col,
		const uint32_t colours[256]) {
	const unsigned w = raster_cell_width(l), cols = raster_cols(l);
	const unsigned pairs = w / 2, border = raster_x_offset(l) / 2;
	const uint8_t *row = line->bits[sr];
	uint32_t *p = px;
	for (unsigned i = 0; i < border; ++i)
		*p++ = 0;
	for (unsigned c = 0; c < cols; ++c) {
		unsigned b = raster_cell_bits(row, w, c);
		if ((int)c == cursor_col)
			b = ~b;
		uint32_t fg = colours[line->fg[c]], bg = colours[line->bg[c]];
		// Two pixels per word: pixel 2k in the low half, from bit 2k.
		for (unsigned k = 0; k < pairs; ++k, b >>= 2) {
			uint32_t m = (0u - (b & 1)) & 0xffffu;
			m |= (0u - (b >> 1 & 1)) << 16;
			*p++ = (fg & m) | (bg & ~m);
		}
	}
	while (p < px + RASTER_LINE_WORDS)
		*p++ = 0;
}
