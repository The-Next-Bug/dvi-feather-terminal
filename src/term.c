#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "palette.h"
#include "term_core.h"
#include "vt100.h"

// ---------------------------------------------------------------------------
// Helpers

unsigned term_cols(const term_t *t) {
	return t->screen->cols;
}

unsigned term_abs_row(const term_t *t, unsigned row) {
	return t->top + row;
}

// Palette index for printed text: bold brightens default text and ANSI 0-7;
// reverse swaps.
static void text_colours(const term_t *t, uint8_t *fg, uint8_t *bg) {
	const term_attr_t *a = &t->cur.attr;
	uint8_t f = a->fg == TERM_DEFAULT_COLOUR ? (a->bold ? PALETTE_BOLD_TEXT : PALETTE_TEXT)
		: (uint8_t)(a->fg < 8 && a->bold ? a->fg + 8 : a->fg);
	uint8_t b = a->bg == TERM_DEFAULT_COLOUR ? PALETTE_BACKGROUND : (uint8_t)a->bg;
	*fg = a->reverse ? b : f;
	*bg = a->reverse ? f : b;
}

void term_erase_colours(const term_t *t, uint8_t *fg, uint8_t *bg) {
	*fg = PALETTE_TEXT;
	*bg = t->cur.attr.bg == TERM_DEFAULT_COLOUR ? PALETTE_BACKGROUND : (uint8_t)t->cur.attr.bg;
}

static void sync_cursor(term_t *t) {
	t->screen->cursor_col = (uint8_t)t->cur.col;
	t->screen->cursor_row = (uint8_t)term_abs_row(t, t->cur.row);
}

void term_reply(term_t *t, const char *fmt, ...) {
	if (!t->reply)
		return;
	char buf[40];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > 0)
		t->reply(t->reply_ctx, (const uint8_t *)buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

// ---------------------------------------------------------------------------
// Cursor and scrolling

void term_move_to(term_t *t, unsigned col, unsigned row) {
	unsigned lo = 0, hi = t->rows - 1;
	if (t->cur.origin_mode) {
		row += t->margin_top;
		lo = t->margin_top;
		hi = t->margin_bottom;
	}
	t->cur.row = row < lo ? lo : row > hi ? hi : row;
	t->cur.col = col < term_cols(t) ? col : term_cols(t) - 1;
	t->cur.wrap_pending = false;
}

void term_scroll_up(term_t *t, unsigned top, unsigned bottom, unsigned n) {
	uint8_t fg, bg;
	term_erase_colours(t, &fg, &bg);
	for (unsigned i = 0; i < n && i <= bottom - top; ++i)
		screen_scroll_up(t->screen, term_abs_row(t, top), term_abs_row(t, bottom), fg, bg);
	if (top == bottom)
		screen_erase(t->screen, term_abs_row(t, top), 0, term_cols(t), fg, bg);
}

void term_scroll_down(term_t *t, unsigned top, unsigned bottom, unsigned n) {
	uint8_t fg, bg;
	term_erase_colours(t, &fg, &bg);
	for (unsigned i = 0; i < n && i <= bottom - top; ++i)
		screen_scroll_down(t->screen, term_abs_row(t, top), term_abs_row(t, bottom), fg, bg);
	if (top == bottom)
		screen_erase(t->screen, term_abs_row(t, top), 0, term_cols(t), fg, bg);
}

void term_cursor_up(term_t *t, unsigned n) {
	unsigned limit = t->cur.row >= t->margin_top ? t->margin_top : 0;
	t->cur.row = t->cur.row >= limit + n ? t->cur.row - n : limit;
	t->cur.wrap_pending = false;
}

void term_cursor_down(term_t *t, unsigned n) {
	unsigned limit = t->cur.row <= t->margin_bottom ? t->margin_bottom : t->rows - 1;
	t->cur.row = t->cur.row + n < limit ? t->cur.row + n : limit;
	t->cur.wrap_pending = false;
}

void term_index_down(term_t *t) {
	if (t->cur.row == t->margin_bottom)
		term_scroll_up(t, t->margin_top, t->margin_bottom, 1);
	else if (t->cur.row + 1 < t->rows)
		++t->cur.row;
}

void term_index_up(term_t *t) {
	if (t->cur.row == t->margin_top)
		term_scroll_down(t, t->margin_top, t->margin_bottom, 1);
	else if (t->cur.row > 0)
		--t->cur.row;
}

// ---------------------------------------------------------------------------
// Printing

static uint8_t map_charset(const term_t *t, uint8_t ch) {
	if (ch >= 0xa0)
		return ch; // Latin-1
	uint8_t set = t->cur.charset[t->cur.gl];
	if (set == '0' && ch >= 0x5f && ch <= 0x7e)
		return ch == 0x5f ? ' ' : GLYPH_DEC_GRAPHICS(ch);
	if (set == 'A' && ch == '#')
		return 0xa3; // UK: pound sign
	return ch;
}

void term_print(term_t *t, uint8_t ch) {
	if (ch == 0x7f)
		return;
	uint8_t glyph = map_charset(t, ch);
	if (t->cur.wrap_pending) {
		t->cur.col = 0;
		term_index_down(t);
		t->cur.wrap_pending = false;
	}
	uint8_t fg, bg;
	text_colours(t, &fg, &bg);
	unsigned row = term_abs_row(t, t->cur.row);
	uint8_t attr = (t->cur.attr.underline ? SCREEN_ATTR_UNDERLINE : 0) |
		(t->cur.attr.blink ? SCREEN_ATTR_BLINK : 0) | (t->cur.attr.bold ? SCREEN_ATTR_BOLD : 0);
	if (t->insert_mode)
		screen_insert_cells(t->screen, row, t->cur.col, 1, fg, bg);
	screen_put_attr(t->screen, t->cur.col, row, glyph, fg, bg, attr);
	if (t->cur.col + 1 < term_cols(t))
		++t->cur.col;
	else if (t->autowrap)
		t->cur.wrap_pending = true;
}

// ---------------------------------------------------------------------------
// Tabs

void term_set_tab(term_t *t, unsigned col, bool on) {
	if (on)
		t->tabs[col / 8] |= (uint8_t)(1u << col % 8);
	else
		t->tabs[col / 8] &= (uint8_t)~(1u << col % 8);
}

static bool is_tab(const term_t *t, unsigned col) {
	return t->tabs[col / 8] >> col % 8 & 1;
}

void term_reset_tabs(term_t *t) {
	memset(t->tabs, 0, sizeof t->tabs);
	for (unsigned col = 8; col < SCREEN_MAX_COLS; col += 8)
		term_set_tab(t, col, true);
}

void term_tab(term_t *t) {
	unsigned col = t->cur.col + 1;
	while (col < term_cols(t) - 1 && !is_tab(t, col))
		++col;
	t->cur.col = col < term_cols(t) ? col : term_cols(t) - 1;
	t->cur.wrap_pending = false;
}

// ---------------------------------------------------------------------------
// Erasing and line insert/delete

static void clear_band(term_t *t) {
	uint8_t fg, bg;
	term_erase_colours(t, &fg, &bg);
	for (unsigned row = 0; row < t->rows; ++row)
		screen_erase(t->screen, term_abs_row(t, row), 0, term_cols(t), fg, bg);
}

void term_erase_display(term_t *t, unsigned how) {
	uint8_t fg, bg;
	term_erase_colours(t, &fg, &bg);
	unsigned row = t->cur.row;
	if (how == 0) {
		screen_erase(t->screen, term_abs_row(t, row), t->cur.col, term_cols(t), fg, bg);
		for (unsigned r = row + 1; r < t->rows; ++r)
			screen_erase(t->screen, term_abs_row(t, r), 0, term_cols(t), fg, bg);
	} else if (how == 1) {
		for (unsigned r = 0; r < row; ++r)
			screen_erase(t->screen, term_abs_row(t, r), 0, term_cols(t), fg, bg);
		screen_erase(t->screen, term_abs_row(t, row), 0, t->cur.col + 1, fg, bg);
	} else if (how == 2) {
		clear_band(t);
	}
	t->cur.wrap_pending = false;
}

void term_erase_line(term_t *t, unsigned how) {
	uint8_t fg, bg;
	term_erase_colours(t, &fg, &bg);
	unsigned row = term_abs_row(t, t->cur.row);
	if (how == 0)
		screen_erase(t->screen, row, t->cur.col, term_cols(t), fg, bg);
	else if (how == 1)
		screen_erase(t->screen, row, 0, t->cur.col + 1, fg, bg);
	else if (how == 2)
		screen_erase(t->screen, row, 0, term_cols(t), fg, bg);
	t->cur.wrap_pending = false;
}

static bool in_region(const term_t *t) {
	return t->cur.row >= t->margin_top && t->cur.row <= t->margin_bottom;
}

void term_insert_lines(term_t *t, unsigned n) {
	if (in_region(t)) {
		term_scroll_down(t, t->cur.row, t->margin_bottom, n);
		t->cur.col = 0;
		t->cur.wrap_pending = false;
	}
}

void term_delete_lines(term_t *t, unsigned n) {
	if (in_region(t)) {
		term_scroll_up(t, t->cur.row, t->margin_bottom, n);
		t->cur.col = 0;
		t->cur.wrap_pending = false;
	}
}

// ---------------------------------------------------------------------------
// Reset and modes

void term_reset_cursor(term_cursor_t *c) {
	*c = (term_cursor_t){
		.attr = { .fg = TERM_DEFAULT_COLOUR, .bg = TERM_DEFAULT_COLOUR },
		.charset = { 'B', 'B' },
	};
}

// The personality's own state, as RIS leaves it.
static void personality_reset(term_t *t) {
	switch (t->personality) {
	case TERM_VT100: vt100_reset(t); break;
	}
}

static void reset(term_t *t) {
	term_reset_cursor(&t->cur);
	t->margin_top = 0;
	t->margin_bottom = t->rows - 1;
	t->autowrap = true;
	t->insert_mode = false;
	t->newline_mode = t->defaults.newline_mode;
	t->backspace_bs = t->defaults.backspace_bs;
	term_reset_tabs(t);
	personality_reset(t);
	t->screen->cursor_visible = true;
	term_set_reverse_screen(t, false);
	clear_band(t);
}

// DECSCNM: show the terminal's rows with fg and bg swapped.
void term_set_reverse_screen(term_t *t, bool on) {
	t->reverse_screen = on;
	uint32_t band = ((1u << t->rows) - 1) << t->top;
	uint32_t mask = on ? t->screen->reverse_rows | band : t->screen->reverse_rows & ~band;
	screen_set_reverse_rows(t->screen, mask);
}

// DECCOLM: switch between 80 and 132 columns. This clears the whole screen,
// also when the width does not change (status line included; its owner
// redraws it when it sees screen_resets change).
static void set_columns(term_t *t, unsigned n) {
	bool visible = t->screen->cursor_visible;
	screen_init(t->screen, n, PALETTE_TEXT, PALETTE_BACKGROUND);
	++t->screen_resets;
	t->screen->cursor_visible = visible;
	term_set_reverse_screen(t, t->reverse_screen);
	t->margin_top = 0;
	t->margin_bottom = t->rows - 1;
	term_move_to(t, 0, 0);
}

// ---------------------------------------------------------------------------
// API

void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows) {
	*t = (term_t){ .screen = screen, .top = top, .rows = rows, .personality = TERM_VT100 };
	vt100_init(t);
	reset(t);
	sync_cursor(t);
}

// From RIS this runs inside the personality's parser; vt100_cancel allows
// that.
void term_reset(term_t *t) {
	switch (t->personality) {
	case TERM_VT100: vt100_cancel(t); break;
	}
	reset(t);
	sync_cursor(t);
}

void term_set_reply(term_t *t, term_reply_t reply_fn, void *ctx) {
	t->reply = reply_fn;
	t->reply_ctx = ctx;
}

// The screen's cursor is updated once per call, not per byte.
size_t term_write(term_t *t, const uint8_t *data, size_t len) {
	unsigned setup_requests = t->setup_requests;
	size_t i = 0;
	while (i < len && t->setup_requests == setup_requests) {
		switch (t->personality) {
		case TERM_VT100: vt100_byte(t, data[i]); break;
		}
		++i;
	}
	sync_cursor(t);
	return i;
}

void term_set_defaults(term_t *t, const term_defaults_t *d) {
	t->defaults = *d;
	t->backspace_bs = d->backspace_bs;
	t->newline_mode = d->newline_mode;
}

void term_set_columns(term_t *t, unsigned cols) {
	set_columns(t, cols);
	sync_cursor(t);
}

void term_putc(term_t *t, uint8_t c) {
	term_write(t, &c, 1);
}

void term_puts(term_t *t, const char *text) {
	term_write(t, (const uint8_t *)text, strlen(text));
}
