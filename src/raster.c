#include "raster.h"

#include <stdbool.h>

#include "font.h"

// Row sr (0 .. rows + 1) of the stored rows for glyph g: the padding rows
// repeat a connector's edge row and are blank for everything else.
static inline unsigned stored_glyph_row(const uint8_t *glyphs, unsigned rows, unsigned sr, uint8_t g) {
	if (sr == 0)
		return glyph_is_connector(g) ? glyphs[g] : 0;
	if (sr > rows)
		return glyph_is_connector(g) ? glyphs[(rows - 1) * FONT_GLYPHS + g] : 0;
	return glyphs[(sr - 1) * FONT_GLYPHS + g];
}

bool raster_render_line(raster_line_t *out, const screen_line_t *line, unsigned cols, unsigned flags) {
	const unsigned w = raster_cell_width(cols), rows = raster_font_rows(cols);
	const uint8_t *glyphs = cols > 80 ? font_132 : font_8x16;
	const unsigned cell_mask = (1u << w) - 1;
	const bool reverse = flags & RASTER_REVERSE;

	bool any_blink = false;
	for (unsigned c = 0; c < cols; ++c) {
		out->fg[c] = reverse ? line->bg[c] : line->fg[c];
		out->bg[c] = reverse ? line->fg[c] : line->bg[c];
		uint8_t g = line->glyph[c], attr = line->attr[c];
		bool blink = attr & SCREEN_ATTR_BLINK;
		any_blink |= blink;
		bool hidden = blink && (flags & RASTER_BLINK_OFF);
		bool underline = (attr & SCREEN_ATTR_UNDERLINE) && !hidden;
		bool bold = (attr & SCREEN_ATTR_BOLD) && !glyph_is_connector(g);
		for (unsigned sr = 0; sr < rows + 2; ++sr) {
			unsigned m = hidden ? 0 : stored_glyph_row(glyphs, rows, sr, g);
			if (bold)
				m |= m << 1; // clipped to the cell below
			if (underline && sr == rows) // the glyph's bottom row
				m = cell_mask;
			out->bits[sr][c] = (uint8_t)(m & cell_mask);
		}
	}
	return any_blink;
}

void raster_compose(uint32_t *px, const raster_line_t *line, unsigned cols, unsigned sr, int cursor_col,
		const uint32_t colours[256]) {
	const unsigned pairs = raster_cell_width(cols) / 2, border = raster_x_offset(cols) / 2;
	const uint8_t *bits = line->bits[sr];
	uint32_t *p = px;
	for (unsigned i = 0; i < border; ++i)
		*p++ = 0;
	for (unsigned c = 0; c < cols; ++c) {
		unsigned b = bits[c];
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
