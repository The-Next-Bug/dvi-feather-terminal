#include "raster.h"

#include <stdbool.h>
#include <string.h>

#include "font.h"

uint16_t raster_active_width = RASTER_WIDTH_800;

// Each width's layouts' fonts as generated (in flash on the board):
// [0] 800, [1] 1024 pixels wide.
typedef struct {
	const void *normal, *bold; // bold NULL: no bold face
	bool wide;                 // 16-bit rows (otherwise 8-bit)
	const char *name;
} font_set_t;

static const font_set_t fonts[2][RASTER_LAYOUTS] = {
	{
		[RASTER_80] = {font_10x20, font_10x20_bold, true, "Terminus 10x20"},
		[RASTER_132] = {font_132, NULL, false, "misc-fixed 6x12"},
	},
	{
		[RASTER_80] = {font_12x24, font_12x24_bold, true, "Terminus 12x24"},
		[RASTER_132] = {font_132_7, NULL, false, "misc-fixed 6x12 in 7-pixel cells"},
	},
};

static const font_set_t *font_set(raster_layout_t l) {
	return &fonts[raster_width() == RASTER_WIDTH_1024][l];
}

// The active layout's fonts in SRAM, one 16-bit word per glyph row
// whatever the source: [0] normal, [1] bold (unused without a bold face,
// when font_bold points at the normal one).
static uint16_t font_copy[2][FONT_GLYPHS * RASTER_MAX_FONT_ROWS];
static const uint16_t *font_normal = font_copy[0], *font_bold = font_copy[1];
static const font_set_t *font_copied; // the fonts copied, NULL: none yet

static unsigned source_row(const void *table, bool wide, unsigned i) {
	return wide ? ((const uint16_t *)table)[i] : ((const uint8_t *)table)[i];
}

void raster_set_width(unsigned w) {
	raster_active_width = w == RASTER_WIDTH_1024 ? RASTER_WIDTH_1024 : RASTER_WIDTH_800;
}

void raster_select_font(raster_layout_t l) {
	const font_set_t *f = font_set(l);
	if (f == font_copied) {
		return;
	}
	const unsigned n = FONT_GLYPHS * raster_font_rows(l);
	for (unsigned i = 0; i < n; ++i) {
		font_copy[0][i] = (uint16_t)source_row(f->normal, f->wide, i);
	}
	if (f->bold) {
		for (unsigned i = 0; i < n; ++i) {
			font_copy[1][i] = (uint16_t)source_row(f->bold, f->wide, i);
		}
	}
	font_bold = f->bold ? font_copy[1] : font_copy[0];
	font_copied = f;
}

unsigned raster_glyph_row(raster_layout_t l, bool bold, uint8_t g, unsigned r) {
	const font_set_t *f = font_set(l);
	const void *table = bold && f->bold ? f->bold : f->normal;
	return source_row(table, f->wide, g * raster_font_rows(l) + r);
}

bool raster_has_bold_face(raster_layout_t l) {
	return font_set(l)->bold != NULL;
}

const char *raster_font_name(raster_layout_t l) {
	return font_set(l)->name;
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
			for (unsigned sr = 0; sr < rows + 2; ++sr) {
				put_cell_bits(out->bits[sr], w, c, 0);
			}
			continue;
		}
		// Line drawing keeps its shape in bold, so it still joins. No bold
		// face (132 columns): the glyph as it is; bold shows only as the
		// brighter color term.c gives it. At 6 pixels a smear closes the
		// gaps of m, w, # and the like.
		bool connector = glyph_is_connector(g);
		bool bold = (attr & SCREEN_ATTR_BOLD) && !connector;
		const uint16_t *glyph = (bold ? font_bold : font_normal) + g * rows;
		// The padding rows repeat a connector's edge rows, blank otherwise.
		put_cell_bits(out->bits[0], w, c, connector ? glyph[0] & cell_mask : 0);
		for (unsigned r = 0; r < rows; ++r) {
			put_cell_bits(out->bits[1 + r], w, c, glyph[r] & cell_mask);
		}
		put_cell_bits(out->bits[rows + 1], w, c, connector ? glyph[rows - 1] & cell_mask : 0);
		if (attr & SCREEN_ATTR_UNDERLINE) { // the glyph's bottom row
			put_cell_bits(out->bits[rows], w, c, cell_mask);
		}
	}
	return any_blink;
}

void raster_blank_line(raster_line_t *line, uint8_t color) {
	// Colors first: until the bits are clear, set pixels show the color
	// too, so the line is blank from the first store on.
	memset(line->fg, color, sizeof line->fg);
	memset(line->bg, color, sizeof line->bg);
	memset(line->bits, 0, sizeof line->bits);
}

// Pixel-pair masks for two bits of a cell (bit 0: the pair's first pixel,
// the word's low half). Not const: core 1 must not read flash.
static uint32_t pair_masks[4] = { 0, 0x0000ffffu, 0xffff0000u, 0xffffffffu };

// Cell c's bits from the bit string row, in the low bits (the ones above
// are the next cells': each caller takes only the cell's own). One
// unaligned load; the 3 bytes past the cell stay inside raster_line_t
// (the next stored row, or fg[]).
static inline uint32_t cell_word(const uint8_t *row, unsigned bit) {
	uint32_t v;
	memcpy(&v, row + (bit >> 3), sizeof v);
	return v >> (bit & 7);
}

// One cell's pairs of pixels from its bits b.
static inline __attribute__((always_inline)) uint32_t *compose_cell(uint32_t *p, uint32_t b, unsigned pairs,
		uint32_t fg, uint32_t bg) {
	uint32_t d = fg ^ bg;
	for (unsigned k = 0; k < pairs; ++k, b >>= 2) {
		*p++ = bg ^ (d & pair_masks[b & 3]);
	}
	return p;
}

// Two cells of an odd width w from their 2w bits b (the first cell's in
// the low w): w words, the middle one a pixel of each, so its colors are
// half the first cell's, half the second's.
static inline __attribute__((always_inline)) uint32_t *compose_cell_pair(uint32_t *p, uint32_t b, unsigned w,
		uint32_t fg0, uint32_t bg0, uint32_t fg1, uint32_t bg1) {
	const unsigned mid = w / 2;
	uint32_t d0 = fg0 ^ bg0, d1 = fg1 ^ bg1;
	uint32_t bgm = (bg0 & 0xffffu) | (bg1 & 0xffff0000u), dm = (d0 & 0xffffu) | (d1 & 0xffff0000u);
	for (unsigned k = 0; k < w; ++k, b >>= 2) {
		uint32_t m = pair_masks[b & 3];
		*p++ = k < mid ? bg0 ^ (d0 & m) : k == mid ? bgm ^ (dm & m) : bg1 ^ (d1 & m);
	}
	return p;
}

// raster_compose for one layout, its width and columns constants once
// inlined (qur.22: the generic loop kept them on the stack, reloaded per
// cell, and compared each cell with the cursor's column). Odd widths go
// two cells at a time (cols is even).
static inline __attribute__((always_inline)) void compose_layout(uint32_t *px, const raster_line_t *line,
		const uint8_t *row, unsigned width, unsigned w, unsigned cols, int cursor_col, const uint32_t colors[256]) {
	const unsigned border = (width - cols * w) / 4; // words: the cells centered
	const unsigned pairs = w / 2;
	uint32_t *p = px;
	for (unsigned i = 0; i < border; ++i) {
		*p++ = 0;
	}
	if (w % 2) {
		for (unsigned c = 0; c < cols; c += 2) {
			p = compose_cell_pair(p, cell_word(row, c * w), w, colors[line->fg[c]], colors[line->bg[c]],
				colors[line->fg[c + 1]], colors[line->bg[c + 1]]);
		}
	} else {
		for (unsigned c = 0; c < cols; ++c) {
			p = compose_cell(p, cell_word(row, c * w), pairs, colors[line->fg[c]], colors[line->bg[c]]);
		}
	}
	while (p < px + width / 2) {
		*p++ = 0;
	}
	// The cursor's cell again, inverted: once per line, not a test per cell.
	if (cursor_col >= 0 && (unsigned)cursor_col < cols) {
		unsigned c = (unsigned)cursor_col;
		if (w % 2) {
			c &= ~1u; // its pair, only its own bits inverted
			uint32_t flip = ((1u << w) - 1) << ((unsigned)cursor_col % 2 * w);
			compose_cell_pair(px + border + c / 2 * w, cell_word(row, c * w) ^ flip, w, colors[line->fg[c]],
				colors[line->bg[c]], colors[line->fg[c + 1]], colors[line->bg[c + 1]]);
		} else {
			compose_cell(px + border + c * pairs, ~cell_word(row, c * w), pairs, colors[line->fg[c]],
				colors[line->bg[c]]);
		}
	}
}

void raster_compose(uint32_t *px, const raster_line_t *line, raster_layout_t l, unsigned sr, int cursor_col,
		const uint32_t colors[256]) {
	// One loop per width and layout (cell widths and counts constants).
	const uint8_t *row = line->bits[sr];
	if (raster_width() == RASTER_WIDTH_1024) {
		if (l == RASTER_132) {
			compose_layout(px, line, row, RASTER_WIDTH_1024, 7, 132, cursor_col, colors);
		} else {
			compose_layout(px, line, row, RASTER_WIDTH_1024, 12, 80, cursor_col, colors);
		}
	} else if (l == RASTER_132) {
		compose_layout(px, line, row, RASTER_WIDTH_800, 6, 132, cursor_col, colors);
	} else {
		compose_layout(px, line, row, RASTER_WIDTH_800, 10, 80, cursor_col, colors);
	}
}
