#include <string.h>

#include "demo.h"
#include "font.h"
#include "raster.h"
#include "test.h"

static screen_t s;
static raster_line_t out;

// The renderer's colour table for these tests: RGB222 colour c as the
// "pixel" c, so composed pixels read back as RGB222.
static uint32_t identity[64];

// The scanline last composed from `out`, and what it was composed for.
static uint32_t px[RASTER_LINE_WORDS];
static struct {
	bool valid;
	unsigned cols, sr;
	int cursor;
} composed;

static void render(const screen_line_t *line, unsigned cols, unsigned flags) {
	for (unsigned c = 0; c < 64; ++c)
		identity[c] = c | c << 16;
	raster_render_line(&out, line, cols, flags);
	composed.valid = false;
}

// The colour (RGB222) of pixel x on scanline sy of `out`, with the cursor on
// cell cursor (-1: none).
static unsigned shown_at(unsigned cols, unsigned sy, unsigned x, int cursor) {
	unsigned sr = raster_stored_row(cols, sy);
	if (!composed.valid || composed.cols != cols || composed.sr != sr || composed.cursor != cursor) {
		// Poison first: every word must be written.
		memset(px, 0xa5, sizeof px);
		raster_compose(px, &out, cols, sr, cursor, identity);
		composed.valid = true;
		composed.cols = cols;
		composed.sr = sr;
		composed.cursor = cursor;
	}
	return px[x / 2] >> 16 * (x & 1) & 0xffff;
}

static unsigned shown(unsigned cols, unsigned sy, unsigned x) {
	return shown_at(cols, sy, x, -1);
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

// The colour the cells ask for at pixel x of scanline sy (no attributes).
static unsigned wanted(const screen_line_t *line, unsigned cols, unsigned sy, unsigned x) {
	unsigned w = raster_cell_width(cols), x0 = raster_x_offset(cols);
	if (x < x0 || x >= x0 + cols * w)
		return 0; // border
	unsigned c = (x - x0) / w, i = (x - x0) % w;
	return glyph_pixel(cols, line->glyph[c], sy, i) ? line->fg[c] : line->bg[c];
}

// Render row `row` of s and count pixels (over all scanlines and pixels,
// border included) whose colour is not the wanted one.
static unsigned mismatches(unsigned row) {
	const screen_line_t *line = screen_line(&s, row);
	render(line, s.cols, 0);
	unsigned bad = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
			bad += shown(s.cols, sy, x) != wanted(line, s.cols, sy, x);
	return bad;
}

static void test_layout(void) {
	CHECK_EQ(raster_cell_width(80), 8);
	CHECK_EQ(raster_x_offset(80), 80);
	CHECK_EQ(raster_cell_width(132), 6);
	CHECK_EQ(raster_x_offset(132), 4); // centred
	CHECK_EQ(raster_x_offset(132) * 2 + 132 * 6, RASTER_ACTIVE_WIDTH);
	CHECK_EQ(SCREEN_ROWS * RASTER_ROW_HEIGHT, RASTER_ACTIVE_HEIGHT);
	// Scanlines map onto top padding, every glyph row in order, bottom padding.
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned rows = raster_font_rows(cols), top = raster_pad_top(cols);
		CHECK(rows + 2 <= RASTER_STORED_ROWS);
		CHECK_EQ(raster_x_offset(cols) % 2, 0); // cells start on whole words
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			unsigned want = sy < top ? 0 : sy < top + rows ? sy - top + 1 : rows + 1;
			CHECK_EQ(raster_stored_row(cols, sy), want);
		}
	}
}

// Any fg/bg per cell is exact, in both widths: neighbouring cells no longer
// share colour levels.
static void test_any_colours_exact(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c)
			screen_put(&s, c, 0, (uint8_t)(0x21 + c % 94), (uint8_t)(c * 7 % 64), (uint8_t)(c * 13 % 64));
		CHECK_EQ(mismatches(0), 0);
		// Full blocks of three levels side by side (the old encoder's worst case).
		screen_init(&s, cols, 0, 0);
		for (unsigned c = 0; c < cols; ++c)
			screen_put(&s, c, 0, GLYPH_FULL_BLOCK, RGB222(c % 3 + 1, 0, 0), RGB222(0, 0, c % 2 + 1));
		CHECK_EQ(mismatches(0), 0);
		// White on blue with ink in the first and last cells.
		screen_init(&s, cols, RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, 0, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		screen_puts(&s, cols - 1, 0, "W", RGB222(2, 2, 2), RGB222(0, 0, 1));
		CHECK_EQ(mismatches(0), 0);
	}
}

static void test_glyph_bits(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, 'A', RGB222(3, 0, 0), RGB222(0, 0, 1));
	render(screen_line(&s, 0), 80, 0);
	for (unsigned gy = 0; gy < FONT_8X16_HEIGHT; ++gy)
		CHECK_EQ(out.bits[1 + gy][0], font_8x16[gy * FONT_GLYPHS + 'A']);
	CHECK_EQ(out.fg[0], RGB222(3, 0, 0));
	CHECK_EQ(out.bg[0], RGB222(0, 0, 1));
	// Padding rows above and below the glyph are blank.
	CHECK_EQ(out.bits[0][0], 0);
	CHECK_EQ(out.bits[FONT_8X16_HEIGHT + 1][0], 0);
	// 6-pixel cells keep only their 6 bits.
	screen_init(&s, 132, RGB222(2, 2, 2), 0);
	screen_put(&s, 0, 0, GLYPH_FULL_BLOCK, RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), 132, 0);
	CHECK_EQ(out.bits[5][0], 0x3f);
}

static void test_border_is_black(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		screen_init(&s, cols, RGB222(3, 3, 3), RGB222(3, 3, 3));
		render(screen_line(&s, 0), cols, 0);
		const unsigned x0 = raster_x_offset(cols), x_end = x0 + cols * raster_cell_width(cols);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
			for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x)
				CHECK_EQ(shown(cols, sy, x), x >= x0 && x < x_end ? RGB222(3, 3, 3) : 0);
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
		render(screen_line(&s, 0), cols, 0);
		for (unsigned i = 0; i < sizeof verticals; ++i) {
			// Some column of the glyph is lit on every scanline.
			unsigned always = (1u << w) - 1;
			for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
				unsigned lit = 0;
				for (unsigned b = 0; b < w; ++b)
					lit |= (shown(cols, sy, x0 + i * w + b) != 0) << b;
				always &= lit;
			}
			CHECK(always != 0);
		}

		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_puts(&s, 0, 0, "\xc5\xc9gjpqy|", RGB222(2, 2, 2), 0); // Å É descenders and a bar
		render(screen_line(&s, 0), cols, 0);
		unsigned top = raster_pad_top(cols), rows = raster_font_rows(cols);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			if (sy >= top && sy < top + rows)
				continue;
			for (unsigned x = x0; x < x0 + 8 * w; ++x)
				CHECK_EQ(shown(cols, sy, x), 0);
		}
	}
}

// The cursor cell is shown inverted on every scanline of the row: a blank
// cell becomes a block of the fg, a glyph's pixels swap fg and bg. Other
// cells are untouched.
static void test_cursor_inverts_cell(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x0 = raster_x_offset(cols), w = raster_cell_width(cols);
		const uint8_t fg = RGB222(2, 2, 2), bg = RGB222(0, 0, 1);
		screen_init(&s, cols, fg, bg);
		screen_put(&s, 6, 0, 'H', fg, bg);
		const screen_line_t *line = screen_line(&s, 0);
		render(line, cols, 0);
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			for (unsigned i = 0; i < w; ++i) {
				unsigned x5 = x0 + 5 * w + i, x6 = x0 + 6 * w + i;
				unsigned inv = wanted(line, cols, sy, x6) == fg ? bg : fg;
				CHECK_EQ(shown_at(cols, sy, x5, 5), fg); // blank cell: all fg
				CHECK_EQ(shown_at(cols, sy, x6, 5), wanted(line, cols, sy, x6));
				CHECK_EQ(shown_at(cols, sy, x6, 6), inv); // glyph: swapped
				CHECK_EQ(shown_at(cols, sy, x5, 6), bg);
			}
			CHECK_EQ(shown_at(cols, sy, x0 - 1, 0), 0); // border unaffected
		}
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
		render(screen_line(&s, 0), cols, 0);
		for (unsigned x = x0; x < x0 + 3 * w; ++x) {
			CHECK_EQ(shown(cols, bottom, x), RGB222(0, 3, 0));
			CHECK_EQ(shown(cols, bottom - 1, x), 0);
		}
		CHECK_EQ(shown(cols, bottom, x0 + 3 * w), 0);
	}
}

// Blink: glyph (and underline) hidden in the off phase; the renderer is
// told the line needs re-rendering when the phase changes.
static void test_blink(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "ab", RGB222(2, 2, 2), 0);
	CHECK(!raster_render_line(&out, screen_line(&s, 0), 80, RASTER_BLINK_OFF));
	screen_put_attr(&s, 1, 0, 'H', RGB222(3, 0, 0), 0, SCREEN_ATTR_BLINK | SCREEN_ATTR_UNDERLINE);
	CHECK(raster_render_line(&out, screen_line(&s, 0), 80, 0));
	render(screen_line(&s, 0), 80, 0);
	unsigned lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			lit += shown(80, sy, x) != 0;
	CHECK(lit > 0);
	render(screen_line(&s, 0), 80, RASTER_BLINK_OFF);
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 88; x < 96; ++x)
			CHECK_EQ(shown(80, sy, x), 0);
	// The non-blinking neighbour is unaffected.
	lit = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			lit += shown(80, sy, x) != 0;
	CHECK(lit > 0);
}

// Bold: glyph rows ORed with themselves shifted one pixel right (within
// the cell); line-drawing glyphs keep their shape so they still join.
static void test_bold(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		unsigned x0 = raster_x_offset(cols), w = raster_cell_width(cols);
		const uint8_t *font = cols > 80 ? font_6x13 : font_8x16;
		unsigned rows = raster_font_rows(cols), top = raster_pad_top(cols);
		const uint8_t fg = RGB222(3, 3, 3);
		screen_init(&s, cols, RGB222(2, 2, 2), 0);
		screen_put_attr(&s, 0, 0, 'I', fg, 0, SCREEN_ATTR_BOLD);
		screen_put_attr(&s, 1, 0, GLYPH_DEC_GRAPHICS('x'), fg, 0, SCREEN_ATTR_BOLD);
		render(screen_line(&s, 0), cols, 0);
		for (unsigned gy = 0; gy < rows; ++gy) {
			unsigned g = font[gy * FONT_GLYPHS + 'I'], bold = (g | g << 1) & ((1u << w) - 1);
			unsigned v = font[gy * FONT_GLYPHS + GLYPH_DEC_GRAPHICS('x')];
			for (unsigned i = 0; i < w; ++i) {
				CHECK_EQ(shown(cols, top + gy, x0 + i), (bold >> i & 1) ? fg : 0);
				CHECK_EQ(shown(cols, top + gy, x0 + w + i), (v >> i & 1) ? fg : 0);
			}
		}
	}
}

// Reverse (DECSCNM): every cell's fg and bg swap.
static void test_reverse(void) {
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	screen_puts(&s, 0, 0, "Hi", RGB222(2, 2, 2), 0);
	render(screen_line(&s, 0), 80, RASTER_REVERSE);
	// A blank cell shows the (old) fg everywhere; a glyph pixel shows black.
	CHECK_EQ(shown(80, 0, 100), RGB222(2, 2, 2));
	unsigned dark = 0;
	for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy)
		for (unsigned x = 80; x < 88; ++x)
			dark += shown(80, sy, x) == 0;
	CHECK(dark > 0);
	CHECK_EQ(shown(80, 12, 10), 0); // border stays black
}

// The demo screen, all 64 colours as adjacent backgrounds included, is
// pixel-exact in both widths.
static void test_demo(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		demo_draw(&s, cols, " status");
		unsigned bad = 0;
		for (unsigned row = 0; row < SCREEN_ROWS; ++row)
			bad += mismatches(row);
		CHECK_EQ(bad, 0);
	}
}

int main(void) {
	RUN(test_layout);
	RUN(test_any_colours_exact);
	RUN(test_glyph_bits);
	RUN(test_border_is_black);
	RUN(test_connectors_join);
	RUN(test_cursor_inverts_cell);
	RUN(test_underline);
	RUN(test_blink);
	RUN(test_bold);
	RUN(test_reverse);
	RUN(test_demo);
	return tests_done();
}
