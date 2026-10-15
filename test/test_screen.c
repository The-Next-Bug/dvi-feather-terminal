#include <stdbool.h>

#include "screen.h"
#include "test.h"

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 1)
#define ALL_SLOTS ((1u << SCREEN_ROWS) - 1)

static screen_t s;

static void test_init_fills_blanks(void) {
	for (unsigned cols = 80; cols <= 132; cols += 52) {
		screen_init(&s, cols, FG, BG);
		CHECK_EQ(s.cols, cols);
		CHECK_EQ(s.dirty, ALL_SLOTS);
		CHECK(!s.cursor_visible);
		for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
			CHECK_EQ(s.row_map[row], row);
			for (unsigned col = 0; col < cols; ++col) {
				uint8_t fg, bg;
				screen_colors(&s, col, row, &fg, &bg);
				CHECK_EQ(screen_glyph(&s, col, row), ' ');
				CHECK_EQ(fg, FG);
				CHECK_EQ(bg, BG);
			}
		}
	}
}

static void test_bad_width_means_80(void) {
	screen_init(&s, 100, FG, BG);
	CHECK_EQ(s.cols, 80);
}

static void test_put_round_trips(void) {
	screen_init(&s, 132, FG, BG);
	for (unsigned c = 0; c < 64; ++c) {
		screen_put(&s, c * 2, 3, (uint8_t)(0x80 + c), (uint8_t)c, (uint8_t)(63 - c));
	}
	for (unsigned c = 0; c < 64; ++c) {
		uint8_t fg, bg;
		screen_colors(&s, c * 2, 3, &fg, &bg);
		CHECK_EQ(screen_glyph(&s, c * 2, 3), 0x80 + c);
		CHECK_EQ(fg, c);
		CHECK_EQ(bg, 63 - c);
		CHECK_EQ(screen_glyph(&s, c * 2 + 1, 3), ' ');
	}
}

static void test_out_of_range_is_ignored(void) {
	screen_init(&s, 80, FG, BG);
	screen_t before = s;
	screen_put(&s, 80, 0, 'x', 1, 2); // beyond an 80-column screen
	screen_put(&s, 0, SCREEN_ROWS, 'x', 1, 2);
	screen_puts(&s, 78, 1, "abcd", FG, BG);
	CHECK_EQ(screen_glyph(&s, 78, 1), 'a');
	CHECK_EQ(screen_glyph(&s, 79, 1), 'b');
	CHECK_EQ(screen_glyph(&s, 80, 1), 0);
	CHECK_EQ(s.lines[1].glyph[80], ' '); // storage past the width untouched
	for (unsigned col = 0; col < 80; ++col) {
		CHECK_EQ(screen_glyph(&s, col, 0), screen_glyph(&before, col, 0));
	}
}

static void test_dirty_tracks_slots(void) {
	screen_init(&s, 80, FG, BG);
	s.dirty = 0;
	screen_put(&s, 5, 7, 'x', FG, BG);
	CHECK_EQ(s.dirty, 1u << 7);
	s.dirty = 0;
	screen_clear_line(&s, 2, FG, BG);
	CHECK_EQ(s.dirty, 1u << 2);

	// After a scroll only the recycled slot (old top of the region) is dirty,
	// and writes go to the slot now shown at that row.
	s.dirty = 0;
	screen_scroll_up(&s, 0, 23, FG, BG);
	CHECK_EQ(s.dirty, 1u << 0);
	CHECK_EQ(s.row_map[23], 0);
	s.dirty = 0;
	screen_put(&s, 0, 0, 'y', FG, BG);
	CHECK_EQ(s.dirty, 1u << 1);
}

static void test_scroll_region(void) {
	screen_init(&s, 80, FG, BG);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		screen_put(&s, 0, row, (uint8_t)('A' + row), FG, BG);
	}

	screen_scroll_up(&s, 5, 10, RGB222(1, 1, 1), RGB222(0, 1, 0));

	for (unsigned row = 0; row < 5; ++row) {
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row);
	}
	for (unsigned row = 5; row < 10; ++row) {
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row + 1);
	}
	uint8_t fg, bg;
	screen_colors(&s, 0, 10, &fg, &bg);
	CHECK_EQ(screen_glyph(&s, 0, 10), ' ');
	CHECK_EQ(fg, RGB222(1, 1, 1));
	CHECK_EQ(bg, RGB222(0, 1, 0));
	for (unsigned row = 11; row < SCREEN_ROWS; ++row) {
		CHECK_EQ(screen_glyph(&s, 0, row), 'A' + row);
	}

	// row_map must stay a permutation.
	unsigned seen = 0;
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		seen |= 1u << s.row_map[row];
	}
	CHECK_EQ(seen, ALL_SLOTS);
}

// The recycle hook: called for the slot a scroll reuses, with the row it
// becomes and the fill colors, after its cells are cleared and before
// row_map moves it (so the renderer can blank it before it shows there).
static struct {
	unsigned calls, slot, row;
	uint8_t fg, bg;
	bool cleared, still_at_old_row;
	unsigned old_row;
} hook;

static void recycle(void *ctx, unsigned slot, unsigned row, uint8_t fg, uint8_t bg) {
	CHECK(ctx == &hook);
	++hook.calls;
	hook.slot = slot;
	hook.row = row;
	hook.fg = fg;
	hook.bg = bg;
	hook.cleared = s.lines[slot].glyph[0] == ' ' && s.lines[slot].bg[0] == bg;
	hook.still_at_old_row = s.row_map[hook.old_row] == slot;
}

static void test_scroll_recycle_hook(void) {
	screen_set_recycle(&s, recycle, &hook);
	screen_init(&s, 80, FG, BG); // keeps the hook
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		screen_put(&s, 0, row, (uint8_t)('A' + row), FG, BG);
	}
	// Up: the top row's slot becomes the bottom row.
	unsigned slot = s.row_map[3];
	hook.calls = 0;
	hook.old_row = 3;
	screen_scroll_up(&s, 3, 12, RGB222(1, 1, 1), RGB222(0, 1, 0));
	CHECK_EQ(hook.calls, 1);
	CHECK_EQ(hook.slot, slot);
	CHECK_EQ(hook.row, 12);
	CHECK_EQ(hook.fg, RGB222(1, 1, 1));
	CHECK_EQ(hook.bg, RGB222(0, 1, 0));
	CHECK(hook.cleared);
	CHECK(hook.still_at_old_row);
	CHECK_EQ(s.row_map[12], slot);
	// Down: the bottom row's slot becomes the top row.
	slot = s.row_map[20];
	hook.old_row = 20;
	screen_scroll_down(&s, 0, 20, FG, BG);
	CHECK_EQ(hook.calls, 2);
	CHECK_EQ(hook.slot, slot);
	CHECK_EQ(hook.row, 0);
	CHECK(hook.cleared);
	CHECK(hook.still_at_old_row);
	CHECK_EQ(s.row_map[0], slot);
	// A rejected region recycles nothing.
	screen_scroll_up(&s, 5, 5, FG, BG);
	CHECK_EQ(hook.calls, 2);
	screen_set_recycle(&s, NULL, NULL);
}

static void test_scroll_many_times(void) {
	screen_init(&s, 132, FG, BG);
	const unsigned bottom = SCREEN_ROWS - 2;
	for (unsigned n = 0; n < 1000; ++n) {
		screen_scroll_up(&s, 0, bottom, FG, BG);
		screen_put(&s, 131, bottom, (uint8_t)(n & 0xff), FG, BG);
	}
	for (unsigned row = 0; row <= bottom; ++row) {
		CHECK_EQ(screen_glyph(&s, 131, row), (1000 - (bottom + 1) + row) & 0xff);
	}
}

static void test_scroll_rejects_bad_regions(void) {
	screen_init(&s, 80, FG, BG);
	screen_put(&s, 0, 3, 'x', FG, BG);
	screen_t before = s;
	screen_scroll_up(&s, 3, 3, FG, BG);
	screen_scroll_up(&s, 4, 2, FG, BG);
	screen_scroll_up(&s, 0, SCREEN_ROWS, FG, BG);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		CHECK_EQ(s.row_map[row], before.row_map[row]);
	}
	CHECK_EQ(screen_glyph(&s, 0, 3), 'x');
}

static void test_attributes(void) {
	screen_init(&s, 80, FG, BG);
	screen_put_attr(&s, 3, 2, 'u', FG, BG, SCREEN_ATTR_UNDERLINE);
	screen_put_attr(&s, 4, 2, 'b', FG, BG, SCREEN_ATTR_BLINK);
	CHECK_EQ(screen_attr(&s, 3, 2), SCREEN_ATTR_UNDERLINE);
	CHECK_EQ(screen_attr(&s, 4, 2), SCREEN_ATTR_BLINK);
	// Attributes move with their cells and are cleared when erased.
	screen_insert_cells(&s, 2, 0, 2, FG, BG);
	CHECK_EQ(screen_attr(&s, 5, 2), SCREEN_ATTR_UNDERLINE);
	CHECK_EQ(screen_attr(&s, 3, 2), 0);
	screen_delete_cells(&s, 2, 0, 2, FG, BG);
	CHECK_EQ(screen_attr(&s, 4, 2), SCREEN_ATTR_BLINK);
	screen_erase(&s, 2, 0, 80, FG, BG);
	CHECK_EQ(screen_attr(&s, 4, 2), 0);
	screen_put_attr(&s, 0, 2, 'x', FG, BG, SCREEN_ATTR_BLINK);
	screen_put(&s, 0, 2, 'y', FG, BG);
	CHECK_EQ(screen_attr(&s, 0, 2), 0);
}

static void test_reverse_rows_mark_dirty(void) {
	screen_init(&s, 80, FG, BG);
	screen_scroll_up(&s, 0, 23, FG, BG); // row 0 now shows slot 1
	s.dirty = 0;
	screen_set_reverse_rows(&s, 1u << 0);
	CHECK_EQ(s.reverse_rows, 1u << 0);
	CHECK_EQ(s.dirty, 1u << 1);
	s.dirty = 0;
	screen_set_reverse_rows(&s, 1u << 0);
	CHECK_EQ(s.dirty, 0);
	screen_init(&s, 80, FG, BG);
	CHECK_EQ(s.reverse_rows, 0);
}

// Saved rows come back in whichever slot now holds the row, marked dirty,
// with their colors and attributes.
static void test_stash_round_trip(void) {
	static uint8_t buf[SCREEN_STASH_BYTES(SCREEN_ROWS, 132)];
	screen_stash_t st;
	screen_stash_init(&st, buf, sizeof buf);
	screen_init(&s, 80, FG, BG);
	screen_puts(&s, 0, 20, "keep me", FG, BG);
	screen_put_attr(&s, 79, 21, 'z', 5, 6, SCREEN_ATTR_BOLD);
	CHECK(screen_stash_push(&st, &s, 'a', 20, 2));
	CHECK(screen_stash_holds(&st, 'a'));
	CHECK_EQ(st.used, SCREEN_STASH_BYTES(2, 80));
	screen_puts(&s, 0, 20, "overlay", BG, FG);
	screen_scroll_up(&s, 0, SCREEN_ROWS - 1, FG, BG); // the rows move to other slots
	screen_scroll_down(&s, 0, SCREEN_ROWS - 1, FG, BG);
	s.dirty = 0;
	CHECK(screen_stash_pop(&st, &s, 'a', FG, BG));
	CHECK_EQ(screen_glyph(&s, 0, 20), 'k');
	CHECK_EQ(screen_glyph(&s, 79, 21), 'z');
	uint8_t fg, bg;
	screen_colors(&s, 79, 21, &fg, &bg);
	CHECK(fg == 5 && bg == 6);
	CHECK_EQ(screen_attr(&s, 79, 21), SCREEN_ATTR_BOLD);
	CHECK_EQ(s.dirty, 1u << s.row_map[20] | 1u << s.row_map[21]);
	CHECK(!screen_stash_holds(&st, 'a'));
	CHECK_EQ(st.used, 0);
	CHECK(!screen_stash_pop(&st, &s, 'a', FG, BG)); // gone
}

// Entries share the buffer: too big, a tag used twice or a third entry is
// refused; dropping or popping the first keeps the second intact.
static void test_stash_entries(void) {
	static uint8_t buf[SCREEN_STASH_BYTES(30, 80)];
	screen_stash_t st;
	screen_stash_init(&st, buf, sizeof buf);
	screen_init(&s, 80, FG, BG);
	for (unsigned r = 0; r < SCREEN_ROWS; ++r) {
		screen_put(&s, 0, r, (uint8_t)('A' + r), FG, BG);
	}
	CHECK(!screen_stash_push(&st, &s, 0, 0, 1)); // tag 0 is none
	CHECK(screen_stash_push(&st, &s, 'm', 0, 24));
	CHECK(screen_stash_holds(&st, 'm'));
	CHECK(!screen_stash_push(&st, &s, 'm', 0, 1));        // tag in use
	CHECK(!screen_stash_push(&st, &s, 's', 10, 7));        // 24 + 7 rows > 30
	CHECK(screen_stash_push(&st, &s, 's', 10, 6));
	CHECK(!screen_stash_push(&st, &s, 't', 0, 0));         // only two entries
	for (unsigned r = 0; r < SCREEN_ROWS; ++r) {
		screen_put(&s, 0, r, '.', FG, BG);
	}
	// The first goes: the second moves down and still comes back right.
	screen_stash_drop(&st, 'm');
	CHECK_EQ(st.used, SCREEN_STASH_BYTES(6, 80));
	CHECK(screen_stash_pop(&st, &s, 's', FG, BG));
	for (unsigned r = 10; r < 16; ++r) {
		CHECK_EQ(screen_glyph(&s, 0, r), 'A' + r);
	}
	CHECK_EQ(screen_glyph(&s, 0, 9), '.');
	CHECK_EQ(screen_glyph(&s, 0, 16), '.');
	screen_stash_drop(&st, 'x'); // no such entry: nothing happens
	CHECK_EQ(st.n, 0);
	CHECK(!screen_stash_push(&st, &s, 'v', 20, SCREEN_ROWS)); // off the screen
}

// Rows saved at 80 columns and put back at 132: the new columns are
// cleared to the colors given; saved at 132 and put back at 80: cut.
static void test_stash_width_change(void) {
	static uint8_t buf[SCREEN_STASH_BYTES(SCREEN_ROWS, 132)];
	screen_stash_t st;
	screen_stash_init(&st, buf, sizeof buf);
	screen_init(&s, 80, FG, BG);
	screen_put(&s, 79, 3, 'x', FG, BG);
	CHECK(screen_stash_push(&st, &s, 'a', 3, 1));
	screen_init(&s, 132, FG, BG);
	screen_put(&s, 100, 3, 'y', FG, BG);
	CHECK(screen_stash_pop(&st, &s, 'a', 7, 1));
	CHECK_EQ(screen_glyph(&s, 79, 3), 'x');
	CHECK_EQ(screen_glyph(&s, 100, 3), ' ');
	uint8_t fg, bg;
	screen_colors(&s, 100, 3, &fg, &bg);
	CHECK(fg == 7 && bg == 1);
	screen_put(&s, 131, 3, 'w', FG, BG);
	CHECK(screen_stash_push(&st, &s, 'a', 3, 1));
	CHECK_EQ(st.used, SCREEN_STASH_BYTES(1, 132));
	screen_init(&s, 80, FG, BG);
	CHECK(screen_stash_pop(&st, &s, 'a', FG, BG));
	CHECK_EQ(screen_glyph(&s, 79, 3), 'x');
}

int main(void) {
	RUN(test_init_fills_blanks);
	RUN(test_stash_round_trip);
	RUN(test_stash_entries);
	RUN(test_stash_width_change);
	RUN(test_bad_width_means_80);
	RUN(test_put_round_trips);
	RUN(test_out_of_range_is_ignored);
	RUN(test_dirty_tracks_slots);
	RUN(test_scroll_region);
	RUN(test_scroll_many_times);
	RUN(test_scroll_recycle_hook);
	RUN(test_scroll_rejects_bad_regions);
	RUN(test_attributes);
	RUN(test_reverse_rows_mark_dirty);
	return tests_done();
}
