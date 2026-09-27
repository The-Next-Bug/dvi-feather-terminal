#include "raster.h"

#include <stdbool.h>
#include <string.h>

#include "font.h"

static inline unsigned level(uint8_t rgb222, int plane) {
	return rgb222 >> 2 * plane & 3u;
}

// Whether level v is drawn with a group's "set" level (hi) rather than its
// "clear" level (lo): the nearer of the two, ties going to lo.
static inline unsigned picks_hi(unsigned v, unsigned lo, unsigned hi) {
	return 2 * v > lo + hi;
}

// Scratch state, static to keep it off the (small) stack: rendering is not
// reentrant.
static uint8_t group_lo[RASTER_PLANES][RASTER_GROUPS], group_hi[RASTER_PLANES][RASTER_GROUPS];
static uint8_t seen[RASTER_GROUPS];
static uint8_t fg_set[SCREEN_MAX_COLS][RASTER_PLANES], bg_set[SCREEN_MAX_COLS][RASTER_PLANES];
// Per cell, after attributes: colours as shown, and what the glyph draws.
static uint8_t cell_fg[SCREEN_MAX_COLS], cell_bg[SCREEN_MAX_COLS];
static bool cell_hidden[SCREEN_MAX_COLS], cell_underline[SCREEN_MAX_COLS], cell_bold[SCREEN_MAX_COLS];

typedef struct {
	const uint8_t *glyphs;
	unsigned rows;
	uint8_t ink[FONT_GLYPHS]; // OR of all rows: which columns of a glyph have ink
	bool ink_done;
} font_info_t;

static font_info_t font_8x16_info = { font_8x16, FONT_8X16_HEIGHT, { 0 }, false };
static font_info_t font_6x13_info = { font_6x13, FONT_6X13_HEIGHT, { 0 }, false };

static const font_info_t *font_for(unsigned cols) {
	font_info_t *f = cols > 80 ? &font_6x13_info : &font_8x16_info;
	if (!f->ink_done) {
		for (unsigned g = 0; g < FONT_GLYPHS; ++g) {
			uint8_t m = 0;
			for (unsigned r = 0; r < f->rows; ++r)
				m |= f->glyphs[r * FONT_GLYPHS + g];
			f->ink[g] = m;
		}
		f->ink_done = true;
	}
	return f;
}

// Row sr (0 .. rows + 1) of the stored rows for glyph g: the padding rows
// repeat a connector's edge row and are blank for everything else.
static inline unsigned stored_glyph_row(const font_info_t *f, unsigned sr, uint8_t g) {
	if (sr == 0)
		return glyph_is_connector(g) ? f->glyphs[g] : 0;
	if (sr > f->rows)
		return glyph_is_connector(g) ? f->glyphs[(f->rows - 1) * FONT_GLYPHS + g] : 0;
	return f->glyphs[(sr - 1) * FONT_GLYPHS + g];
}

bool raster_render_line(raster_line_t *out, const screen_line_t *line, unsigned cols, int cursor_col,
		unsigned flags) {
	const unsigned w = raster_cell_width(cols), x0 = raster_x_offset(cols);
	const font_info_t *font = font_for(cols);
	const uint8_t *ink = font->ink;
	const unsigned cell_mask = (1u << w) - 1;

	bool any_blink = false;
	for (unsigned c = 0; c < cols; ++c) {
		bool reverse = flags & RASTER_REVERSE;
		cell_fg[c] = reverse ? line->bg[c] : line->fg[c];
		cell_bg[c] = reverse ? line->fg[c] : line->bg[c];
		bool blink = line->attr[c] & SCREEN_ATTR_BLINK;
		any_blink |= blink;
		cell_hidden[c] = blink && (flags & RASTER_BLINK_OFF);
		cell_underline[c] = (line->attr[c] & SCREEN_ATTR_UNDERLINE) && !cell_hidden[c];
		cell_bold[c] = line->attr[c] & SCREEN_ATTR_BOLD;
	}

	// Level range per plane and group, over the levels the cells overlapping
	// the group actually show there: their bg (all but a full block show
	// some; counting it regardless keeps this simple), and their fg if the
	// glyph has ink in that part of the group.
	// Groups with no cells (the border) stay black.
	memset(group_lo, 0, sizeof group_lo);
	memset(group_hi, 0, sizeof group_hi);
	memset(seen, 0, sizeof seen);
	// Groups that are partly border must also show black (level 0) there.
	const unsigned x_end = x0 + cols * w;
	seen[x0 / 8] = x0 % 8 != 0;
	seen[(x_end - 1) / 8] = x_end % 8 != 0;
	for (unsigned c = 0; c < cols; ++c) {
		unsigned x = x0 + c * w;
		for (unsigned g = x / 8; g <= (x + w - 1) / 8; ++g) {
			// This cell's pixels within group g, as a mask over the cell.
			int first = (int)(8 * g) - (int)x;
			unsigned in_group = (first >= 0 ? 0xffu << first : 0xffu >> -first) & cell_mask;
			unsigned glyph_ink = ink[line->glyph[c]];
			if (cell_bold[c])
				glyph_ink |= glyph_ink << 1;
			bool fg_shows = (!cell_hidden[c] && (glyph_ink & in_group)) ||
				cell_underline[c] || (int)c == cursor_col;
			for (int p = 0; p < RASTER_PLANES; ++p) {
				unsigned f = level(cell_fg[c], p), b = level(cell_bg[c], p);
				if (!fg_shows)
					f = b;
				unsigned min = f < b ? f : b, max = f < b ? b : f;
				if (!seen[g] || min < group_lo[p][g])
					group_lo[p][g] = (uint8_t)min;
				if (!seen[g] || max > group_hi[p][g])
					group_hi[p][g] = (uint8_t)max;
			}
			seen[g] = 1;
		}
	}
	for (int p = 0; p < RASTER_PLANES; ++p) {
		for (unsigned i = 0; i < RASTER_COLOUR_WORDS; ++i) {
			uint32_t word = 0;
			for (unsigned j = 0; j < 8; ++j) {
				unsigned g = i * 8 + j;
				word |= (uint32_t)(group_hi[p][g] | group_lo[p][g] << 2) << 4 * j;
			}
			out->colour[p][i] = word;
		}
	}

	// Per cell and plane: which of its pixels take the "set" level where the
	// glyph is set (fg_set) and where it is clear (bg_set). A cell can span
	// two groups with different level pairs, so this is per pixel.
	for (unsigned c = 0; c < cols; ++c) {
		unsigned x = x0 + c * w;
		for (int p = 0; p < RASTER_PLANES; ++p) {
			unsigned f = level(cell_fg[c], p), b = level(cell_bg[c], p);
			uint8_t fm = 0, bm = 0;
			for (unsigned i = 0; i < w; ++i) {
				unsigned g = (x + i) / 8;
				fm |= (uint8_t)(picks_hi(f, group_lo[p][g], group_hi[p][g]) << i);
				bm |= (uint8_t)(picks_hi(b, group_lo[p][g], group_hi[p][g]) << i);
			}
			fg_set[c][p] = fm;
			bg_set[c][p] = bm;
		}
	}

	for (unsigned sr = 0; sr < font->rows + 2; ++sr) {
		uint8_t (*rows)[RASTER_BYTES] = out->bits[sr];
		memset(rows, 0, sizeof out->bits[sr]);
		for (unsigned c = 0; c < cols; ++c) {
			unsigned m = cell_hidden[c] ? 0 : stored_glyph_row(font, sr, line->glyph[c]);
			if (cell_bold[c] && !glyph_is_connector(line->glyph[c]))
				m |= m << 1; // clipped to the cell by cell_mask below
			if (cell_underline[c] && sr == font->rows) // the glyph's bottom row
				m = cell_mask;
			unsigned x = x0 + c * w, byte = x / 8, shift = x % 8;
			for (int p = 0; p < RASTER_PLANES; ++p) {
				unsigned bits = ((m & fg_set[c][p]) | (~m & bg_set[c][p])) & cell_mask;
				unsigned v = bits << shift; // w + shift <= 15: at most two bytes
				rows[p][byte] |= (uint8_t)v;
				if (v >> 8)
					rows[p][byte + 1] |= (uint8_t)(v >> 8);
			}
		}
	}
	return any_blink;
}
