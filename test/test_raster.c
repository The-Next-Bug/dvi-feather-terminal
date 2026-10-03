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
	for (unsigned c = 0; c < 256; ++c)
		identity[c] = c | c << 16;
	raster_render_line(&out, line, l, flags);
	composed.valid = false;
}

// The colour (palette index) of pixel x on scanline sy of `out`, with the cursor on
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
// the glyph centred in the row, with connectors extended to the edges.
static unsigned glyph_pixel(raster_layout_t l, uint8_t g, unsigned sy, unsigned i) {
	int rows = (int)raster_font_rows(l), gy = (int)sy - (int)raster_pad_top(l);
	gy = gy < 0 ? -1 : gy / (int)raster_font_scale(l);
	if (gy < 0 || gy >= rows) {
		if (!glyph_is_connector(g))
			return 0;
		gy = gy < 0 ? 0 : rows - 1;
	}
	return raster_glyph_row(l, false, g, (unsigned)gy) >> i & 1;
}

// The colour the cells ask for at pixel x of scanline sy (no attributes).
static unsigned wanted(const screen_line_t *line, raster_layout_t l, unsigned sy, unsigned x) {
	const unsigned cols = raster_cols(l);
	unsigned w = raster_cell_width(l), x0 = raster_x_offset(l);
	if (x < x0 || x >= x0 + cols * w)
		return 0; // border
	unsigned c = (x - x0) / w, i = (x - x0) % w;
	return glyph_pixel(l, line->glyph[c], sy, i) ? line->fg[c] : line->bg[c];
}

// Render row `row` of s in layout l and count pixels (over all scanlines
// and pixels, border included) whose colour is not the wanted one.
static unsigned mismatches(unsigned row, raster_layout_t l) {
	const screen_line_t *line = screen_line(&s, row);
	render(line, l, 0);
	unsigned bad = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
			bad += shown(l, sy, x) != wanted(line, l, sy, x);
	return bad;
}

static void test_layout(void) {
	CHECK_EQ(raster_layout(80, false), RASTER_80);
	CHECK_EQ(raster_layout(80, true), RASTER_80_WIDE);
	CHECK_EQ(raster_layout(132, true), RASTER_132);
	CHECK_EQ(raster_cell_width(RASTER_80), 8);
	CHECK_EQ(raster_x_offset(RASTER_80), 80);
	CHECK_EQ(raster_cell_width(RASTER_80_WIDE), 10);
	CHECK_EQ(raster_x_offset(RASTER_80_WIDE), 0); // the full width
	CHECK_EQ(raster_cell_width(RASTER_132), 6);
	CHECK_EQ(raster_x_offset(RASTER_132), 4); // centred
	CHECK_EQ(raster_x_offset(RASTER_132) * 2 + 132 * 6, RASTER_ACTIVE_WIDTH);
	CHECK_EQ(SCREEN_ROWS * RASTER_ROW_HEIGHT, RASTER_ACTIVE_HEIGHT);
	CHECK_EQ(raster_pad_top(RASTER_80_WIDE), 2); // 20 glyph rows in 24
	// Scanlines map onto top padding, every glyph row in order, bottom padding.
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		unsigned rows = raster_font_rows(l), top = raster_pad_top(l), k = raster_font_scale(l);
		CHECK(rows + 2 <= RASTER_STORED_ROWS);
		// The cells fit a stored row, with the byte after the last one's
		// start (raster_cell_bits reads two).
		CHECK((raster_cols(l) - 1) * raster_cell_width(l) / 8 + 1 < RASTER_ROW_BYTES);
		CHECK(top + rows * k <= RASTER_ROW_HEIGHT); // padding may be empty (6x12)
		CHECK_EQ(raster_x_offset(l) % 2, 0); // cells start on whole words
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			unsigned want = sy < top ? 0 : sy < top + rows * k ? (sy - top) / k + 1 : rows + 1;
			CHECK_EQ(raster_stored_row(l, sy), want);
		}
	}
}

// Any fg/bg per cell is exact, in both widths: neighbouring cells no longer
// share colour levels.
static void test_any_colours_exact(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c)
			screen_put(&s, c, 0, (uint8_t)(0x21 + c % 94), (uint8_t)(c * 7 % 256), (uint8_t)(c * 13 % 256));
		CHECK_EQ(mismatches(0, l), 0);
		// Full blocks of three levels side by side (the old encoder's worst case).
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c)
			screen_put(&s, c, 0, GLYPH_FULL_BLOCK, RGB222(c % 3 + 1, 0, 0), RGB222(0, 0, c % 2 + 1));
		CHECK_EQ(mismatches(0, l), 0);
		// White on blue with ink in the first and last cells.
		screen_init(&s, cols, RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, 0, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, cols - 1, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		CHECK_EQ(mismatches(0, l), 0);
	}
}

static void test_glyph_bits(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, 'A', RGB222(3, 0, 0), RGB222(0, 0, 1));
	screen_put(&s, 1, 0, 'W', RGB222(3, 0, 0), RGB222(0, 0, 1));
	render(screen_line(&s, 0), RASTER_80, 0);
	for (unsigned gy = 0; gy < FONT_8X16_HEIGHT; ++gy) {
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], 8, 0), font_8x16['A' * FONT_8X16_HEIGHT + gy]);
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], 8, 1), font_8x16['W' * FONT_8X16_HEIGHT + gy]);
	}
	CHECK_EQ(out.fg[0], RGB222(3, 0, 0));
	CHECK_EQ(out.bg[0], RGB222(0, 0, 1));
	// Padding rows above and below the glyph are blank.
	CHECK_EQ(raster_cell_bits(out.bits[0], 8, 0), 0);
	CHECK_EQ(raster_cell_bits(out.bits[FONT_8X16_HEIGHT + 1], 8, 0), 0);
	// 10-pixel cells: 10 bits each, packed (cell 1 starts mid-byte).
	render(screen_line(&s, 0), RASTER_80_WIDE, 0);
	for (unsigned gy = 0; gy < FONT_10X20_HEIGHT; ++gy) {
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], 10, 0), font_10x20['A' * FONT_10X20_HEIGHT + gy]);
		CHECK_EQ(raster_cell_bits(out.bits[1 + gy], 10, 1), font_10x20['W' * FONT_10X20_HEIGHT + gy]);
	}
	// 6-pixel cells keep only their 6 bits.
	screen_init(&s, 132, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, GLYPH_FULL_BLOCK, RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), RASTER_132, 0);
	CHECK_EQ(raster_cell_bits(out.bits[5], 6, 0), 0x3f);
	CHECK_EQ(raster_cell_bits(out.bits[5], 6, 1), 0);
}

static void test_border_is_black(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		screen_init(&s, cols, RGB222(3, 3, 3), RGB222(3, 3, 3));
		render(screen_line(&s, 0), l, 0);
		const unsigned x0 = raster_x_offset(l), x_end = x0 + cols * raster_cell_width(l);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
				CHECK_EQ(shown(l, sy, x), x >= x0 && x < x_end ? RGB222(3, 3, 3) : 0);
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
		for (unsigned i = 0; i < sizeof verticals; ++i)
			screen_put(&s, i, 0, verticals[i], RGB222(2, 2, 2), 0);
		render(screen_line(&s, 0), l, 0);
		for (unsigned i = 0; i < sizeof verticals; ++i) {
			// Some column of the glyph is lit on every scanline.
			unsigned always = (1u << w) - 1;
			for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
				unsigned lit = 0;
				for (unsigned b = 0; b < w; ++b)
					lit |= (shown(l, sy, x0 + i * w + b) != 0) << b;
				always &= lit;
			}
			CHECK(always != 0);
		}

		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_puts(&s, 0, 0, "\xc5\xc9gjpqy|", RGB222(2, 2, 2), 0); // Å É descenders and a bar
		render(screen_line(&s, 0), l, 0);
		unsigned top = raster_pad_top(l), height = raster_glyph_height(l);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			if (sy >= top && sy < top + height)
				continue;
			for (unsigned x = x0; x < x0 + 8 * w; ++x)
				CHECK_EQ(shown(l, sy, x), 0);
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
			if (x0) // the full width has no border
				CHECK_EQ(shown_at(l, sy, x0 - 1, 0), 0); // border unaffected
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
		for (unsigned c = 0; c < 3; ++c)
			screen_put_attr(&s, c, 0, ' ', RGB222(0, 3, 0), 0, SCREEN_ATTR_UNDERLINE);
		render(screen_line(&s, 0), l, 0);
		for (unsigned x = x0; x < x0 + 3 * w; ++x) {
			for (unsigned i = 0; i < k; ++i)
				CHECK_EQ(shown(l, bottom - i, x), RGB222(0, 3, 0));
			CHECK_EQ(shown(l, bottom - k, x), 0);
			if (bottom + 1 < RASTER_ROW_HEIGHT)
				CHECK_EQ(shown(l, bottom + 1, x), 0);
		}
		CHECK_EQ(shown(l, bottom, x0 + 3 * w), 0);
	}
}

// Blink: glyph (and underline) hidden in the off phase; the renderer is
// told the line needs re-rendering when the phase changes.
static void test_blink(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "ab", RGB222(2, 2, 2), 0);
	CHECK(!raster_render_line(&out, screen_line(&s, 0), RASTER_80, RASTER_BLINK_OFF));
	screen_put_attr(&s, 1, 0, 'H', RGB222(3, 0, 0), 0, SCREEN_ATTR_BLINK | SCREEN_ATTR_UNDERLINE);
	CHECK(raster_render_line(&out, screen_line(&s, 0), RASTER_80, 0));
	render(screen_line(&s, 0), RASTER_80, 0);
	unsigned lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			lit += shown(RASTER_80, sy, x) != 0;
	CHECK(lit > 0);
	render(screen_line(&s, 0), RASTER_80, RASTER_BLINK_OFF);
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			CHECK_EQ(shown(RASTER_80, sy, x), 0);
	// The non-blinking neighbour is unaffected.
	lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			lit += shown(RASTER_80, sy, x) != 0;
	CHECK(lit > 0);
}

// Bold: in 80 columns Terminus's bold face; in 132 (no bold face) glyph
// rows ORed with themselves shifted a pixel right, short of the cell's last
// column so cells stay apart. Line-drawing glyphs keep their shape so they
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
			unsigned bold = raster_has_bold_face(l) ? raster_glyph_row(l, true, 'I', gy)
				: g | ((g << 1) & ((1u << w) - 1) >> 1);
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
			for (unsigned i = 0; i < w; ++i)
				lit += shown(l, sy, x0 + i) != 0;
			gaps += lit > 0 && lit < w - 1;
		}
		if (raster_has_bold_face(l))
			CHECK(gaps > 0); // not a solid block
	}
}

// Reverse (DECSCNM): every cell's fg and bg swap.
static void test_reverse(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "Hi", RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), RASTER_80, RASTER_REVERSE);
	// A blank cell shows the (old) fg everywhere; a glyph pixel shows black.
	CHECK_EQ(shown(RASTER_80, 0, 100), RGB222(2, 2, 2));
	unsigned dark = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			dark += shown(RASTER_80, sy, x) == 0;
	CHECK(dark > 0);
	CHECK_EQ(shown(RASTER_80, 12, 10), 0); // border stays black
}

// The demo screen, the xterm colours as adjacent backgrounds included, is
// pixel-exact in both widths.
static void test_demo(void) {
	for (raster_layout_t l = 0; l < RASTER_LAYOUTS; ++l) {
		const unsigned cols = raster_cols(l);
		demo_draw(&s, cols, raster_font_name(l), " status");
		unsigned bad = 0;
		for (unsigned row = 0; row < SCREEN_ROWS; ++row)
			bad += mismatches(row, l);
		CHECK_EQ(bad, 0);
	}
}

int main(void) {
	RUN(test_layout);
	RUN(test_any_colours_exact);
	RUN(test_glyph_bits);
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
