#include "screen.h"
#include "test.h"

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 1)

static screen_t s;

static void test_init_fills_blanks(void) {
	screen_init(&s, FG, BG);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		CHECK_EQ(s.row_map[row], row);
		for (unsigned col = 0; col < SCREEN_COLS; ++col) {
			uint8_t fg, bg;
			screen_colours(&s, col, row, &fg, &bg);
			CHECK_EQ(screen_glyph(&s, col, row), ' ');
			CHECK_EQ(fg, FG);
			CHECK_EQ(bg, BG);
		}
	}
}

static void test_put_round_trips_every_colour(void) {
	screen_init(&s, FG, BG);
	for (unsigned c = 0; c < 64; ++c) {
		unsigned col = c % SCREEN_COLS, row = c / SCREEN_COLS + 3;
		screen_put(&s, col, row, (uint8_t)(0x80 + c), (uint8_t)c, (uint8_t)(63 - c));
	}
	for (unsigned c = 0; c < 64; ++c) {
		unsigned col = c % SCREEN_COLS, row = c / SCREEN_COLS + 3;
		uint8_t fg, bg;
		screen_colours(&s, col, row, &fg, &bg);
		CHECK_EQ(screen_glyph(&s, col, row), 0x80 + c);
		CHECK_EQ(fg, c);
		CHECK_EQ(bg, 63 - c);
	}
}

// The renderer reads the planes directly: plane 0 is blue, and each cell has
// fg in bits 1:0 and bg in bits 3:2 of its nibble, lowest cell first.
static void test_colour_plane_layout(void) {
	screen_init(&s, 0, 0);
	screen_put(&s, 9, 0, 'x', RGB222(1, 2, 3), RGB222(3, 0, 1));
	const screen_line_t *line = screen_line(&s, 0);
	CHECK_EQ(line->colour[0][1], (3u | 1u << 2) << 4); // blue: fg 3, bg 1
	CHECK_EQ(line->colour[1][1], (2u | 0u << 2) << 4); // green: fg 2, bg 0
	CHECK_EQ(line->colour[2][1], (1u | 3u << 2) << 4); // red: fg 1, bg 3
	CHECK_EQ(line->colour[0][0], 0);
}

static void test_put_does_not_touch_neighbours(void) {
	screen_init(&s, FG, BG);
	screen_put(&s, 8, 1, 'A', RGB222(3, 3, 3), RGB222(3, 0, 0));
	uint8_t fg, bg;
	for (unsigned col = 7; col <= 9; col += 2) {
		screen_colours(&s, col, 1, &fg, &bg);
		CHECK_EQ(fg, FG);
		CHECK_EQ(bg, BG);
		CHECK_EQ(screen_glyph(&s, col, 1), ' ');
	}
}

static void test_out_of_range_is_ignored(void) {
	screen_init(&s, FG, BG);
	screen_t before = s;
	screen_put(&s, SCREEN_COLS, 0, 'x', 1, 2);
	screen_put(&s, 0, SCREEN_ROWS, 'x', 1, 2);
	screen_puts(&s, SCREEN_COLS - 2, 0, "abcd", FG, BG);
	CHECK_EQ(screen_glyph(&s, SCREEN_COLS - 2, 0), 'a');
	CHECK_EQ(screen_glyph(&s, SCREEN_COLS - 1, 0), 'b');
	CHECK_EQ(screen_glyph(&s, SCREEN_COLS, 0), 0);
	for (unsigned row = 1; row < SCREEN_ROWS; ++row)
		for (unsigned col = 0; col < SCREEN_COLS; ++col)
			CHECK_EQ(screen_glyph(&s, col, row), screen_glyph(&before, col, row));
}

static void test_scroll_region(void) {
	screen_init(&s, FG, BG);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		screen_put(&s, 0, row, (uint8_t)('A' + row % 26), FG, BG);

	screen_scroll_up(&s, 5, 10, RGB222(1, 1, 1), RGB222(0, 1, 0));

	for (unsigned row = 0; row < 5; ++row)
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row);
	for (unsigned row = 5; row < 10; ++row)
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row + 1);
	uint8_t fg, bg;
	screen_colours(&s, 0, 10, &fg, &bg);
	CHECK_EQ(screen_glyph(&s, 0, 10), ' ');
	CHECK_EQ(fg, RGB222(1, 1, 1));
	CHECK_EQ(bg, RGB222(0, 1, 0));
	for (unsigned row = 11; row < SCREEN_ROWS; ++row)
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row % 26);

	// row_map must stay a permutation.
	unsigned seen = 0;
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		seen |= 1u << s.row_map[row];
	CHECK_EQ(seen, (1u << SCREEN_ROWS) - 1);
}

static void test_scroll_full_screen_many_times(void) {
	screen_init(&s, FG, BG);
	for (unsigned n = 0; n < 1000; ++n) {
		screen_scroll_up(&s, 0, SCREEN_ROWS - 1, FG, BG);
		screen_put(&s, 0, SCREEN_ROWS - 1, (uint8_t)(n & 0xff), FG, BG);
	}
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		CHECK_EQ(screen_glyph(&s, 0, row), (1000 - SCREEN_ROWS + row) & 0xff);
}

static void test_scroll_rejects_bad_regions(void) {
	screen_init(&s, FG, BG);
	screen_put(&s, 0, 3, 'x', FG, BG);
	screen_t before = s;
	screen_scroll_up(&s, 3, 3, FG, BG);
	screen_scroll_up(&s, 4, 2, FG, BG);
	screen_scroll_up(&s, 0, SCREEN_ROWS, FG, BG);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row)
		CHECK_EQ(s.row_map[row], before.row_map[row]);
	CHECK_EQ(screen_glyph(&s, 0, 3), 'x');
}

static void test_swap_cell_colours(void) {
	screen_init(&s, RGB222(3, 2, 1), RGB222(0, 1, 2));
	const screen_line_t *line = screen_line(&s, 0);
	for (int plane = 0; plane < SCREEN_PLANES; ++plane) {
		uint32_t word = line->colour[plane][1];
		uint32_t swapped = screen_swap_cell_colours(word, 10);
		unsigned fg = (unsigned)(RGB222(3, 2, 1) >> 2 * plane & 3);
		unsigned bg = (unsigned)(RGB222(0, 1, 2) >> 2 * plane & 3);
		CHECK_EQ(swapped >> 8 & 0xf, bg | fg << 2); // cell 10 is nibble 2 of word 1
		CHECK_EQ(swapped & ~0xf00u, word & ~0xf00u); // other cells untouched
		CHECK_EQ(screen_swap_cell_colours(swapped, 10), word);
	}
}

int main(void) {
	RUN(test_init_fills_blanks);
	RUN(test_put_round_trips_every_colour);
	RUN(test_colour_plane_layout);
	RUN(test_put_does_not_touch_neighbours);
	RUN(test_out_of_range_is_ignored);
	RUN(test_scroll_region);
	RUN(test_scroll_full_screen_many_times);
	RUN(test_scroll_rejects_bad_regions);
	RUN(test_swap_cell_colours);
	return tests_done();
}
