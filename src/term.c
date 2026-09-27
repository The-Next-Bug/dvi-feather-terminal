#include "term.h"

#include <string.h>

#define BS 0x08
#define HT 0x09
#define LF 0x0a
#define VT 0x0b
#define FF 0x0c
#define CR 0x0d

static void sync_cursor(term_t *t) {
	t->screen->cursor_col = (uint8_t)t->col;
	t->screen->cursor_row = (uint8_t)(t->top + t->row);
}

static void line_feed(term_t *t) {
	if (t->row + 1 < t->rows)
		++t->row;
	else
		screen_scroll_up(t->screen, t->top, t->top + t->rows - 1, t->fg, t->bg);
}

static void print(term_t *t, uint8_t glyph) {
	if (t->wrap_pending) {
		t->col = 0;
		line_feed(t);
		t->wrap_pending = false;
	}
	screen_put(t->screen, t->col, t->top + t->row, glyph, t->fg, t->bg);
	if (t->col + 1 < SCREEN_COLS)
		++t->col;
	else
		t->wrap_pending = true;
}

void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows, uint8_t fg, uint8_t bg) {
	*t = (term_t){ .screen = screen, .top = top, .rows = rows, .fg = fg, .bg = bg };
	for (unsigned row = 0; row < rows; ++row)
		screen_clear_line(screen, top + row, fg, bg);
	screen->cursor_visible = true;
	sync_cursor(t);
}

static void control(term_t *t, uint8_t c) {
	switch (c) {
	case CR:
		t->col = 0;
		t->wrap_pending = false;
		break;
	case LF:
	case VT:
	case FF:
		line_feed(t);
		t->wrap_pending = false;
		break;
	case BS:
		if (t->col > 0 && !t->wrap_pending)
			--t->col;
		t->wrap_pending = false;
		break;
	case HT:
		t->col = (t->col / TERM_TAB_WIDTH + 1) * TERM_TAB_WIDTH;
		if (t->col >= SCREEN_COLS)
			t->col = SCREEN_COLS - 1;
		break;
	default: // BEL, NUL and the rest are ignored for now.
		break;
	}
}

static void putc_nosync(term_t *t, uint8_t c) {
	if (c < 0x20)
		control(t, c);
	else if (c < 0x7f || c >= 0xa0) // ASCII and Latin-1 map straight to glyphs
		print(t, c);
	// DEL and C1 controls (0x80-0x9f) are ignored.
}

// The screen's cursor is updated once per call, not per byte.
void term_write(term_t *t, const uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; ++i)
		putc_nosync(t, data[i]);
	sync_cursor(t);
}

void term_putc(term_t *t, uint8_t c) {
	term_write(t, &c, 1);
}

void term_puts(term_t *t, const char *text) {
	term_write(t, (const uint8_t *)text, strlen(text));
}
