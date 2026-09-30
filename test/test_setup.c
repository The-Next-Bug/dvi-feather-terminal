#include <string.h>

#include "setup.h"
#include "term.h"
#include "test.h"

#define FG RGB222(3, 3, 3)
#define BG RGB222(0, 0, 2)

static setup_t u;

static void open_with(unsigned columns) {
	settings_t s;
	settings_defaults(&s);
	s.columns = (uint8_t)columns;
	setup_open(&u, &s);
}

static setup_action_t send(const char *keys, uint32_t now) {
	setup_action_t last = SETUP_NONE;
	for (const char *p = keys; *p; ++p) {
		setup_action_t a = setup_input(&u, (uint8_t)*p, now);
		if (a != SETUP_NONE)
			last = a;
	}
	return last;
}

static void test_space_and_arrows_change_columns(void) {
	open_with(80);
	CHECK_EQ(send(" ", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send(" ", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW); // Right
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send("\x1bOD", 0), SETUP_REDRAW); // Left, DECCKM form
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(send("\x1b[D", 0), SETUP_REDRAW); // wraps round
	CHECK_EQ(u.values.columns, 132);
}

// Down moves to the next setting, whose choices Space then cycles.
static void test_down_selects_next_setting(void) {
	open_with(80);
	u.values.backspace = 0x7f;
	CHECK_EQ(send("\x1b[B", 0), SETUP_REDRAW);
	CHECK_EQ(u.item, 1);
	send(" ", 0);
	CHECK_EQ(u.values.backspace, 0x08);
	CHECK_EQ(u.values.columns, 80);
	send("\x1b[B ", 0); // Local echo on
	CHECK_EQ(u.values.local_echo, 1);
	for (unsigned i = 2; i < SETUP_ITEMS; ++i)
		send("\x1b[B", 0);
	CHECK_EQ(u.item, 0); // round to Columns
	send("\x1b[A", 0);
	CHECK_EQ(u.item, SETUP_ITEMS - 1);
}

static void test_return_saves(void) {
	open_with(80);
	send(" ", 0);
	CHECK_EQ(send("\r", 0), SETUP_SAVE);
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send("\n", 0), SETUP_SAVE);
}

static void test_lone_escape_cancels_after_timeout(void) {
	open_with(80);
	CHECK_EQ(send("\x1b", 1000), SETUP_NONE);
	CHECK_EQ(setup_tick(&u, 1000 + SETUP_ESC_TIMEOUT_MS - 1), SETUP_NONE);
	CHECK_EQ(setup_tick(&u, 1000 + SETUP_ESC_TIMEOUT_MS), SETUP_CANCEL);
	CHECK_EQ(setup_tick(&u, 5000), SETUP_NONE); // only once
}

static void test_escape_then_other_key_cancels(void) {
	open_with(80);
	CHECK_EQ(send("\x1bx", 0), SETUP_CANCEL);
}

static void test_other_sequences_are_ignored(void) {
	open_with(80);
	CHECK_EQ(send("\x1b[2~", 0), SETUP_NONE); // Insert
	CHECK_EQ(send("a", 0), SETUP_NONE);
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(setup_tick(&u, 10000), SETUP_NONE);
}

static void test_message_cleared_by_next_key(void) {
	open_with(80);
	u.message = "Saving to flash failed";
	CHECK_EQ(send("a", 0), SETUP_REDRAW);
	CHECK(u.message == NULL);
}

static bool row_contains(const screen_t *s, unsigned row, const char *text) {
	char line[SCREEN_MAX_COLS + 1];
	for (unsigned c = 0; c < s->cols; ++c)
		line[c] = (char)screen_glyph(s, c, row);
	line[s->cols] = 0;
	return strstr(line, text) != NULL;
}

static void test_draw_marks_current_choice(void) {
	static screen_t s;
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	open_with(132);
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS; // above the status line
	setup_draw(&u, &s, top, "build x", FG, BG);
	CHECK(row_contains(&s, top, "SET-UP"));
	CHECK(row_contains(&s, top, "build x"));
	CHECK(row_contains(&s, top + 1, "> Columns"));
	CHECK(row_contains(&s, top + 2, "  Backspace sends"));
	CHECK(row_contains(&s, top + 3, "  Local echo"));
	CHECK(row_contains(&s, top + 4, "< 115200 >")); // long lists: current only
	CHECK(row_contains(&s, top + 5, "< 8N1 >"));
	CHECK(row_contains(&s, top + 7, "  Colors"));
	CHECK(row_contains(&s, top + 7, "white   green   amber   blue")); // short lists: all
	CHECK(row_contains(&s, top + SETUP_ROWS - 1, "Return save"));
	// " 132 " is drawn reversed, " 80 " normal.
	char line[81];
	for (unsigned c = 0; c < 80; ++c)
		line[c] = (char)screen_glyph(&s, c, top + 1);
	line[80] = 0;
	unsigned at80 = (unsigned)(strstr(line, " 80 ") - line) + 1;
	unsigned at132 = (unsigned)(strstr(line, " 132 ") - line) + 1;
	uint8_t fg, bg;
	screen_colours(&s, at80, top + 1, &fg, &bg);
	CHECK_EQ(fg, FG);
	screen_colours(&s, at132, top + 1, &fg, &bg);
	CHECK_EQ(fg, BG);
	CHECK_EQ(bg, FG);
}

// Under Colors and Palette, two rows preview the choice: default text,
// bold, status line, and each ANSI colour as text (first row) and as a
// background (second row, same column).
static void test_colours_preview(void) {
	static screen_t s;
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	open_with(80);
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS;
	const unsigned preview = top + 9; // under Palette (item 7, row top + 8)
	setup_draw(&u, &s, top, "", FG, BG);
	CHECK(row_contains(&s, top + 7, "Colors"));
	CHECK(row_contains(&s, top + 8, "Palette"));
	CHECK(row_contains(&s, preview, " Text Bold  Status "));
	CHECK(row_contains(&s, preview, "0 1 2 3 4 5 6 7 8 9 A B C D E F"));
	CHECK(row_contains(&s, preview + 1, "0 1 2 3 4 5 6 7 8 9 A B C D E F"));
	CHECK(row_contains(&s, top + SETUP_ROWS - 1, "Return save"));

	unsigned text_col = 0, digits = 0;
	for (unsigned c = 0; c < 80; ++c) {
		if (!text_col && screen_glyph(&s, c, preview) == 'T')
			text_col = c;
		if (!digits && screen_glyph(&s, c, preview) == '0')
			digits = c;
	}
	uint8_t fg, bg;
	screen_colours(&s, text_col, preview, &fg, &bg);
	CHECK_EQ(fg, RGB222(2, 2, 2)); // white
	CHECK_EQ(bg, 0);
	screen_colours(&s, text_col + 5, preview, &fg, &bg); // "Bold"
	CHECK_EQ(fg, RGB222(3, 3, 3));
	CHECK(screen_attr(&s, text_col + 5, preview) & SCREEN_ATTR_BOLD);
	for (unsigned i = 0; i < 16; ++i) {
		unsigned col = digits + 2 * i;
		screen_colours(&s, col, preview, &fg, &bg);
		CHECK_EQ(fg, term_ansi_colour(i)); // as text
		CHECK_EQ(bg, 0);
		screen_colours(&s, col, preview + 1, &fg, &bg);
		CHECK_EQ(fg, RGB222(2, 2, 2)); // as background
		CHECK_EQ(bg, term_ansi_colour(i));
		screen_colours(&s, col + 1, preview + 1, &fg, &bg);
		CHECK_EQ(bg, 0); // spaced out with the default background
	}

	// Colors -> blue and Palette -> mono: the preview follows both.
	for (unsigned i = 0; i < 6; ++i)
		send("\x1b[B", 0);
	send("\x1b[D", 0); // white -> blue (wraps)
	send("\x1b[B ", 0); // Palette: mono
	CHECK_EQ(u.values.colours, 3);
	CHECK_EQ(u.values.palette, 1);
	setup_draw(&u, &s, top, "", FG, BG);
	// Mono, blue phosphor: as text, normal colours blue and bright ones
	// bright blue; as backgrounds, inverse (bright ones bright), black plain.
	const uint8_t blue = RGB222(0, 1, 3), bright = RGB222(0, 2, 3), black = 0;
	screen_colours(&s, text_col, preview, &fg, &bg);
	CHECK(fg == blue && bg == black);
	screen_colours(&s, digits + 2, preview, &fg, &bg); // red as text
	CHECK(fg == blue && bg == black);
	screen_colours(&s, digits + 18, preview, &fg, &bg); // bright red as text
	CHECK(fg == bright && bg == black);
	screen_colours(&s, digits + 2, preview + 1, &fg, &bg); // red as background
	CHECK(fg == black && bg == blue);
	screen_colours(&s, digits + 18, preview + 1, &fg, &bg); // bright red
	CHECK(fg == black && bg == bright);
	screen_colours(&s, digits, preview + 1, &fg, &bg); // black as background
	CHECK(fg == blue && bg == black);
}

int main(void) {
	RUN(test_space_and_arrows_change_columns);
	RUN(test_down_selects_next_setting);
	RUN(test_return_saves);
	RUN(test_lone_escape_cancels_after_timeout);
	RUN(test_escape_then_other_key_cancels);
	RUN(test_other_sequences_are_ignored);
	RUN(test_message_cleared_by_next_key);
	RUN(test_draw_marks_current_choice);
	RUN(test_colours_preview);
	return tests_done();
}
