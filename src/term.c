#include "term.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "font.h"

#define BEL 0x07
#define BS 0x08
#define HT 0x09
#define LF 0x0a
#define VT 0x0b
#define FF 0x0c
#define CR 0x0d
#define SO 0x0e
#define SI 0x0f

// Attribute fg/bg values 16-79 are direct RGB222 colours (from SGR 38/48).
#define DIRECT_COLOUR(rgb) ((uint8_t)(16 + (rgb)))

// ---------------------------------------------------------------------------
// Helpers

static unsigned cols(const term_t *t) {
	return t->screen->cols;
}

static unsigned abs_row(const term_t *t, unsigned row) {
	return t->top + row;
}

uint8_t term_ansi_colour(unsigned n) {
	static const uint8_t palette[16] = {
		RGB222(0, 0, 0), RGB222(2, 0, 0), RGB222(0, 2, 0), RGB222(2, 2, 0),
		RGB222(0, 0, 2), RGB222(2, 0, 2), RGB222(0, 2, 2), RGB222(2, 2, 2),
		RGB222(1, 1, 1), RGB222(3, 0, 0), RGB222(0, 3, 0), RGB222(3, 3, 0),
		RGB222(0, 0, 3), RGB222(3, 0, 3), RGB222(0, 3, 3), RGB222(3, 3, 3),
	};
	return palette[n & 15];
}

static uint8_t attr_colour(const term_t *t, uint8_t c, bool bold, bool is_fg) {
	if (c == TERM_DEFAULT_COLOUR) {
		if (!is_fg)
			return t->default_bg;
		return bold ? RGB222(3, 3, 3) : t->default_fg;
	}
	if (c < 16)
		return term_ansi_colour(c < 8 && bold && is_fg ? c + 8u : c);
	return (uint8_t)(c - 16);
}

// Colours for printed text: bold brightens the fg, reverse swaps.
static void text_colours(const term_t *t, uint8_t *fg, uint8_t *bg) {
	const term_attr_t *a = &t->cur.attr;
	uint8_t f = attr_colour(t, a->fg, a->bold, true);
	uint8_t b = attr_colour(t, a->bg, false, false);
	*fg = a->reverse ? b : f;
	*bg = a->reverse ? f : b;
}

// Colours for erased cells: the current background (as xterm's "background
// colour erase"), default foreground.
static void erase_colours(const term_t *t, uint8_t *fg, uint8_t *bg) {
	*fg = t->default_fg;
	*bg = attr_colour(t, t->cur.attr.bg, false, false);
}

static void sync_cursor(term_t *t) {
	t->screen->cursor_col = (uint8_t)t->cur.col;
	t->screen->cursor_row = (uint8_t)abs_row(t, t->cur.row);
}

static void reply(term_t *t, const char *fmt, ...) {
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

// Move to (col, row), row relative to the scrolling region in origin mode.
static void move_to(term_t *t, unsigned col, unsigned row) {
	unsigned lo = 0, hi = t->rows - 1;
	if (t->cur.origin_mode) {
		row += t->margin_top;
		lo = t->margin_top;
		hi = t->margin_bottom;
	}
	t->cur.row = row < lo ? lo : row > hi ? hi : row;
	t->cur.col = col < cols(t) ? col : cols(t) - 1;
	t->cur.wrap_pending = false;
}

static void scroll_up(term_t *t, unsigned top, unsigned bottom, unsigned n) {
	uint8_t fg, bg;
	erase_colours(t, &fg, &bg);
	for (unsigned i = 0; i < n && i <= bottom - top; ++i)
		screen_scroll_up(t->screen, abs_row(t, top), abs_row(t, bottom), fg, bg);
	if (top == bottom)
		screen_erase(t->screen, abs_row(t, top), 0, cols(t), fg, bg);
}

static void scroll_down(term_t *t, unsigned top, unsigned bottom, unsigned n) {
	uint8_t fg, bg;
	erase_colours(t, &fg, &bg);
	for (unsigned i = 0; i < n && i <= bottom - top; ++i)
		screen_scroll_down(t->screen, abs_row(t, top), abs_row(t, bottom), fg, bg);
	if (top == bottom)
		screen_erase(t->screen, abs_row(t, top), 0, cols(t), fg, bg);
}

// CUU / CUD: stop at the scrolling region's margin if inside it, else at
// the screen edge.
static void cursor_up(term_t *t, unsigned n) {
	unsigned limit = t->cur.row >= t->margin_top ? t->margin_top : 0;
	t->cur.row = t->cur.row >= limit + n ? t->cur.row - n : limit;
	t->cur.wrap_pending = false;
}

static void cursor_down(term_t *t, unsigned n) {
	unsigned limit = t->cur.row <= t->margin_bottom ? t->margin_bottom : t->rows - 1;
	t->cur.row = t->cur.row + n < limit ? t->cur.row + n : limit;
	t->cur.wrap_pending = false;
}

// IND: down a line, scrolling the region at its bottom margin.
static void index_down(term_t *t) {
	if (t->cur.row == t->margin_bottom)
		scroll_up(t, t->margin_top, t->margin_bottom, 1);
	else if (t->cur.row + 1 < t->rows)
		++t->cur.row;
}

// RI: up a line, scrolling the region at its top margin.
static void index_up(term_t *t) {
	if (t->cur.row == t->margin_top)
		scroll_down(t, t->margin_top, t->margin_bottom, 1);
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

static void print(term_t *t, uint8_t ch) {
	if (ch == 0x7f)
		return;
	uint8_t glyph = map_charset(t, ch);
	if (t->cur.wrap_pending) {
		t->cur.col = 0;
		index_down(t);
		t->cur.wrap_pending = false;
	}
	uint8_t fg, bg;
	text_colours(t, &fg, &bg);
	unsigned row = abs_row(t, t->cur.row);
	uint8_t attr = (t->cur.attr.underline ? SCREEN_ATTR_UNDERLINE : 0) |
		(t->cur.attr.blink ? SCREEN_ATTR_BLINK : 0) | (t->cur.attr.bold ? SCREEN_ATTR_BOLD : 0);
	if (t->insert_mode)
		screen_insert_cells(t->screen, row, t->cur.col, 1, fg, bg);
	screen_put_attr(t->screen, t->cur.col, row, glyph, fg, bg, attr);
	if (t->cur.col + 1 < cols(t))
		++t->cur.col;
	else if (t->autowrap)
		t->cur.wrap_pending = true;
}

// ---------------------------------------------------------------------------
// Tabs

static void set_tab(term_t *t, unsigned col, bool on) {
	if (on)
		t->tabs[col / 8] |= (uint8_t)(1u << col % 8);
	else
		t->tabs[col / 8] &= (uint8_t)~(1u << col % 8);
}

static bool is_tab(const term_t *t, unsigned col) {
	return t->tabs[col / 8] >> col % 8 & 1;
}

static void reset_tabs(term_t *t) {
	memset(t->tabs, 0, sizeof t->tabs);
	for (unsigned col = 8; col < SCREEN_MAX_COLS; col += 8)
		set_tab(t, col, true);
}

static void tab(term_t *t) {
	unsigned col = t->cur.col + 1;
	while (col < cols(t) - 1 && !is_tab(t, col))
		++col;
	t->cur.col = col < cols(t) ? col : cols(t) - 1;
	t->cur.wrap_pending = false;
}

// ---------------------------------------------------------------------------
// Reset and modes

static void reset_cursor(term_cursor_t *c) {
	*c = (term_cursor_t){
		.attr = { .fg = TERM_DEFAULT_COLOUR, .bg = TERM_DEFAULT_COLOUR },
		.charset = { 'B', 'B' },
	};
}

static void clear_band(term_t *t) {
	uint8_t fg, bg;
	erase_colours(t, &fg, &bg);
	for (unsigned row = 0; row < t->rows; ++row)
		screen_erase(t->screen, abs_row(t, row), 0, cols(t), fg, bg);
}

static void set_reverse_screen(term_t *t, bool on);

static void reset(term_t *t) {
	reset_cursor(&t->cur);
	reset_cursor(&t->saved);
	t->margin_top = 0;
	t->margin_bottom = t->rows - 1;
	t->autowrap = true;
	t->insert_mode = t->newline_mode = t->cursor_keys_app = t->keypad_app = false;
	reset_tabs(t);
	t->screen->cursor_visible = true;
	set_reverse_screen(t, false);
	clear_band(t);
}

// DECSCNM: show the terminal's rows with fg and bg swapped.
static void set_reverse_screen(term_t *t, bool on) {
	t->reverse_screen = on;
	uint32_t band = ((1u << t->rows) - 1) << t->top;
	uint32_t mask = on ? t->screen->reverse_rows | band : t->screen->reverse_rows & ~band;
	screen_set_reverse_rows(t->screen, mask);
}

// DECCOLM: switch between 80 and 132 columns. This clears the whole screen
// (status line included; its owner redraws it when it sees the new width).
static void set_columns(term_t *t, unsigned n) {
	bool visible = t->screen->cursor_visible;
	screen_init(t->screen, n, t->default_fg, t->default_bg);
	t->screen->cursor_visible = visible;
	set_reverse_screen(t, t->reverse_screen);
	t->margin_top = 0;
	t->margin_bottom = t->rows - 1;
	move_to(t, 0, 0);
}

static void set_mode(term_t *t, bool dec, unsigned mode, bool on) {
	if (!dec) {
		if (mode == 4)
			t->insert_mode = on;
		else if (mode == 20)
			t->newline_mode = on;
		return;
	}
	switch (mode) {
	case 1: t->cursor_keys_app = on; break;
	case 3: set_columns(t, on ? 132 : 80); break;
	case 5: set_reverse_screen(t, on); break;
	case 6: t->cur.origin_mode = on; move_to(t, 0, 0); break;
	case 7: t->autowrap = on; if (!on) t->cur.wrap_pending = false; break;
	case 25: t->screen->cursor_visible = on; break;
	default: break; // DECANM (VT52), DECSCLM, DECARM...: ignored
	}
}

// ---------------------------------------------------------------------------
// SGR

static void sgr(term_t *t, const vtparse_t *p) {
	term_attr_t *a = &t->cur.attr;
	unsigned n = p->n_params ? p->n_params : 1;
	for (unsigned i = 0; i < n; ++i) {
		unsigned v = p->params[i];
		if (v == 0)
			*a = (term_attr_t){ .fg = TERM_DEFAULT_COLOUR, .bg = TERM_DEFAULT_COLOUR };
		else if (v == 1)
			a->bold = true;
		else if (v == 4)
			a->underline = true;
		else if (v == 5)
			a->blink = true;
		else if (v == 7)
			a->reverse = true;
		else if (v == 22)
			a->bold = false;
		else if (v == 24)
			a->underline = false;
		else if (v == 25)
			a->blink = false;
		else if (v == 27)
			a->reverse = false;
		else if (v >= 30 && v <= 37)
			a->fg = (uint8_t)(v - 30);
		else if (v == 39)
			a->fg = TERM_DEFAULT_COLOUR;
		else if (v >= 40 && v <= 47)
			a->bg = (uint8_t)(v - 40);
		else if (v == 49)
			a->bg = TERM_DEFAULT_COLOUR;
		else if (v >= 90 && v <= 97)
			a->fg = (uint8_t)(v - 90 + 8);
		else if (v >= 100 && v <= 107)
			a->bg = (uint8_t)(v - 100 + 8);
		else if ((v == 38 || v == 48) && i + 1 < n) {
			// 38;5;n (256 colours) or 38;2;r;g;b (direct), reduced to RGB222.
			uint8_t *dst = v == 38 ? &a->fg : &a->bg;
			if (p->params[i + 1] == 5 && i + 2 < n) {
				unsigned c = p->params[i + 2];
				if (c < 16)
					*dst = (uint8_t)c;
				else if (c < 232) { // 6x6x6 cube: levels 0-5 to 0-3
					c -= 16;
					*dst = DIRECT_COLOUR(RGB222((c / 36) * 3 / 5, (c / 6 % 6) * 3 / 5, (c % 6) * 3 / 5));
				} else { // grey ramp
					unsigned g = (c - 232) * 4 / 24;
					*dst = DIRECT_COLOUR(RGB222(g, g, g));
				}
				i += 2;
			} else if (p->params[i + 1] == 2 && i + 4 < n) {
				*dst = DIRECT_COLOUR(RGB222(p->params[i + 2] >> 6, p->params[i + 3] >> 6,
					p->params[i + 4] >> 6));
				i += 4;
			} else {
				i = n; // malformed: stop
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Dispatch

static void erase_in_display(term_t *t, unsigned how) {
	uint8_t fg, bg;
	erase_colours(t, &fg, &bg);
	unsigned row = t->cur.row;
	if (how == 0) {
		screen_erase(t->screen, abs_row(t, row), t->cur.col, cols(t), fg, bg);
		for (unsigned r = row + 1; r < t->rows; ++r)
			screen_erase(t->screen, abs_row(t, r), 0, cols(t), fg, bg);
	} else if (how == 1) {
		for (unsigned r = 0; r < row; ++r)
			screen_erase(t->screen, abs_row(t, r), 0, cols(t), fg, bg);
		screen_erase(t->screen, abs_row(t, row), 0, t->cur.col + 1, fg, bg);
	} else if (how == 2) {
		clear_band(t);
	}
	t->cur.wrap_pending = false;
}

static void erase_in_line(term_t *t, unsigned how) {
	uint8_t fg, bg;
	erase_colours(t, &fg, &bg);
	unsigned row = abs_row(t, t->cur.row);
	if (how == 0)
		screen_erase(t->screen, row, t->cur.col, cols(t), fg, bg);
	else if (how == 1)
		screen_erase(t->screen, row, 0, t->cur.col + 1, fg, bg);
	else if (how == 2)
		screen_erase(t->screen, row, 0, cols(t), fg, bg);
	t->cur.wrap_pending = false;
}

static bool in_region(const term_t *t) {
	return t->cur.row >= t->margin_top && t->cur.row <= t->margin_bottom;
}

static void device_attributes(term_t *t) {
	reply(t, "\x1b[?1;2c"); // VT100 with Advanced Video Option
}

static void csi_dispatch(term_t *t, const vtparse_t *p, uint8_t final) {
	unsigned n = vtparse_param(p, 0, 1);
	uint8_t fg, bg;

	if (p->private_marker == '?') {
		if (final == 'h' || final == 'l')
			for (unsigned i = 0; i < p->n_params; ++i)
				set_mode(t, true, p->params[i], final == 'h');
		return;
	}
	if (p->private_marker || p->n_intermediates)
		return; // not VT100

	switch (final) {
	case 'A': // CUU
		cursor_up(t, n);
		break;
	case 'B': // CUD
		cursor_down(t, n);
		break;
	case 'C': // CUF
		t->cur.col = t->cur.col + n < cols(t) ? t->cur.col + n : cols(t) - 1;
		t->cur.wrap_pending = false;
		break;
	case 'D': // CUB
		t->cur.col = t->cur.col >= n ? t->cur.col - n : 0;
		t->cur.wrap_pending = false;
		break;
	case 'E': // CNL
		cursor_down(t, n);
		t->cur.col = 0;
		break;
	case 'F': // CPL
		cursor_up(t, n);
		t->cur.col = 0;
		break;
	case 'G': // CHA
	case '`': // HPA
		t->cur.col = n - 1 < cols(t) ? n - 1 : cols(t) - 1;
		t->cur.wrap_pending = false;
		break;
	case 'H': // CUP
	case 'f': // HVP
		move_to(t, vtparse_param(p, 1, 1) - 1, n - 1);
		break;
	case 'd': // VPA
		move_to(t, t->cur.col, n - 1);
		break;
	case 'J': // ED
		erase_in_display(t, vtparse_param(p, 0, 0));
		break;
	case 'K': // EL
		erase_in_line(t, vtparse_param(p, 0, 0));
		break;
	case 'L': // IL
		if (in_region(t)) {
			scroll_down(t, t->cur.row, t->margin_bottom, n);
			t->cur.col = 0;
			t->cur.wrap_pending = false;
		}
		break;
	case 'M': // DL
		if (in_region(t)) {
			scroll_up(t, t->cur.row, t->margin_bottom, n);
			t->cur.col = 0;
			t->cur.wrap_pending = false;
		}
		break;
	case '@': // ICH
		erase_colours(t, &fg, &bg);
		screen_insert_cells(t->screen, abs_row(t, t->cur.row), t->cur.col, n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'P': // DCH
		erase_colours(t, &fg, &bg);
		screen_delete_cells(t->screen, abs_row(t, t->cur.row), t->cur.col, n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'X': // ECH
		erase_colours(t, &fg, &bg);
		screen_erase(t->screen, abs_row(t, t->cur.row), t->cur.col, t->cur.col + n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'S': // SU
		scroll_up(t, t->margin_top, t->margin_bottom, n);
		break;
	case 'T': // SD
		scroll_down(t, t->margin_top, t->margin_bottom, n);
		break;
	case 'b': { // REP: the preceding printed character, n more times
		uint8_t ch = t->last_char;
		unsigned max = cols(t) * t->rows;
		for (unsigned i = 0; ch && i < n && i < max; ++i)
			print(t, ch);
		break;
	}
	case 'c': // DA
		if (vtparse_param(p, 0, 0) == 0)
			device_attributes(t);
		break;
	case 'g': // TBC
		if (vtparse_param(p, 0, 0) == 0)
			set_tab(t, t->cur.col, false);
		else if (vtparse_param(p, 0, 0) == 3)
			memset(t->tabs, 0, sizeof t->tabs);
		break;
	case 'h': // SM
	case 'l': // RM
		for (unsigned i = 0; i < p->n_params; ++i)
			set_mode(t, false, p->params[i], final == 'h');
		break;
	case 'm': // SGR
		sgr(t, p);
		break;
	case 'n': // DSR
		if (vtparse_param(p, 0, 0) == 5) {
			reply(t, "\x1b[0n");
		} else if (vtparse_param(p, 0, 0) == 6) {
			unsigned row = t->cur.row - (t->cur.origin_mode ? t->margin_top : 0);
			reply(t, "\x1b[%u;%uR", row + 1, t->cur.col + 1);
		}
		break;
	case 'r': { // DECSTBM
		unsigned top = vtparse_param(p, 0, 1), bottom = vtparse_param(p, 1, t->rows);
		if (bottom > t->rows)
			bottom = t->rows;
		if (top < bottom) {
			t->margin_top = top - 1;
			t->margin_bottom = bottom - 1;
			move_to(t, 0, 0);
		}
		break;
	}
	case 'x': { // DECREQTPARM: no parity, 8 bits, 38400 baud, 16x clock
		unsigned req = vtparse_param(p, 0, 0);
		if (req <= 1)
			reply(t, "\x1b[%u;1;1;128;128;1;0x", req + 2);
		break;
	}
	default:
		break; // DECLL, DECTST and anything else: ignored
	}
}

static void esc_dispatch(term_t *t, const vtparse_t *p, uint8_t final) {
	if (p->n_intermediates == 1) {
		uint8_t i = p->intermediates[0];
		if (i == '(' || i == ')') { // SCS: designate G0 / G1
			t->cur.charset[i == ')'] = final == '0' || final == 'A' ? final : 'B';
		} else if (i == '#' && final == '8') { // DECALN
			t->margin_top = 0;
			t->margin_bottom = t->rows - 1;
			for (unsigned row = 0; row < t->rows; ++row)
				for (unsigned col = 0; col < cols(t); ++col)
					screen_put(t->screen, col, abs_row(t, row), 'E', t->default_fg, t->default_bg);
			move_to(t, 0, 0);
		}
		return; // ESC # 3-6 (double height/width): ignored
	}
	if (p->n_intermediates)
		return;
	switch (final) {
	case 'D': index_down(t); t->cur.wrap_pending = false; break; // IND
	case 'E': t->cur.col = 0; index_down(t); t->cur.wrap_pending = false; break; // NEL
	case 'M': index_up(t); t->cur.wrap_pending = false; break; // RI
	case 'H': set_tab(t, t->cur.col, true); break; // HTS
	case '7': t->saved = t->cur; break; // DECSC
	case '8': // DECRC
		t->cur = t->saved;
		move_to(t, t->cur.col, t->cur.row - (t->cur.origin_mode ? t->margin_top : 0));
		t->cur.wrap_pending = t->saved.wrap_pending;
		break;
	case '=': t->keypad_app = true; break; // DECKPAM
	case '>': t->keypad_app = false; break; // DECKPNM
	case 'Z': device_attributes(t); break; // DECID
	case 'c': reset(t); break; // RIS
	default: break;
	}
}

static void execute(term_t *t, uint8_t ch) {
	switch (ch) {
	case BS:
		if (t->cur.col > 0)
			--t->cur.col;
		t->cur.wrap_pending = false;
		break;
	case HT:
		tab(t);
		break;
	case LF:
	case VT:
	case FF:
		index_down(t);
		if (t->newline_mode)
			t->cur.col = 0;
		t->cur.wrap_pending = false;
		break;
	case CR:
		t->cur.col = 0;
		t->cur.wrap_pending = false;
		break;
	case SO:
		t->cur.gl = 1;
		break;
	case SI:
		t->cur.gl = 0;
		break;
	default:
		break; // BEL, ENQ (no answerback), NUL, CAN, SUB...: nothing to do
	}
}

static void on_parse(vtparse_t *p, vtparse_action_t action, uint8_t ch) {
	term_t *t = p->user;
	switch (action) {
	case VTPARSE_PRINT:
		print(t, ch);
		t->last_char = ch == 0x7f ? 0 : ch;
		return;
	case VTPARSE_EXECUTE: execute(t, ch); break;
	case VTPARSE_ESC_DISPATCH: esc_dispatch(t, p, ch); break;
	case VTPARSE_CSI_DISPATCH: csi_dispatch(t, p, ch); break;
	}
	t->last_char = 0; // REP repeats only a character printed just before it
}

// ---------------------------------------------------------------------------
// API

void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows, uint8_t fg, uint8_t bg) {
	*t = (term_t){ .screen = screen, .top = top, .rows = rows, .default_fg = fg, .default_bg = bg };
	vtparse_init(&t->parser, on_parse, t);
	reset(t);
	sync_cursor(t);
}

void term_set_reply(term_t *t, term_reply_t reply_fn, void *ctx) {
	t->reply = reply_fn;
	t->reply_ctx = ctx;
}

// The screen's cursor is updated once per call, not per byte.
void term_write(term_t *t, const uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; ++i)
		vtparse_byte(&t->parser, data[i]);
	sync_cursor(t);
}

void term_putc(term_t *t, uint8_t c) {
	term_write(t, &c, 1);
}

void term_puts(term_t *t, const char *text) {
	term_write(t, (const uint8_t *)text, strlen(text));
}
