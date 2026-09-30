#include <string.h>

#include "font.h"
#include "term.h"
#include "test.h"

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)

// The terminal uses rows 0..23; row 24 is a status line it must not touch.
#define ROWS (SCREEN_ROWS - 1)
#define STATUS (SCREEN_ROWS - 1)

static screen_t s;
static term_t t;
static unsigned cols = 80; // width for setup(); some tests run at 80 and 132

static char replies[256];

static void on_reply(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	size_t room = sizeof replies - strlen(replies) - 1;
	strncat(replies, (const char *)data, len < room ? len : room);
}

static void setup(void) {
	screen_init(&s, cols, FG, BG);
	screen_puts(&s, 0, STATUS, "status", FG, BG);
	term_init(&t, &s, 0, ROWS, FG, BG);
	term_set_reply(&t, on_reply, NULL);
	replies[0] = 0;
}

static void put(const char *text) {
	term_puts(&t, text);
}

// Compare the text of a screen row (trailing blanks ignored) with expected.
static int row_is(unsigned row, const char *expected) {
	size_t len = strlen(expected);
	for (unsigned col = 0; col < s.cols; ++col) {
		uint8_t want = col < len ? (uint8_t)expected[col] : ' ';
		if (screen_glyph(&s, col, row) != want) {
			fprintf(stderr, "row %u col %u: got 0x%02x want 0x%02x\n",
				row, col, screen_glyph(&s, col, row), want);
			return 0;
		}
	}
	return 1;
}

static uint8_t fg_at(unsigned col, unsigned row) {
	uint8_t fg, bg;
	screen_colours(&s, col, row, &fg, &bg);
	return fg;
}

static uint8_t bg_at(unsigned col, unsigned row) {
	uint8_t fg, bg;
	screen_colours(&s, col, row, &fg, &bg);
	return bg;
}

#define CHECK_CURSOR(c, r) do { \
	CHECK_EQ(s.cursor_col, c); \
	CHECK_EQ(s.cursor_row, r); \
} while (0)

#define CHECK_REPLY(want) do { \
	if (strcmp(replies, want) != 0) { \
		fprintf(stderr, "%s:%d: reply \"%s\" want \"%s\"\n", __FILE__, __LINE__, replies, want); \
		++test_failures; \
	} \
	replies[0] = 0; \
} while (0)

// Fill every terminal row with "row N" so movement is visible.
static void number_rows(void) {
	char text[24];
	for (unsigned r = 0; r < ROWS; ++r) {
		snprintf(text, sizeof text, "\x1b[%u;1Hrow %u", r + 1, r);
		put(text);
	}
}

// ---------------------------------------------------------------------------
// Teletype basics (run at 80 and 132 columns)

static void test_init(void) {
	setup();
	CHECK(s.cursor_visible);
	CHECK_CURSOR(0, 0);
	CHECK(row_is(STATUS, "status"));
}

static void test_text_and_crlf(void) {
	setup();
	put("hello\r\nworld");
	CHECK(row_is(0, "hello"));
	CHECK(row_is(1, "world"));
	CHECK_CURSOR(5, 1);
}

static void test_bare_lf_keeps_column(void) {
	setup();
	put("ab\ncd");
	CHECK(row_is(1, "  cd"));
}

static void test_backspace_and_cr(void) {
	setup();
	put("12345\rab\b\bX");
	CHECK(row_is(0, "Xb345")); // backspace moves, it does not erase
	CHECK_CURSOR(1, 0);
	put("\b\b\b");
	CHECK_CURSOR(0, 0);
}

static void test_tabs(void) {
	setup();
	put("a\tb\tc");
	CHECK(row_is(0, "a       b       c"));
	for (int i = 0; i < 20; ++i)
		put("\t");
	CHECK_CURSOR(s.cols - 1, 0);
}

static void test_wrap_is_deferred(void) {
	setup();
	char line[SCREEN_MAX_COLS + 1];
	memset(line, 'x', s.cols);
	line[s.cols] = 0;
	put(line);
	CHECK_CURSOR(s.cols - 1, 0);
	CHECK(row_is(1, ""));
	put("y");
	CHECK(row_is(0, line));
	CHECK(row_is(1, "y"));
	// A CR LF after a full line does not double-space.
	setup();
	put(line);
	put("\r\nnext");
	CHECK(row_is(1, "next"));
}

static void test_scrolls_at_bottom_and_spares_status(void) {
	setup();
	char text[16];
	for (unsigned i = 0; i < ROWS + 5; ++i) {
		snprintf(text, sizeof text, "line %u\r\n", i);
		put(text);
	}
	CHECK(row_is(0, "line 6"));
	CHECK(row_is(ROWS - 2, "line 28"));
	CHECK(row_is(ROWS - 1, ""));
	CHECK(row_is(STATUS, "status"));
}

static void test_band_offset(void) {
	screen_init(&s, cols, FG, BG);
	screen_puts(&s, 0, 0, "title", FG, BG);
	term_init(&t, &s, 1, 3, FG, BG);
	put("a\r\nb\r\nc\r\nd\x1b[H*");
	CHECK(row_is(0, "title"));
	CHECK(row_is(1, "*"));
	CHECK(row_is(2, "c"));
	CHECK(row_is(3, "d"));
	CHECK(row_is(4, ""));
}

// ---------------------------------------------------------------------------
// Cursor movement

static void test_cup(void) {
	setup();
	put("\x1b[5;10H");
	CHECK_CURSOR(9, 4);
	put("\x1b[H");
	CHECK_CURSOR(0, 0);
	put("\x1b[;7H");
	CHECK_CURSOR(6, 0);
	put("\x1b[99;999f");
	CHECK_CURSOR(s.cols - 1, ROWS - 1);
	put("\x1b[3d");
	CHECK_CURSOR(s.cols - 1, 2);
	put("\x1b[4G");
	CHECK_CURSOR(3, 2);
}

static void test_relative_moves(void) {
	setup();
	put("\x1b[10;10H\x1b[3A");
	CHECK_CURSOR(9, 6);
	put("\x1b[B");
	CHECK_CURSOR(9, 7);
	put("\x1b[5C\x1b[2D");
	CHECK_CURSOR(12, 7);
	put("\x1b[99A\x1b[99D");
	CHECK_CURSOR(0, 0);
	put("\x1b[99B\x1b[999C");
	CHECK_CURSOR(s.cols - 1, ROWS - 1);
	put("\x1b[2F");
	CHECK_CURSOR(0, ROWS - 3);
	put("\x1b[E");
	CHECK_CURSOR(0, ROWS - 2);
}

static void test_moves_clear_pending_wrap(void) {
	setup();
	put("\x1b[1;80H" "a\x1b[D" "b");
	CHECK_EQ(screen_glyph(&s, 78, 0), 'b');
	CHECK(row_is(1, ""));
}

// ---------------------------------------------------------------------------
// Erasing

static void test_erase_in_line(void) {
	setup();
	put("abcdef\x1b[1;3H\x1b[K");
	CHECK(row_is(0, "ab"));
	put("\r\x1b[2Cxyz\x1b[1;4H\x1b[1K");
	CHECK(row_is(0, "    z"));
	put("\x1b[2K");
	CHECK(row_is(0, ""));
	CHECK_CURSOR(3, 0);
}

static void test_erase_in_display(void) {
	setup();
	number_rows();
	put("\x1b[5;3H\x1b[J");
	CHECK(row_is(3, "row 3"));
	CHECK(row_is(4, "ro"));
	CHECK(row_is(5, ""));
	CHECK(row_is(ROWS - 1, ""));

	setup();
	number_rows();
	put("\x1b[5;3H\x1b[1J");
	CHECK(row_is(3, ""));
	CHECK(row_is(4, "    4")); // columns 0-2 erased
	CHECK(row_is(5, "row 5"));

	setup();
	number_rows();
	put("\x1b[2J");
	for (unsigned r = 0; r < ROWS; ++r)
		CHECK(row_is(r, ""));
	CHECK(row_is(STATUS, "status"));
}

static void test_erase_uses_current_background(void) {
	setup();
	put("\x1b[44m\x1b[2J\x1b[0m");
	CHECK_EQ(bg_at(0, 0), term_ansi_colour(4));
	CHECK_EQ(bg_at(s.cols - 1, ROWS - 1), term_ansi_colour(4));
	CHECK_EQ(bg_at(0, STATUS), BG);
}

static void test_erase_characters(void) {
	setup();
	put("abcdefgh\x1b[1;3H\x1b[3X");
	CHECK(row_is(0, "ab   fgh"));
	CHECK_CURSOR(2, 0);
}

// ---------------------------------------------------------------------------
// Scrolling regions, insert and delete

static void test_scroll_region(void) {
	setup();
	number_rows();
	put("\x1b[5;10r"); // rows 4..9 (0-based)
	CHECK_CURSOR(0, 0); // DECSTBM homes the cursor
	put("\x1b[10;1H\n");
	CHECK(row_is(3, "row 3"));
	CHECK(row_is(4, "row 5"));
	CHECK(row_is(8, "row 9"));
	CHECK(row_is(9, ""));
	CHECK(row_is(10, "row 10"));
	CHECK_CURSOR(0, 9);

	put("\x1b[5;1H\x1bM"); // RI at the top margin scrolls down
	CHECK(row_is(4, ""));
	CHECK(row_is(5, "row 5"));
	CHECK(row_is(3, "row 3"));

	// Below the region, LF moves down to the last row but never scrolls.
	put("\x1b[20;1H\n\n\n\n\n\n");
	CHECK_CURSOR(0, ROWS - 1);
	CHECK(row_is(ROWS - 1, "row 23"));
}

static void test_insert_delete_lines(void) {
	setup();
	number_rows();
	put("\x1b[3;8r\x1b[4;5H\x1b[2L"); // region rows 2..7
	CHECK_CURSOR(0, 3);
	CHECK(row_is(2, "row 2"));
	CHECK(row_is(3, ""));
	CHECK(row_is(4, ""));
	CHECK(row_is(5, "row 3"));
	CHECK(row_is(7, "row 5"));
	CHECK(row_is(8, "row 8"));

	put("\x1b[M");
	CHECK(row_is(3, ""));
	CHECK(row_is(4, "row 3"));
	CHECK(row_is(7, ""));
	CHECK(row_is(8, "row 8"));

	// Outside the region: no effect.
	put("\x1b[12;1H\x1b[L");
	CHECK(row_is(11, "row 11"));
}

static void test_insert_delete_characters(void) {
	setup();
	put("abcdef\x1b[1;3H\x1b[2@");
	CHECK(row_is(0, "ab  cdef"));
	put("\x1b[3P");
	CHECK(row_is(0, "abdef"));
	CHECK_CURSOR(2, 0);
}

static void test_insert_mode(void) {
	setup();
	put("abcd\x1b[1;2H\x1b[4hXY\x1b[4lZ");
	CHECK(row_is(0, "aXYZcd"));
}

// ---------------------------------------------------------------------------
// Modes

static void test_autowrap_off(void) {
	setup();
	put("\x1b[?7l\x1b[1;79Habcd");
	CHECK_EQ(screen_glyph(&s, 78, 0), 'a');
	CHECK_EQ(screen_glyph(&s, 79, 0), 'd');
	CHECK(row_is(1, ""));
}

static void test_origin_mode(void) {
	setup();
	put("\x1b[5;10r\x1b[?6h");
	CHECK_CURSOR(0, 4);
	put("\x1b[2;3H");
	CHECK_CURSOR(2, 5);
	put("\x1b[99;1H");
	CHECK_CURSOR(0, 9); // clamped to the region
	put("\x1b[6n");
	CHECK_REPLY("\x1b[6;1R"); // reported relative to the region
	put("\x1b[?6l");
	CHECK_CURSOR(0, 0);
}

static void test_newline_mode(void) {
	setup();
	put("ab\x1b[20h\ncd\x1b[20l\nef");
	CHECK(row_is(1, "cd"));
	CHECK(row_is(2, "  ef"));
}

static void test_cursor_visibility(void) {
	setup();
	put("\x1b[?25l");
	CHECK(!s.cursor_visible);
	put("\x1b[?25h");
	CHECK(s.cursor_visible);
}

static void test_deccolm(void) {
	setup();
	put("hello\x1b[?3h");
	CHECK_EQ(s.cols, 132);
	CHECK(row_is(0, ""));
	CHECK_CURSOR(0, 0);
	put("\x1b[1;200Hx");
	CHECK_EQ(screen_glyph(&s, 131, 0), 'x');
	put("\x1b[?3l");
	CHECK_EQ(s.cols, 80);
	CHECK_EQ(t.screen_resets, 2);

	// The same width again still clears everything, status line included,
	// and says so.
	screen_puts(&s, 0, STATUS, "status", FG, BG);
	put("hello\x1b[?3l");
	CHECK(row_is(0, ""));
	CHECK(row_is(STATUS, ""));
	CHECK_EQ(t.screen_resets, 3);
}

// ---------------------------------------------------------------------------
// Save/restore, character sets, tabs, reset

static void test_save_restore_cursor(void) {
	setup();
	put("\x1b[5;6H\x1b[31m\x1b(0\x1b" "7\x1b[H\x1b[0m\x1b(B" "a\x1b" "8q");
	CHECK_CURSOR(6, 4);
	CHECK_EQ(screen_glyph(&s, 5, 4), GLYPH_DEC_GRAPHICS('q')); // charset restored
	CHECK_EQ(fg_at(5, 4), term_ansi_colour(1));               // colour restored
	CHECK_EQ(screen_glyph(&s, 0, 0), 'a');
}

static void test_charsets(void) {
	setup();
	put("\x1b(0lqk\x1b(B" "lqk");
	CHECK_EQ(screen_glyph(&s, 0, 0), GLYPH_DEC_GRAPHICS('l'));
	CHECK_EQ(screen_glyph(&s, 1, 0), GLYPH_DEC_GRAPHICS('q'));
	CHECK_EQ(screen_glyph(&s, 3, 0), 'l');
	// G1 = DEC graphics, invoked with SO, back with SI.
	put("\r\n\x1b)0x\x0ex\x0fx");
	CHECK_EQ(screen_glyph(&s, 0, 1), 'x');
	CHECK_EQ(screen_glyph(&s, 1, 1), GLYPH_DEC_GRAPHICS('x'));
	CHECK_EQ(screen_glyph(&s, 2, 1), 'x');
	// UK set: # is a pound sign. Latin-1 bytes print directly.
	put("\r\n\x1b(A#\x1b(B#\xe9");
	CHECK_EQ(screen_glyph(&s, 0, 2), 0xa3);
	CHECK_EQ(screen_glyph(&s, 1, 2), '#');
	CHECK_EQ(screen_glyph(&s, 2, 2), 0xe9);
}

static void test_tab_stops(void) {
	setup();
	// Clear all stops, set stops at columns 4 and 19.
	put("\x1b[3g\x1b[1;5H\x1bH\x1b[1;20H\x1bH\r\tx\tx\tx");
	CHECK_EQ(screen_glyph(&s, 4, 0), 'x');
	CHECK_EQ(screen_glyph(&s, 19, 0), 'x');
	CHECK_EQ(screen_glyph(&s, s.cols - 1, 0), 'x'); // no more stops
	// Clear the stop at column 4: the first tab now goes to 19.
	put("\x1b[2;5H\x1b[g\r\ty");
	CHECK_EQ(screen_glyph(&s, 19, 1), 'y');
}

static void test_decaln(void) {
	setup();
	put("\x1b[5;10r\x1b#8");
	CHECK_CURSOR(0, 0);
	for (unsigned r = 0; r < ROWS; ++r)
		CHECK_EQ(screen_glyph(&s, s.cols - 1, r), 'E');
	CHECK(row_is(STATUS, "status"));
}

static void test_reset(void) {
	setup();
	put("\x1b[5;10r\x1b[31mhello\x1b[?25l\x1b" "c");
	CHECK(row_is(0, ""));
	CHECK_CURSOR(0, 0);
	CHECK(s.cursor_visible);
	put("\x1b[99B");
	CHECK_CURSOR(0, ROWS - 1); // margins reset
	put("x");
	CHECK_EQ(fg_at(0, ROWS - 1), FG);
}

// ---------------------------------------------------------------------------
// SGR

static void test_sgr_colours(void) {
	setup();
	put("\x1b[31ma\x1b[1mb\x1b[0mc\x1b[42;37md\x1b[7me\x1b[27;39;49mf");
	CHECK_EQ(fg_at(0, 0), term_ansi_colour(1));
	CHECK_EQ(fg_at(1, 0), term_ansi_colour(9)); // bold brightens
	CHECK_EQ(fg_at(2, 0), FG);
	CHECK_EQ(bg_at(2, 0), BG);
	CHECK_EQ(fg_at(3, 0), term_ansi_colour(7));
	CHECK_EQ(bg_at(3, 0), term_ansi_colour(2));
	CHECK_EQ(fg_at(4, 0), term_ansi_colour(2)); // reverse
	CHECK_EQ(bg_at(4, 0), term_ansi_colour(7));
	CHECK_EQ(fg_at(5, 0), FG);
	CHECK_EQ(bg_at(5, 0), BG);
	put("\x1b[1mg\x1b[22;95mh\x1b[104mi\x1b[m");
	CHECK_EQ(fg_at(6, 0), RGB222(3, 3, 3)); // bold default: white
	CHECK_EQ(fg_at(7, 0), term_ansi_colour(13));
	CHECK_EQ(bg_at(8, 0), term_ansi_colour(12));
}

static void test_sgr_underline_blink(void) {
	setup();
	put("a\x1b[4mb\x1b[5mc\x1b[24md\x1b[0me");
	CHECK_EQ(screen_attr(&s, 0, 0), 0);
	CHECK_EQ(screen_attr(&s, 1, 0), SCREEN_ATTR_UNDERLINE);
	CHECK_EQ(screen_attr(&s, 2, 0), SCREEN_ATTR_UNDERLINE | SCREEN_ATTR_BLINK);
	CHECK_EQ(screen_attr(&s, 3, 0), SCREEN_ATTR_BLINK);
	CHECK_EQ(screen_attr(&s, 4, 0), 0);
	put("\x1b[1mB\x1b[22mn");
	CHECK_EQ(screen_attr(&s, 5, 0), SCREEN_ATTR_BOLD);
	CHECK_EQ(screen_attr(&s, 6, 0), 0);
	// Erasing clears attributes, even with SGR attributes in effect.
	put("\x1b[4m\x1b[1;1H\x1b[K\x1b[0m");
	CHECK_EQ(screen_attr(&s, 1, 0), 0);
}

static void test_reverse_screen(void) {
	setup();
	put("\x1b[?5h");
	CHECK_EQ(s.reverse_rows, (1u << ROWS) - 1); // the terminal's rows, not the status line
	put("\x1b[?3h"); // DECCOLM keeps it
	CHECK_EQ(s.reverse_rows, (1u << ROWS) - 1);
	put("\x1b[?5l");
	CHECK_EQ(s.reverse_rows, 0);
	put("\x1b[?5h\x1b" "c"); // RIS clears it
	CHECK_EQ(s.reverse_rows, 0);
}

static void test_sgr_extended_colours(void) {
	setup();
	put("\x1b[38;5;196ma\x1b[48;5;21mb\x1b[38;2;255;128;0mc\x1b[38;5;244md\x1b[38;5;3me\x1b[0m");
	CHECK_EQ(fg_at(0, 0), RGB222(3, 0, 0));
	CHECK_EQ(bg_at(1, 0), RGB222(0, 0, 3));
	CHECK_EQ(fg_at(2, 0), RGB222(3, 2, 0));
	CHECK_EQ(fg_at(3, 0), RGB222(2, 2, 2));
	CHECK_EQ(fg_at(4, 0), term_ansi_colour(3));
}

static void test_rep(void) {
	setup();
	put("ab\x1b[3bc\x1b[b");
	CHECK(row_is(0, "abbbbcc"));
	// Only right after a printed character; SGR in between cancels it.
	put("\r\nx\x1b[0m\x1b[5b");
	CHECK(row_is(1, "x"));
	// Repeats through the charset in effect, and wraps like printing.
	put("\x1b[3;1H\x1b(0q\x1b[2b\x1b(B");
	CHECK_EQ(screen_glyph(&s, 2, 2), GLYPH_DEC_GRAPHICS('q'));
	put("\x1b[4;79Hz\x1b[3b");
	CHECK_EQ(screen_glyph(&s, 79, 3), 'z');
	CHECK_EQ(screen_glyph(&s, 1, 4), 'z');
}

// ---------------------------------------------------------------------------
// Replies

static void test_replies(void) {
	setup();
	put("\x1b[c");
	CHECK_REPLY("\x1b[?1;2c");
	put("\x1b[0c\x1bZ");
	CHECK_REPLY("\x1b[?1;2c\x1b[?1;2c");
	put("\x1b[5n");
	CHECK_REPLY("\x1b[0n");
	put("\x1b[7;12H\x1b[6n");
	CHECK_REPLY("\x1b[7;12R");
	put("\x1b[x");
	CHECK_REPLY("\x1b[2;1;1;128;128;1;0x");
	put("\x1b[>c\x1b[99n");
	CHECK_REPLY("");
}

// ---------------------------------------------------------------------------
// Robustness

static void test_unknown_and_aborted_sequences(void) {
	setup();
	put("a\x1b[12;34zb\x1b[1\x18" "c\x1b]0;title\x07" "d\x9b" "1me");
	CHECK(row_is(0, "abcd1me"));
	CHECK_EQ(fg_at(3, 0), FG);
}

static void test_random_input_is_safe(void) {
	// Garbage must never write outside the screen (ASan/UBSan) or leave the
	// cursor out of range.
	static const char interesting[] = "\x1b[;?0123456789HJKLMPr@mhl\r\n\b\t";
	unsigned seed = 12345;
	for (cols = 80; cols <= 132; cols += 52) {
		setup();
		for (unsigned i = 0; i < 200000; ++i) {
			seed = seed * 1103515245u + 12345u;
			uint8_t b = (uint8_t)(seed >> 16);
			if ((seed >> 8 & 7) == 0) // bias towards sequence bytes
				b = (uint8_t)interesting[(seed >> 24) % (sizeof interesting - 1)];
			term_putc(&t, b);
			if (s.cursor_col >= s.cols || s.cursor_row >= ROWS) {
				CHECK(!"cursor out of range");
				break;
			}
		}
	}
	cols = 80;
}

static void run_basics(void) {
	RUN(test_init);
	RUN(test_text_and_crlf);
	RUN(test_bare_lf_keeps_column);
	RUN(test_backspace_and_cr);
	RUN(test_tabs);
	RUN(test_wrap_is_deferred);
	RUN(test_scrolls_at_bottom_and_spares_status);
	RUN(test_band_offset);
}

// CSI ? 999 ~ asks for SET-UP; term_write stops right after it.
static void test_setup_request(void) {
	setup();
	const char *data = "ab\x1b[?999~cd";
	size_t len = strlen(data);
	size_t used = term_write(&t, (const uint8_t *)data, len);
	CHECK_EQ(used, len - 2);
	CHECK_EQ(t.setup_requests, 1);
	CHECK(row_is(0, "ab"));
	// Other parameters, or a different marker, are not requests.
	put("\x1b[?998~\x1b[999~\x1b[?999;1~");
	CHECK_EQ(t.setup_requests, 1);
	CHECK(row_is(0, "ab"));
}

// DECBKM (CSI ? 67 h/l) sets what Backspace sends; RIS goes back to the
// SET-UP default.
static void test_decbkm(void) {
	setup();
	CHECK(!t.backspace_bs);
	put("\x1b[?67h");
	CHECK(t.backspace_bs);
	put("\x1b[?67l");
	CHECK(!t.backspace_bs);
	term_set_backspace_default(&t, true);
	CHECK(t.backspace_bs);
	put("\x1b[?67l\x1b" "c"); // RIS
	CHECK(t.backspace_bs);
}

static void test_set_columns(void) {
	setup();
	put("hello");
	term_set_columns(&t, 132);
	CHECK_EQ(s.cols, 132);
	CHECK_EQ(t.screen_resets, 1);
	CHECK(row_is(0, ""));
	CHECK_EQ(s.cursor_col, 0);
	CHECK_EQ(s.cursor_row, 0);
}

int main(void) {
	for (cols = 80; cols <= 132; cols += 52) {
		printf("-- basics, %u columns\n", cols);
		run_basics();
	}
	cols = 80;
	printf("-- VT100, 80 columns\n");
	RUN(test_cup);
	RUN(test_relative_moves);
	RUN(test_moves_clear_pending_wrap);
	RUN(test_erase_in_line);
	RUN(test_erase_in_display);
	RUN(test_erase_uses_current_background);
	RUN(test_erase_characters);
	RUN(test_scroll_region);
	RUN(test_insert_delete_lines);
	RUN(test_insert_delete_characters);
	RUN(test_insert_mode);
	RUN(test_autowrap_off);
	RUN(test_origin_mode);
	RUN(test_newline_mode);
	RUN(test_cursor_visibility);
	RUN(test_deccolm);
	RUN(test_setup_request);
	RUN(test_set_columns);
	RUN(test_decbkm);
	RUN(test_save_restore_cursor);
	RUN(test_charsets);
	RUN(test_tab_stops);
	RUN(test_decaln);
	RUN(test_reset);
	RUN(test_sgr_colours);
	RUN(test_sgr_extended_colours);
	RUN(test_sgr_underline_blink);
	RUN(test_reverse_screen);
	RUN(test_rep);
	RUN(test_replies);
	RUN(test_unknown_and_aborted_sequences);
	RUN(test_random_input_is_safe);
	return tests_done();
}
