#include <string.h>

#include "term.h"
#include "test.h"

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)

// The terminal uses rows 0..23; row 24 is a status line it must not touch.
#define ROWS (SCREEN_ROWS - 1)
#define STATUS (SCREEN_ROWS - 1)

static screen_t s;
static term_t t;
static unsigned cols = 80; // width for setup(); main() runs the tests at 80 and 132

static void setup(void) {
	screen_init(&s, cols, FG, BG);
	screen_puts(&s, 0, STATUS, "status", FG, BG);
	term_init(&t, &s, 0, ROWS, FG, BG);
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

#define CHECK_CURSOR(c, r) do { \
	CHECK_EQ(s.cursor_col, c); \
	CHECK_EQ(s.cursor_row, r); \
} while (0)

static void test_init(void) {
	setup();
	CHECK(s.cursor_visible);
	CHECK_CURSOR(0, 0);
	CHECK(row_is(STATUS, "status"));
}

static void test_text_and_crlf(void) {
	setup();
	term_puts(&t, "hello\r\nworld");
	CHECK(row_is(0, "hello"));
	CHECK(row_is(1, "world"));
	CHECK_CURSOR(5, 1);
}

static void test_bare_lf_keeps_column(void) {
	setup();
	term_puts(&t, "ab\ncd");
	CHECK(row_is(0, "ab"));
	CHECK(row_is(1, "  cd"));
}

static void test_cr_overwrites(void) {
	setup();
	term_puts(&t, "12345\rab");
	CHECK(row_is(0, "ab345"));
	CHECK_CURSOR(2, 0);
}

static void test_backspace(void) {
	setup();
	term_puts(&t, "abc\b\bX");
	CHECK(row_is(0, "aXc"));
	CHECK_CURSOR(2, 0);
	term_puts(&t, "\b\b\b\b\b");
	CHECK_CURSOR(0, 0); // stops at column 0
}

static void test_tabs(void) {
	setup();
	term_puts(&t, "a\tb\tc");
	CHECK(row_is(0, "a       b       c"));
	for (int i = 0; i < 20; ++i)
		term_putc(&t, '\t');
	CHECK_CURSOR(s.cols - 1, 0); // stops at the last column
}

static void test_wrap_is_deferred(void) {
	setup();
	char line[SCREEN_MAX_COLS + 1];
	memset(line, 'x', s.cols);
	line[s.cols] = 0;
	term_puts(&t, line);
	// Full line: cursor stays in the last column until the next character.
	CHECK_CURSOR(s.cols - 1, 0);
	CHECK(row_is(1, ""));
	term_puts(&t, "y");
	CHECK(row_is(0, line));
	CHECK(row_is(1, "y"));
	CHECK_CURSOR(1, 1);
}

static void test_crlf_after_full_line_does_not_double_space(void) {
	setup();
	char line[SCREEN_MAX_COLS + 1];
	memset(line, '=', s.cols);
	line[s.cols] = 0;
	term_puts(&t, line);
	term_puts(&t, "\r\nnext");
	CHECK(row_is(0, line));
	CHECK(row_is(1, "next"));
}

static void test_scrolls_at_bottom_and_spares_status(void) {
	setup();
	char text[16];
	for (unsigned i = 0; i < ROWS + 5; ++i) {
		snprintf(text, sizeof text, "line %u\r\n", i);
		term_puts(&t, text);
	}
	// 29 lines plus the empty row after the last CR LF fill 30 rows; 24 are
	// visible, so lines 0..5 have scrolled off and the bottom row is empty.
	CHECK(row_is(0, "line 6"));
	CHECK(row_is(ROWS - 2, "line 28"));
	CHECK(row_is(ROWS - 1, ""));
	CHECK_CURSOR(0, ROWS - 1);
	CHECK(row_is(STATUS, "status"));
}

static void test_wrap_scrolls_at_bottom(void) {
	setup();
	for (unsigned i = 0; i < ROWS - 1; ++i)
		term_puts(&t, "\n");
	char line[SCREEN_MAX_COLS + 1];
	memset(line, 'w', s.cols);
	line[s.cols] = 0;
	term_puts(&t, line);
	term_puts(&t, "z");
	CHECK(row_is(ROWS - 2, line));
	CHECK(row_is(ROWS - 1, "z"));
	CHECK(row_is(STATUS, "status"));
}

static void test_controls_and_c1_ignored(void) {
	setup();
	const uint8_t data[] = { 'a', 0x00, 0x07, 0x1b, 0x7f, 0x85, 0x9b, 'b', 0xe9 };
	term_write(&t, data, sizeof data);
	CHECK_EQ(screen_glyph(&s, 0, 0), 'a');
	CHECK_EQ(screen_glyph(&s, 1, 0), 'b');
	CHECK_EQ(screen_glyph(&s, 2, 0), 0xe9); // Latin-1 é
	CHECK_CURSOR(3, 0);
}

static void test_band_offset(void) {
	// A terminal that starts below row 0 writes and scrolls only in its band.
	screen_init(&s, cols, FG, BG);
	screen_puts(&s, 0, 0, "title", FG, BG);
	term_init(&t, &s, 1, 3, FG, BG);
	term_puts(&t, "a\r\nb\r\nc\r\nd");
	CHECK(row_is(0, "title"));
	CHECK(row_is(1, "b"));
	CHECK(row_is(2, "c"));
	CHECK(row_is(3, "d"));
	CHECK(row_is(4, ""));
	CHECK_CURSOR(1, 3);
}

static void run_all(void) {
	RUN(test_init);
	RUN(test_text_and_crlf);
	RUN(test_bare_lf_keeps_column);
	RUN(test_cr_overwrites);
	RUN(test_backspace);
	RUN(test_tabs);
	RUN(test_wrap_is_deferred);
	RUN(test_crlf_after_full_line_does_not_double_space);
	RUN(test_scrolls_at_bottom_and_spares_status);
	RUN(test_wrap_scrolls_at_bottom);
	RUN(test_controls_and_c1_ignored);
	RUN(test_band_offset);
}

int main(void) {
	for (cols = 80; cols <= 132; cols += 52) {
		printf("-- %u columns\n", cols);
		run_all();
	}
	return tests_done();
}
