#include <string.h>

#include "demo.h"
#include "font.h"
#include "raster.h"
#include "test.h"

static screen_t s;
static raster_line_t out;

// The palette for these tests: index c as the "pixel" c, so composed pixels
// read back as palette indices (the RGB222() values below are just indices).
static uint32_t identity[256];

// The scanline last composed from `out`, and what it was composed for.
static uint32_t px[RASTER_LINE_WORDS];
static struct {
	bool valid;
	raster_layout_t layout;
	unsigned sr;
	int cursor;
} composed;

static void render(const screen_line_t *line, raster_layout_t l, unsigned flags) {
	for (unsigned c = 0; c < 256; ++c) {
		identity[c] = c | c << 16;
	}
	raster_render_line(&out, line, l, flags);
	composed.valid = false;
}

// The color (palette index) of pixel x on scanline sy of `out`, with the cursor on
// cell cursor (-1: none).
static unsigned shown_at(raster_layout_t l, unsigned sy, unsigned x, int cursor) {
	unsigned sr = raster_stored_row(l, sy);
	if (!composed.valid || composed.layout != l || composed.sr != sr || composed.cursor != cursor) {
		// Poison first: every word must be written.
		memset(px, 0xa5, sizeof px);
		raster_compose(px, &out, l, sr, cursor, identity);
		composed.valid = true;
		composed.layout = l;
		composed.sr = sr;
		composed.cursor = cursor;
	}
	return px[x / 2] >> 16 * (x & 1) & 0xffff;
}

static unsigned shown(raster_layout_t l, unsigned sy, unsigned x) {
	return shown_at(l, sy, x, -1);
}

// Whether pixel i of glyph g is set on scanline sy (0-23) of a text row:
// the glyph centered in the row, with connectors extended to the edges.
static unsigned glyph_pixel(raster_layout_t l, uint8_t g, unsigned sy, unsigned i) {
	int rows = (int)raster_font_rows(l), gy = (int)sy - (int)raster_pad_top(l);
	gy = gy < 0 ? -1 : gy / (int)raster_font_scale(l);
	if (gy < 0 || gy >= rows) {
		if (!glyph_is_connector(g)) {
			return 0;
		}
		gy = gy < 0 ? 0 : rows - 1;
	}
	return raster_glyph_row(l, false, g, (unsigned)gy) >> i & 1;
}

// The color the cells ask for at pixel x of scanline sy (attributes: bold
// only).
static unsigned wanted(const screen_line_t *line, raster_layout_t l, unsigned sy, unsigned x) {
	const unsigned cols = raster_cols(l);
	unsigned w = raster_cell_width(l), x0 = raster_x_offset(l);
	if (x < x0 || x >= x0 + cols * w) {
		return 0; // border
	}
	unsigned c = (x - x0) / w, i = (x - x0) % w;
	uint8_t g = line->glyph[c];
	unsigned lit = glyph_pixel(l, g, sy, i);
	if ((line->attr[c] & SCREEN_ATTR_BOLD) && !glyph_is_connector(g)) {
		if (raster_has_bold_face(l)) {
			int gy = (int)sy - (int)raster_pad_top(l);
			gy = gy < 0 ? -1 : gy / (int)raster_font_scale(l);
			lit = gy >= 0 && gy < (int)raster_font_rows(l) && raster_glyph_row(l, true, g, (unsigned)gy) >> i & 1;
		}
	}
	return lit ? line->fg[c] : line->bg[c];
}

// Render row `row` of s in layout l and count pixels (over all scanlines
// and pixels, border included) whose color is not the wanted one.
static unsigned mismatches(unsigned row, raster_layout_t l) {
	const screen_line_t *line = screen_line(&s, row);
	render(line, l, 0);
	unsigned bad = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
		for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
			bad += shown(l, sy, x) != wanted(line, l, sy, x);
		}
	}
	return bad;
}

static void test_layout(void) {
	CHECK_EQ(raster_layout(80), RASTER_80);
	CHECK_EQ(raster_layout(132), RASTER_132);
#if RASTER_ACTIVE_WIDTH == 1024
	CHECK_EQ(raster_cell_width(RASTER_80), 12);
	CHECK_EQ(raster_x_offset(RASTER_80), 32); // centered
	CHECK_EQ(raster_cell_width(RASTER_132), 7);
	CHECK_EQ(raster_x_offset(RASTER_132), 50);
	CHECK_EQ(raster_pad_top(RASTER_80), 0); // 24 glyph rows fill them
#else
	CHECK_EQ(raster_cell_width(RASTER_80), 10);
	CHECK_EQ(raster_x_offset(RASTER_80), 0); // the full width
	CHECK_EQ(raster_cell_width(RASTER_132), 6);
	CHECK_EQ(raster_x_offset(RASTER_132), 4); // centered
	CHECK_EQ(raster_pad_top(RASTER_80), 2); // 20 glyph rows in 24
#endif
	CHECK_EQ(raster_x_offset(RASTER_132) * 2 + 132 * raster_cell_width(RASTER_132), RASTER_ACTIVE_WIDTH);
	CHECK_EQ(raster_x_offset(RASTER_80) * 2 + 80 * raster_cell_width(RASTER_80), RASTER_ACTIVE_WIDTH);
	CHECK_EQ(SCREEN_ROWS * RASTER_ROW_HEIGHT, RASTER_ACTIVE_HEIGHT);
	CHECK_EQ(raster_pad_top(RASTER_132), 0); // 12 rows doubled fill them
	CHECK(raster_has_bold_face(RASTER_80));
	CHECK(!raster_has_bold_face(RASTER_132));
	// Scanlines map onto top padding, every glyph row in order, bottom padding.
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		unsigned rows = raster_font_rows(l), top = raster_pad_top(l), k = raster_font_scale(l);
		CHECK(rows + 2 <= RASTER_STORED_ROWS);
		// The cells fit a stored row, with the byte after the last one's
		// start (raster_cell_bits reads two, put_cell_bits writes two).
		CHECK((raster_cols(l) * raster_cell_width(l) - 1) / 8 < RASTER_ROW_BYTES);
		CHECK((raster_cols(l) - 1) * raster_cell_width(l) / 8 + 1 < RASTER_ROW_BYTES);
		CHECK(top + rows * k <= RASTER_ROW_HEIGHT); // padding may be empty (6x12)
		CHECK_EQ(raster_x_offset(l) % 2, 0); // cells start on whole words
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			unsigned want = sy < top ? 0 : sy < top + rows * k ? (sy - top) / k + 1 : rows + 1;
			CHECK_EQ(raster_stored_row(l, sy), want);
		}
	}
}

// Any fg/bg per cell is exact, in both widths: neighboring cells no longer
// share color levels.
static void test_any_colors_exact(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c) {
			screen_put(&s, c, 0, (uint8_t)(0x21 + c % 94), (uint8_t)(c * 7 % 256), (uint8_t)(c * 13 % 256));
		}
		CHECK_EQ(mismatches(0, l), 0);
		// Full blocks of three levels side by side (the old encoder's worst case).
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c) {
			screen_put(&s, c, 0, GLYPH_FULL_BLOCK, RGB222(c % 3 + 1, 0, 0), RGB222(0, 0, c % 2 + 1));
		}
		CHECK_EQ(mismatches(0, l), 0);
		// White on blue with ink in the first and last cells.
		screen_init(&s, cols, RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, 0, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, cols - 1, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		CHECK_EQ(mismatches(0, l), 0);
	}
}

static void test_glyph_bits(void) {
	const unsigned w80 = raster_cell_width(RASTER_80), w132 = raster_cell_width(RASTER_132);
	const unsigned rows80 = raster_font_rows(RASTER_80);
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, 'A', RGB222(3, 0, 0), RGB222(0, 0, 1));
	screen_put(&s, 1, 0, 'W', RGB222(3, 0, 0), RGB222(0, 0, 1));
	screen_put_attr(&s, 2, 0, 'W', RGB222(3, 0, 0), RGB222(0, 0, 1), SCREEN_ATTR_BOLD);
	// 10- or 12-pixel cells: that many bits each, packed (cell 1 starts
	// mid-byte).
	render(screen_line(&s, 0), RASTER_80, 0);
	for (unsigned gy = 0; gy < rows80; ++gy) {
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], w80, 0), raster_glyph_row(RASTER_80, false, 'A', gy));
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], w80, 1), raster_glyph_row(RASTER_80, false, 'W', gy));
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], w80, 2), raster_glyph_row(RASTER_80, true, 'W', gy));
	}
#if RASTER_ACTIVE_WIDTH == 1024
	CHECK_EQ(raster_glyph_row(RASTER_80, false, 'A', 4), font_12x24['A' * FONT_12X24_HEIGHT + 4]);
#else
	CHECK_EQ(raster_glyph_row(RASTER_80, false, 'A', 4), font_10x20['A' * FONT_10X20_HEIGHT + 4]);
#endif
	CHECK_EQ(out.fg[0], RGB222(3, 0, 0));
	CHECK_EQ(out.bg[0], RGB222(0, 0, 1));
	// Padding rows above and below the glyph are blank.
	CHECK_EQ(raster_cell_bits(out.bits[0], w80, 0), 0);
	CHECK_EQ(raster_cell_bits(out.bits[rows80 + 1], w80, 0), 0);
	// 6- or 7-pixel cells keep only their bits; the 8-bit font is widened
	// as copied.
	screen_init(&s, 132, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, GLYPH_FULL_BLOCK, RGB222(2, 2, 2), 0);
	screen_put(&s, 2, 0, 'g', RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), RASTER_132, 0);
	CHECK_EQ(raster_cell_bits(out.bits[5], w132, 0), (1u << w132) - 1);
	CHECK_EQ(raster_cell_bits(out.bits[5], w132, 1), 0);
	for (unsigned gy = 0; gy < FONT_132_HEIGHT; ++gy) {
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], w132, 2), raster_glyph_row(RASTER_132, false, 'g', gy));
	}
#if RASTER_ACTIVE_WIDTH == 1024
	// The 7th column: blank in a letter, carried on in line drawing.
	for (unsigned gy = 0; gy < FONT_132_HEIGHT; ++gy) {
		CHECK_EQ(raster_glyph_row(RASTER_132, false, 'g', gy) >> 6, 0);
		CHECK_EQ(raster_glyph_row(RASTER_132, false, 'g', gy), font_132['g' * FONT_132_HEIGHT + gy] & 0x3fu);
	}
	unsigned h_row = 0;
	for (unsigned gy = 0; gy < FONT_132_HEIGHT; ++gy) {
		h_row |= raster_glyph_row(RASTER_132, false, GLYPH_DEC_GRAPHICS('q'), gy); // ─
	}
	CHECK_EQ(h_row, 0x7fu);
#endif
}

// A line rendered over another (any layout, any attributes) comes out as
// if rendered into a cleared slot: cells are replaced in place (qur.20),
// nothing of the old line is left.
static raster_line_t fresh;

static void fill_busy(unsigned cols, unsigned seed) {
	static const uint8_t attrs[] = {0, SCREEN_ATTR_BOLD, SCREEN_ATTR_UNDERLINE, SCREEN_ATTR_BLINK,
		SCREEN_ATTR_BOLD | SCREEN_ATTR_UNDERLINE};
	screen_init(&s, cols, RGB222(2, 2, 2), 0);
	for (unsigned c = 0; c < cols; ++c) {
		unsigned k = c * 7 + seed * 13;
		uint8_t g = k % 5 == 0 ? GLYPH_FULL_BLOCK : k % 5 == 1 ? GLYPH_DEC_GRAPHICS('x') : (uint8_t)(0x21 + k % 200);
		screen_put_attr(&s, c, 0, g, (uint8_t)k, (uint8_t)(k >> 3), attrs[k % sizeof attrs]);
	}
}

static void test_render_over(void) {
	for (raster_layout_t from = 0; from < RASTER_LAYOUTS; ++from) {
		for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
			for (unsigned flags = 0; flags < 4; ++flags) {
				fill_busy(raster_cols(l), 2);
				memset(&fresh, 0, sizeof fresh);
				raster_render_line(&fresh, screen_line(&s, 0), l, flags);
				// The old line: another layout's or other text, then blanks.
				fill_busy(raster_cols(from), 1);
				raster_render_line(&out, screen_line(&s, 0), from, flags ^ RASTER_BLINK_OFF);
				fill_busy(raster_cols(l), 2);
				raster_render_line(&out, screen_line(&s, 0), l, flags);
				unsigned w = raster_cell_width(l), bad = 0;
				for (unsigned sr = 0; sr < raster_font_rows(l) + 2; ++sr) {
					for (unsigned c = 0; c < raster_cols(l); ++c) {
						bad += raster_cell_bits(out.bits[sr], w, c) != raster_cell_bits(fresh.bits[sr], w, c);
					}
				}
				CHECK_EQ(bad, 0);
				CHECK(memcmp(out.fg, fresh.fg, raster_cols(l)) == 0);
				CHECK(memcmp(out.bg, fresh.bg, raster_cols(l)) == 0);
			}
		}
	}
	// A cell's neighbors keep their pixels when it changes.
	screen_init(&s, 132, RGB222(2, 2, 2), 0);
	for (unsigned c = 0; c < 132; ++c) {
		screen_put(&s, c, 0, GLYPH_FULL_BLOCK, RGB222(2, 2, 2), 0);
	}
	render(screen_line(&s, 0), RASTER_132, 0);
	screen_put(&s, 5, 0, ' ', RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), RASTER_132, 0);
	const unsigned w132 = raster_cell_width(RASTER_132);
	for (unsigned c = 0; c < 132; ++c) {
		CHECK_EQ(raster_cell_bits(out.bits[3], w132, c), c == 5 ? 0 : (1u << w132) - 1);
	}
}

static void test_border_is_black(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		screen_init(&s, cols, RGB222(3, 3, 3), RGB222(3, 3, 3));
		render(screen_line(&s, 0), l, 0);
		const unsigned x0 = raster_x_offset(l), x_end = x0 + cols * raster_cell_width(l);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
				CHECK_EQ(shown(l, sy, x), x >= x0 && x < x_end ? RGB222(3, 3, 3) : 0);
			}
		}
	}
}

// Vertical line-drawing glyphs must be continuous across the whole row
// height, so stacked rows join; ordinary glyphs with ink in their edge rows
// (accents, descenders) must not leak into the padding.
static void test_connectors_join(void) {
	static const uint8_t verticals[] = {
		GLYPH_DEC_GRAPHICS('x'), GLYPH_DEC_GRAPHICS('n'), GLYPH_DOUBLE_V, GLYPH_FULL_BLOCK,
	};
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		unsigned x0 = raster_x_offset(l), w = raster_cell_width(l);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		for (unsigned i = 0; i < sizeof verticals; ++i) {
			screen_put(&s, i, 0, verticals[i], RGB222(2, 2, 2), 0);
		}
		render(screen_line(&s, 0), l, 0);
		for (unsigned i = 0; i < sizeof verticals; ++i) {
			// Some column of the glyph is lit on every scanline.
			unsigned always = (1u << w) - 1;
			for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
				unsigned lit = 0;
				for (unsigned b = 0; b < w; ++b) {
					lit |= (shown(l, sy, x0 + i * w + b) != 0) << b;
				}
				always &= lit;
			}
			CHECK(always != 0);
		}

		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_puts(&s, 0, 0, "\xc5\xc9gjpqy|", RGB222(2, 2, 2), 0); // Å É descenders and a bar
		render(screen_line(&s, 0), l, 0);
		unsigned top = raster_pad_top(l), height = raster_glyph_height(l);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			if (sy >= top && sy < top + height) {
				continue;
			}
			for (unsigned x = x0; x < x0 + 8 * w; ++x) {
				CHECK_EQ(shown(l, sy, x), 0);
			}
		}
	}
}

// Double-line box drawing is double in every font (qur.11: the Terminus
// console fonts map it to the single lines, so font_gen.py draws it).
static void test_double_lines(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		unsigned rows = raster_font_rows(l), h_lines = 0, v_lines = 0;
		for (unsigned r = 0; r < rows; ++r) {
			h_lines += raster_glyph_row(l, false, GLYPH_DOUBLE_H, r) != 0;
			unsigned v = raster_glyph_row(l, false, GLYPH_DOUBLE_V, r);
			v_lines += (unsigned)__builtin_popcount(v);
			CHECK_EQ(__builtin_popcount(v), 2); // two strokes all the way down
		}
		CHECK_EQ(h_lines, 2);
		CHECK_EQ(v_lines, 2 * rows);
	}
}

// The cursor cell is shown inverted on every scanline of the row: a blank
// cell becomes a block of the fg, a glyph's pixels swap fg and bg. Other
// cells are untouched.
static void test_cursor_inverts_cell(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		unsigned x0 = raster_x_offset(l), w = raster_cell_width(l);
		const uint8_t fg = RGB222(2, 2, 2), bg = RGB222(0, 0, 1);
		screen_init(&s, cols, fg, bg);
		screen_put(&s, 6, 0, 'H', fg, bg);
		const screen_line_t *line = screen_line(&s, 0);
		render(line, l, 0);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			for (unsigned i = 0; i < w; ++i) {
				unsigned x5 = x0 + 5 * w + i, x6 = x0 + 6 * w + i;
				unsigned inv = wanted(line, l, sy, x6) == fg ? bg : fg;
				CHECK_EQ(shown_at(l, sy, x5, 5), fg); // blank cell: all fg
				CHECK_EQ(shown_at(l, sy, x6, 5), wanted(line, l, sy, x6));
				CHECK_EQ(shown_at(l, sy, x6, 6), inv); // glyph: swapped
				CHECK_EQ(shown_at(l, sy, x5, 6), bg);
			}
			if (x0) { // the full width has no border
				CHECK_EQ(shown_at(l, sy, x0 - 1, 0), 0); // border unaffected
			}
		}
	}
}

// Underline: the glyph's bottom row lit across the whole cell in the fg, so
// underlined runs join up.
static void test_underline(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		unsigned x0 = raster_x_offset(l), w = raster_cell_width(l);
		// The bottom glyph row covers raster_font_scale scanlines.
		unsigned k = raster_font_scale(l), bottom = raster_pad_top(l) + raster_glyph_height(l) - 1;
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		for (unsigned c = 0; c < 3; ++c) {
			screen_put_attr(&s, c, 0, ' ', RGB222(0, 3, 0), 0, SCREEN_ATTR_UNDERLINE);
		}
		render(screen_line(&s, 0), l, 0);
		for (unsigned x = x0; x < x0 + 3 * w; ++x) {
			for (unsigned i = 0; i < k; ++i) {
				CHECK_EQ(shown(l, bottom - i, x), RGB222(0, 3, 0));
			}
			CHECK_EQ(shown(l, bottom - k, x), 0);
			if (bottom + 1 < RASTER_ROW_HEIGHT) {
				CHECK_EQ(shown(l, bottom + 1, x), 0);
			}
		}
		CHECK_EQ(shown(l, bottom, x0 + 3 * w), 0);
	}
}

// Blink: glyph (and underline) hidden in the off phase; the renderer is
// told the line needs re-rendering when the phase changes.
static void test_blink(void) {
	const unsigned x0 = raster_x_offset(RASTER_80), w = raster_cell_width(RASTER_80);
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "ab", RGB222(2, 2, 2), 0);
	CHECK(!raster_render_line(&out, screen_line(&s, 0), RASTER_80, RASTER_BLINK_OFF));
	screen_put_attr(&s, 1, 0, 'H', RGB222(3, 0, 0), 0, SCREEN_ATTR_BLINK | SCREEN_ATTR_UNDERLINE);
	CHECK(raster_render_line(&out, screen_line(&s, 0), RASTER_80, 0));
	render(screen_line(&s, 0), RASTER_80, 0);
	unsigned lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
		for (unsigned x = x0 + w; x < x0 + 2 * w; ++x) {
			lit += shown(RASTER_80, sy, x) != 0;
		}
	}
	CHECK(lit > 0);
	render(screen_line(&s, 0), RASTER_80, RASTER_BLINK_OFF);
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
		for (unsigned x = x0 + w; x < x0 + 2 * w; ++x) {
			CHECK_EQ(shown(RASTER_80, sy, x), 0);
		}
	}
	// The non-blinking neighbor is unaffected.
	lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
		for (unsigned x = x0; x < x0 + w; ++x) {
			lit += shown(RASTER_80, sy, x) != 0;
		}
	}
	CHECK(lit > 0);
}

// Bold: in 80 columns Terminus's bold faces; in 132 (no bold face) the
// normal glyph, bold being only the brighter color. Line-drawing glyphs keep their shape so they
// still join.
static void test_bold(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		unsigned x0 = raster_x_offset(l), w = raster_cell_width(l);
		unsigned rows = raster_font_rows(l), top = raster_pad_top(l), k = raster_font_scale(l);
		const uint8_t fg = RGB222(3, 3, 3);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_put_attr(&s, 0, 0, 'I', fg, 0, SCREEN_ATTR_BOLD);
		screen_put_attr(&s, 1, 0, GLYPH_DEC_GRAPHICS('x'), fg, 0, SCREEN_ATTR_BOLD);
		render(screen_line(&s, 0), l, 0);
		for (unsigned gy = 0; gy < rows; ++gy) {
			unsigned g = raster_glyph_row(l, false, 'I', gy);
			unsigned bold = raster_has_bold_face(l) ? raster_glyph_row(l, true, 'I', gy) : g;
			unsigned v = raster_glyph_row(l, false, GLYPH_DEC_GRAPHICS('x'), gy);
			for (unsigned i = 0; i < w; ++i) {
				CHECK_EQ(shown(l, top + gy * k, x0 + i), (bold >> i & 1) ? fg : 0);
				CHECK_EQ(shown(l, top + gy * k, x0 + w + i), (v >> i & 1) ? fg : 0);
			}
		}
	}
	// Bold 'm' (qur.9): its stems stay apart and the gap column stays clear,
	// in both widths.
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		unsigned x0 = raster_x_offset(l), w = raster_cell_width(l);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_put_attr(&s, 0, 0, 'm', RGB222(3, 3, 3), 0, SCREEN_ATTR_BOLD);
		render(screen_line(&s, 0), l, 0);
		unsigned gaps = 0;
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			CHECK_EQ(shown(l, sy, x0 + w - 1), 0);
			unsigned lit = 0;
			for (unsigned i = 0; i < w; ++i) {
				lit += shown(l, sy, x0 + i) != 0;
			}
			gaps += lit > 0 && lit < w - 1;
		}
		if (raster_has_bold_face(l)) {
			CHECK(gaps > 0); // not a solid block
		}
	}
}

// Reverse (DECSCNM): every cell's fg and bg swap.
static void test_reverse(void) {
	const unsigned x0 = raster_x_offset(RASTER_80), w = raster_cell_width(RASTER_80);
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "Hi", RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), RASTER_80, RASTER_REVERSE);
	// A blank cell shows the (old) fg everywhere; a glyph pixel shows black.
	CHECK_EQ(shown(RASTER_80, 0, x0 + 10 * w), RGB222(2, 2, 2));
	unsigned dark = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
		for (unsigned x = x0; x < x0 + w; ++x) {
			dark += shown(RASTER_80, sy, x) == 0;
		}
	}
	CHECK(dark > 0);
}

// A blanked line (a slot a scroll reuses) shows one color over every cell,
// whatever it held, with the border black.
static void test_blank_line(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		fill_busy(raster_cols(l), 3);
		render(screen_line(&s, 0), l, 0);
		raster_blank_line(&out, RGB222(0, 1, 1));
		composed.valid = false;
		const unsigned x0 = raster_x_offset(l), x_end = x0 + raster_cols(l) * raster_cell_width(l);
		unsigned bad = 0;
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
				bad += shown(l, sy, x) != (x >= x0 && x < x_end ? RGB222(0, 1, 1) : 0);
			}
		}
		CHECK_EQ(bad, 0);
		// The cursor on it: a block of the color inverted is the color too.
		CHECK_EQ(shown_at(l, 10, x0 + 1, 0), RGB222(0, 1, 1));
	}
}

// The demo screen, the xterm colors as adjacent backgrounds included, is
// pixel-exact in both widths.
static void test_demo(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		demo_draw(&s, cols, "800x600p60", raster_font_name(l), " status");
		unsigned bad = 0;
		for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
			bad += mismatches(row, l);
		}
		CHECK_EQ(bad, 0);
	}
}

int main(void) {
	RUN(test_layout);
	RUN(test_any_colors_exact);
	RUN(test_glyph_bits);
	RUN(test_render_over);
	RUN(test_blank_line);
	RUN(test_border_is_black);
	RUN(test_connectors_join);
	RUN(test_double_lines);
	RUN(test_cursor_inverts_cell);
	RUN(test_underline);
	RUN(test_blink);
	RUN(test_bold);
	RUN(test_reverse);
	RUN(test_demo);
	return tests_done();
}
