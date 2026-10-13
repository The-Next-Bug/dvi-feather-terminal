#include "raster.h"

#include <stdbool.h>
#include <string.h>

#include "font.h"

// Each layout's fonts as generated (in flash on the board).
static const struct {
	const void *normal, *bold; // bold NULL: no bold face
	bool wide;                 // 16-bit rows (otherwise 8-bit)
	const char *name;
} fonts[RASTER_LAYOUTS] = {
	[RASTER_80] = {font_10x20, font_10x20_bold, true, "Terminus 10x20"},
	[RASTER_132] = {font_132, NULL, false, "misc-fixed 6x12"},
};

// The active layout's fonts in SRAM, one 16-bit word per glyph row
// whatever the source: [0] normal, [1] bold (unused without a bold face,
// when font_bold points at the normal one).
static uint16_t font_copy[2][FONT_GLYPHS * RASTER_MAX_FONT_ROWS];
static const uint16_t *font_normal = font_copy[0], *font_bold = font_copy[1];
static int font_layout = -1; // the layout copied, -1: none yet

static unsigned source_row(const void *table, bool wide, unsigned i) {
	return wide ? ((const uint16_t *)table)[i] : ((const uint8_t *)table)[i];
}

void raster_select_font(raster_layout_t l) {
	if ((int)l == font_layout)
		return;
	const unsigned n = FONT_GLYPHS * raster_font_rows(l);
	for (unsigned i = 0; i < n; ++i)
		font_copy[0][i] = (uint16_t)source_row(fonts[l].normal, fonts[l].wide, i);
	if (fonts[l].bold)
		for (unsigned i = 0; i < n; ++i)
			font_copy[1][i] = (uint16_t)source_row(fonts[l].bold, fonts[l].wide, i);
	font_bold = fonts[l].bold ? font_copy[1] : font_copy[0];
	font_layout = (int)l;
}

unsigned raster_glyph_row(raster_layout_t l, bool bold, uint8_t g, unsigned r) {
	const void *table = bold && fonts[l].bold ? fonts[l].bold : fonts[l].normal;
	return source_row(table, fonts[l].wide, g * raster_font_rows(l) + r);
}

bool raster_has_bold_face(raster_layout_t l) {
	return fonts[l].bold != NULL;
}

const char *raster_font_name(raster_layout_t l) {
	return fonts[l].name;
}

// Replace cell c's pixels (w of them) in a stored row with m
// (raster_cell_bits). In place, so core 1 sees the cell old or new.
static inline void put_cell_bits(uint8_t row[RASTER_ROW_BYTES], unsigned w, unsigned c, unsigned m) {
	unsigned bit = c * w, sh = bit & 7;
	unsigned mask = ((1u << w) - 1) << sh, v = m << sh;
	uint8_t *p = &row[bit >> 3];
	p[0] = (uint8_t)((p[0] & ~mask) | v);
	p[1] = (uint8_t)((p[1] & ~(mask >> 8)) | v >> 8);
}

bool raster_render_line(raster_line_t *out, const screen_line_t *line, raster_layout_t l, unsigned flags) {
	raster_select_font(l);
	const unsigned w = raster_cell_width(l), rows = raster_font_rows(l), cols = raster_cols(l);
	const unsigned cell_mask = (1u << w) - 1;
	const bool reverse = flags & RASTER_REVERSE;

	bool any_blink = false;
	for (unsigned c = 0; c < cols; ++c) {
		out->fg[c] = reverse ? line->bg[c] : line->fg[c];
		out->bg[c] = reverse ? line->fg[c] : line->bg[c];
		uint8_t g = line->glyph[c], attr = line->attr[c];
		bool blink = attr & SCREEN_ATTR_BLINK;
		any_blink |= blink;
		if (blink && (flags & RASTER_BLINK_OFF)) {
			for (unsigned sr = 0; sr < rows + 2; ++sr)
				put_cell_bits(out->bits[sr], w, c, 0);
			continue;
		}
		// Line drawing keeps its shape in bold, so it still joins. No bold
		// face (132 columns): the glyph as it is; bold shows only as the
		// brighter colour term.c gives it. At 6 pixels a smear closes the
		// gaps of m, w, # and the like.
		bool connector = glyph_is_connector(g);
		bool bold = (attr & SCREEN_ATTR_BOLD) && !connector;
		const uint16_t *glyph = (bold ? font_bold : font_normal) + g * rows;
		// The padding rows repeat a connector's edge rows, blank otherwise.
		put_cell_bits(out->bits[0], w, c, connector ? glyph[0] & cell_mask : 0);
		for (unsigned r = 0; r < rows; ++r)
			put_cell_bits(out->bits[1 + r], w, c, glyph[r] & cell_mask);
		put_cell_bits(out->bits[rows + 1], w, c, connector ? glyph[rows - 1] & cell_mask : 0);
		if (attr & SCREEN_ATTR_UNDERLINE) // the glyph's bottom row
			put_cell_bits(out->bits[rows], w, c, cell_mask);
	}
	return any_blink;
}

void raster_blank_line(raster_line_t *line, uint8_t colour) {
	// Colours first: until the bits are clear, set pixels show the colour
	// too, so the line is blank from the first store on.
	memset(line->fg, colour, sizeof line->fg);
	memset(line->bg, colour, sizeof line->bg);
	memset(line->bits, 0, sizeof line->bits);
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
