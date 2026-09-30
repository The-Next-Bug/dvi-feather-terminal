#include <string.h>

#include "demo.h"
#include "font.h"
#include "raster.h"
#include "test.h"

static screen_t s;
static raster_line_t out;

// The level (0-3) the encoder will output for pixel x of scanline sy.
static unsigned shown_level(const raster_line_t *r, unsigned cols, unsigned sy, unsigned x, int plane) {
	unsigned bit = r->bits[raster_stored_row(cols, sy)][plane][x / 8] >> x % 8 & 1;
	unsigned g = x / 8;
	unsigned nibble = r->colour[plane][g / 8] >> 4 * (g % 8) & 0xf;
	return bit ? nibble & 3 : nibble >> 2;
}

// Whether pixel i of glyph g is set on scanline sy (0-23) of a text row:
// the glyph centred in the row, with connectors extended to the edges.
static unsigned glyph_pixel(unsigned cols, uint8_t g, unsigned sy, unsigned i) {
	const uint8_t *font = cols > 80 ? font_6x13 : font_8x16;
	int rows = (int)raster_font_rows(cols), gy = (int)sy - (int)raster_pad_top(cols);
	if (gy < 0 || gy >= rows) {
		if (!glyph_is_connector(g))
			return 0;
		gy = gy < 0 ? 0 : rows - 1;
	}
	return font[gy * FONT_GLYPHS + g] >> i & 1;
}

// The level the cells ask for at pixel x of scanline sy.
static unsigned wanted_level(const screen_line_t *line, unsigned cols, unsigned sy, unsigned x, int plane) {
	unsigned w = raster_cell_width(cols), x0 = raster_x_offset(cols);
	if (x < x0 || x >= x0 + cols * w)
		return 0; // border
	unsigned c = (x - x0) / w, i = (x - x0) % w;
	unsigned set = glyph_pixel(cols, line->glyph[c], sy, i);
	return (set ? line->fg[c] : line->bg[c]) >> 2 * plane & 3;
}

// Render row `row` of s and count pixels (over all planes and displayed
// scanlines and pixels, border included) whose level is not the wanted one.
static unsigned mismatches(unsigned row) {
	const screen_line_t *line = screen_line(&s, row);
	raster_render_line(&out, line, s.cols, -1, 0);
	unsigned bad = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (int p = 0; p < RASTER_PLANES; ++p)
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
				bad += shown_level(&out, s.cols, sy, x, p) != wanted_level(line, s.cols, sy, x, p);
	return bad;
}

// White text on a blue background (the "blue" colours setting) at 132
// columns, with ink in the first and last cells: every pixel must be exact.
// With the text 4 px in from the edges, the edge groups needed black
// (border), blue and white in the blue plane, and the background there went
// black.
static void test_132_blue_background_edges(void) {
	const uint8_t white = RGB222(2, 2, 2), blue = RGB222(0, 0, 1);
	screen_init(&s, 132, white, blue);
	screen_puts(&s, 0, 0, "W", white, blue);
	screen_puts(&s, 131, 0, "W", white, blue);
	CHECK_EQ(mismatches(0), 0);
}

static void test_layout(void) {
	CHECK_EQ(raster_cell_width(80), 8);
	CHECK_EQ(raster_x_offset(80), 80);
	CHECK_EQ(raster_cell_width(132), 6);
	CHECK_EQ(raster_x_offset(132), 0); // whole groups (centre would be 4)
	CHECK(raster_x_offset(132) + 132 * 6 <= RASTER_ACTIVE_WIDTH);
	CHECK_EQ(SCREEN_ROWS * RASTER_ROW_HEIGHT, RASTER_ACTIVE_HEIGHT);
	CHECK_EQ(RASTER_WIDTH % 64, 0);
	// Scanlines map onto top padding, every glyph row in order, bottom padding.
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned rows = raster_font_rows(cols), top = raster_pad_top(cols);
		CHECK(rows + 2 <= RASTER_STORED_ROWS);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			unsigned want = sy < top ? 0 : sy < top + rows ? sy - top + 1 : rows + 1;
			CHECK_EQ(raster_stored_row(cols, sy), want);
		}
	}
}

// 80 columns: every group is one cell, so any fg/bg pair is exact.
static void test_80_any_colours_exact(void) {
	screen_init(&s, 80, 0, 0);
	for (unsigned c = 0; c < 80; ++c)
		screen_put(&s, c, 0, (uint8_t)(0x21 + c), (uint8_t)(c * 7 % 64), (uint8_t)(c * 13 % 64));
	CHECK_EQ(mismatches(0), 0);
}

static void test_80_glyph_bits(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, 'A', RGB222(3, 0, 0), 0);
	raster_render_line(&out, screen_line(&s, 0), 80, -1, 0);
	// Red plane: 'A' pixels at byte 10 (x = 80), set level 3, clear level 0.
	for (unsigned gy = 0; gy < FONT_8X16_HEIGHT; ++gy)
		CHECK_EQ(out.bits[1 + gy][2][10], font_8x16[gy * FONT_GLYPHS + 'A']);
	CHECK_EQ(out.colour[2][1] >> 4 * 2 & 0xf, 3); // group 10
	// Blue plane: fg level 0 = bg level 0, so nothing is set.
	for (unsigned sr = 0; sr < FONT_8X16_HEIGHT + 2; ++sr)
		CHECK_EQ(out.bits[sr][0][10], 0);
	// Padding rows above and below the glyph are blank.
	CHECK_EQ(out.bits[0][2][10], 0);
	CHECK_EQ(out.bits[FONT_8X16_HEIGHT + 1][2][10], 0);
}

// 132 columns, one background and fg colours of one level per plane:
// exact even where cells share groups.
static void test_132_hues_exact(void) {
	screen_init(&s, 132, 0, 0);
	for (unsigned c = 0; c < 132; ++c) {
		unsigned n = c % 7 + 1; // red..white, level 2
		uint8_t fg = RGB222(n & 1 ? 2 : 0, n & 2 ? 2 : 0, n & 4 ? 2 : 0);
		screen_put(&s, c, 0, (uint8_t)('!' + c % 94), fg, 0);
	}
	CHECK_EQ(mismatches(0), 0);
}

static void test_132_reverse_exact(void) {
	screen_init(&s, 132, RGB222(2, 2, 2), 0);
	for (unsigned c = 0; c < 132; c += 2)
		screen_put(&s, c, 0, 'x', 0, RGB222(2, 2, 2)); // alternate reverse video
	CHECK_EQ(mismatches(0), 0);
}

// Three levels in one group and plane can't all be shown; each pixel gets
// the nearer of the group's two, so the error is bounded.
static void test_132_three_levels_approximated(void) {
	screen_init(&s, 132, 0, 0);
	for (unsigned c = 0; c < 132; ++c)
		screen_put(&s, c, 0, 0x83 /* full block */, RGB222(c % 3 + 1, 0, 0), 0);
	const screen_line_t *line = screen_line(&s, 0);
	raster_render_line(&out, line, 132, -1, 0);
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
			int d = (int)shown_level(&out, 132, sy, x, 2) - (int)wanted_level(line, 132, sy, x, 2);
			CHECK(d >= -1 && d <= 1);
		}
}

static void test_border_is_black(void) {
	screen_init(&s, 132, RGB222(3, 3, 3), RGB222(3, 3, 3));
	raster_render_line(&out, screen_line(&s, 0), 132, -1, 0);
	const unsigned x0 = raster_x_offset(132), x_end = x0 + 132 * 6;
	for (int p = 0; p < RASTER_PLANES; ++p)
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
				CHECK_EQ(shown_level(&out, 132, sy, x, p), x >= x0 && x < x_end ? 3 : 0);
		}
}

// Vertical line-drawing glyphs must be continuous across the whole row
// height, so stacked rows join; ordinary glyphs with ink in their edge rows
// (accents, descenders) must not leak into the padding.
static void test_connectors_join(void) {
	static const uint8_t verticals[] = {
		GLYPH_DEC_GRAPHICS('x'), GLYPH_DEC_GRAPHICS('n'), GLYPH_DOUBLE_V, GLYPH_FULL_BLOCK,
	};
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x0 = raster_x_offset(cols), w = raster_cell_width(cols);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		for (unsigned i = 0; i < sizeof verticals; ++i)
			screen_put(&s, i, 0, verticals[i], RGB222(2, 2, 2), 0);
		raster_render_line(&out, screen_line(&s, 0), cols, -1, 0);
		for (unsigned i = 0; i < sizeof verticals; ++i) {
			// Some column of the glyph is lit on every scanline.
			unsigned always = (1u << w) - 1;
			for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
				unsigned lit = 0;
				for (unsigned b = 0; b < w; ++b)
					lit |= (shown_level(&out, cols, sy, x0 + i * w + b, 2) != 0) << b;
				always &= lit;
			}
			CHECK(always != 0);
		}

		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_puts(&s, 0, 0, "\xc5\xc9gjpqy|", RGB222(2, 2, 2), 0); // Å É descenders and a bar
		raster_render_line(&out, screen_line(&s, 0), cols, -1, 0);
		unsigned top = raster_pad_top(cols), rows = raster_font_rows(cols);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			if (sy >= top && sy < top + rows)
				continue;
			for (unsigned x = x0; x < x0 + 8 * w; ++x)
				CHECK_EQ(shown_level(&out, cols, sy, x, 2), 0);
		}
	}
}

// The cursor cell gets both of its levels, so inverting it shows even on a
// blank cell; without the cursor a blank cell's groups hold only bg.
static void test_cursor_cell_has_both_levels(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x = raster_x_offset(cols) + 5 * raster_cell_width(cols);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		raster_render_line(&out, screen_line(&s, 0), cols, 5, 0);
		unsigned g = x / 8, nibble = out.colour[1][g / 8] >> 4 * (g % 8) & 0xf;
		CHECK_EQ(nibble & 3, 2); // set level: the fg
		CHECK_EQ(nibble >> 2, 0); // clear level: the bg
		CHECK_EQ(shown_level(&out, cols, 12, x, 1), 0); // still shows as bg
		raster_render_line(&out, screen_line(&s, 0), cols, -1, 0);
		nibble = out.colour[1][g / 8] >> 4 * (g % 8) & 0xf;
		CHECK_EQ(nibble, 0);
	}
}

// Underline: the glyph's bottom row lit across the whole cell in the fg, so
// underlined runs join up.
static void test_underline(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x0 = raster_x_offset(cols), w = raster_cell_width(cols);
		unsigned bottom = raster_pad_top(cols) + raster_font_rows(cols) - 1;
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		for (unsigned c = 0; c < 3; ++c)
			screen_put_attr(&s, c, 0, ' ', RGB222(0, 3, 0), 0, SCREEN_ATTR_UNDERLINE);
		raster_render_line(&out, screen_line(&s, 0), cols, -1, 0);
		for (unsigned x = x0; x < x0 + 3 * w; ++x) {
			CHECK_EQ(shown_level(&out, cols, bottom, x, 1), 3);
			CHECK_EQ(shown_level(&out, cols, bottom - 1, x, 1), 0);
		}
		CHECK_EQ(shown_level(&out, cols, bottom, x0 + 3 * w, 1), 0);
	}
}

// Blink: glyph (and underline) hidden in the off phase; the renderer is
// told the line needs re-rendering when the phase changes.
static void test_blink(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "ab", RGB222(2, 2, 2), 0);
	CHECK(!raster_render_line(&out, screen_line(&s, 0), 80, -1, RASTER_BLINK_OFF));
	screen_put_attr(&s, 1, 0, 'H', RGB222(3, 0, 0), 0, SCREEN_ATTR_BLINK | SCREEN_ATTR_UNDERLINE);
	CHECK(raster_render_line(&out, screen_line(&s, 0), 80, -1, 0));
	unsigned lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			lit += shown_level(&out, 80, sy, x, 2) != 0;
	CHECK(lit > 0);
	CHECK(raster_render_line(&out, screen_line(&s, 0), 80, -1, RASTER_BLINK_OFF));
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			CHECK_EQ(shown_level(&out, 80, sy, x, 2), 0);
	// The non-blinking neighbour is unaffected.
	lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			lit += shown_level(&out, 80, sy, x, 1) != 0;
	CHECK(lit > 0);
}

// Bold: glyph rows ORed with themselves shifted one pixel right (within
// the cell); line-drawing glyphs keep their shape so they still join.
static void test_bold(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x0 = raster_x_offset(cols), w = raster_cell_width(cols);
		const uint8_t *font = cols > 80 ? font_6x13 : font_8x16;
		unsigned rows = raster_font_rows(cols), top = raster_pad_top(cols);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_put_attr(&s, 0, 0, 'I', RGB222(3, 3, 3), 0, SCREEN_ATTR_BOLD);
		screen_put_attr(&s, 1, 0, GLYPH_DEC_GRAPHICS('x'), RGB222(3, 3, 3), 0, SCREEN_ATTR_BOLD);
		raster_render_line(&out, screen_line(&s, 0), cols, -1, 0);
		for (unsigned gy = 0; gy < rows; ++gy) {
			unsigned g = font[gy * FONT_GLYPHS + 'I'], bold = (g | g << 1) & ((1u << w) - 1);
			unsigned v = font[gy * FONT_GLYPHS + GLYPH_DEC_GRAPHICS('x')];
			for (unsigned i = 0; i < w; ++i) {
				CHECK_EQ(shown_level(&out, cols, top + gy, x0 + i, 1), (bold >> i & 1) ? 3 : 0);
				CHECK_EQ(shown_level(&out, cols, top + gy, x0 + w + i, 1), (v >> i & 1) ? 3 : 0);
			}
		}
	}
}

// Reverse (DECSCNM): every cell's fg and bg swap.
static void test_reverse(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "Hi", RGB222(2, 2, 2), 0);
	raster_render_line(&out, screen_line(&s, 0), 80, -1, RASTER_REVERSE);
	// A blank cell shows the (old) fg everywhere; a glyph pixel shows black.
	CHECK_EQ(shown_level(&out, 80, 0, 100, 1), 2);
	unsigned dark = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			dark += shown_level(&out, 80, sy, x, 1) == 0;
	CHECK(dark > 0);
	CHECK_EQ(shown_level(&out, 80, 12, 10, 1), 0); // border stays black
}

// The demo at 80 columns must be pixel-exact; at 132 only its row of all 64
// colours as adjacent backgrounds (row 21) may be approximated.
static void test_demo(void) {
	demo_draw(&s, 80, " status");
	unsigned bad = 0;
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		bad += mismatches(row);
	CHECK_EQ(bad, 0);

	demo_draw(&s, 132, " status");
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		if (row != 21)
			CHECK_EQ(mismatches(row), 0);
	printf("     132-column swatch row: %u of %u pixel-planes approximated\n", mismatches(21),
		RASTER_ROW_HEIGHT * RASTER_PLANES * RASTER_WIDTH);
}

int main(void) {
	RUN(test_layout);
	RUN(test_132_blue_background_edges);
	RUN(test_80_any_colours_exact);
	RUN(test_80_glyph_bits);
	RUN(test_132_hues_exact);
	RUN(test_132_reverse_exact);
	RUN(test_132_three_levels_approximated);
	RUN(test_border_is_black);
	RUN(test_connectors_join);
	RUN(test_cursor_cell_has_both_levels);
	RUN(test_underline);
	RUN(test_blink);
	RUN(test_bold);
	RUN(test_reverse);
	RUN(test_demo);
	return tests_done();
}
