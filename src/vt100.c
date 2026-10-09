#include "vt100.h"

#include <string.h>

#include "palette.h"
#include "term_core.h"

#define BS 0x08
#define HT 0x09
#define LF 0x0a
#define VT 0x0b
#define FF 0x0c
#define CR 0x0d
#define SO 0x0e
#define SI 0x0f

// ---------------------------------------------------------------------------
// Modes

static void set_mode(term_t *t, bool dec, unsigned mode, bool on) {
	if (!dec) {
		if (mode == 4)
			t->insert_mode = on;
		else if (mode == 20)
			t->newline_mode = on;
		return;
	}
	switch (mode) {
	case 1: t->vt100.cursor_keys_app = on; break;
	case 3: term_set_columns(t, on ? 132 : 80); break;
	case 5: term_set_reverse_screen(t, on); break;
	case 6: t->cur.origin_mode = on; term_move_to(t, 0, 0); break;
	case 7: t->autowrap = on; if (!on) t->cur.wrap_pending = false; break;
	case 25: t->screen->cursor_visible = on; break;
	case 67: t->backspace_bs = on; break; // DECBKM
	default: break; // DECANM (VT52), DECSCLM, DECARM...: ignored
	}
}

// ---------------------------------------------------------------------------
// SGR

static uint8_t channel(unsigned v) {
	return (uint8_t)(v > 255 ? 255 : v);
}

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
			// 38;5;n (256 colours) or 38;2;r;g;b (direct: the nearest entry).
			uint16_t *dst = v == 38 ? &a->fg : &a->bg;
			if (p->params[i + 1] == 5 && i + 2 < n) {
				if (p->params[i + 2] < 256)
					*dst = (uint16_t)p->params[i + 2];
				i += 2;
			} else if (p->params[i + 1] == 2 && i + 4 < n) {
				*dst = palette_nearest(channel(p->params[i + 2]), channel(p->params[i + 3]),
					channel(p->params[i + 4]));
				i += 4;
			} else {
				i = n; // malformed: stop
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Dispatch

static void device_attributes(term_t *t) {
	term_reply(t, "\x1b[?1;2c"); // VT100 with Advanced Video Option
}

static void csi_dispatch(term_t *t, const vtparse_t *p, uint8_t final) {
	unsigned n = vtparse_param(p, 0, 1);
	uint8_t fg, bg;

	if (p->private_marker == '?') {
		if (final == 'h' || final == 'l')
			for (unsigned i = 0; i < p->n_params; ++i)
				set_mode(t, true, p->params[i], final == 'h');
		else if (final == '~' && p->n_params == 1 && p->params[0] == TERM_SETUP_PARAM)
			++t->setup_requests;
		return;
	}
	if (p->private_marker || p->n_intermediates)
		return; // not VT100

	switch (final) {
	case 'A': // CUU
		term_cursor_up(t, n);
		break;
	case 'B': // CUD
		term_cursor_down(t, n);
		break;
	case 'C': // CUF
		t->cur.col = t->cur.col + n < term_cols(t) ? t->cur.col + n : term_cols(t) - 1;
		t->cur.wrap_pending = false;
		break;
	case 'D': // CUB
		t->cur.col = t->cur.col >= n ? t->cur.col - n : 0;
		t->cur.wrap_pending = false;
		break;
	case 'E': // CNL
		term_cursor_down(t, n);
		t->cur.col = 0;
		break;
	case 'F': // CPL
		term_cursor_up(t, n);
		t->cur.col = 0;
		break;
	case 'G': // CHA
	case '`': // HPA
		t->cur.col = n - 1 < term_cols(t) ? n - 1 : term_cols(t) - 1;
		t->cur.wrap_pending = false;
		break;
	case 'H': // CUP
	case 'f': // HVP
		term_move_to(t, vtparse_param(p, 1, 1) - 1, n - 1);
		break;
	case 'd': // VPA
		term_move_to(t, t->cur.col, n - 1);
		break;
	case 'J': // ED
		term_erase_display(t, vtparse_param(p, 0, 0));
		break;
	case 'K': // EL
		term_erase_line(t, vtparse_param(p, 0, 0));
		break;
	case 'L': // IL
		term_insert_lines(t, n);
		break;
	case 'M': // DL
		term_delete_lines(t, n);
		break;
	case '@': // ICH
		term_erase_colours(t, &fg, &bg);
		screen_insert_cells(t->screen, term_abs_row(t, t->cur.row), t->cur.col, n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'P': // DCH
		term_erase_colours(t, &fg, &bg);
		screen_delete_cells(t->screen, term_abs_row(t, t->cur.row), t->cur.col, n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'X': // ECH
		term_erase_colours(t, &fg, &bg);
		screen_erase(t->screen, term_abs_row(t, t->cur.row), t->cur.col, t->cur.col + n, fg, bg);
		t->cur.wrap_pending = false;
		break;
	case 'S': // SU
		term_scroll_up(t, t->margin_top, t->margin_bottom, n);
		break;
	case 'T': // SD
		term_scroll_down(t, t->margin_top, t->margin_bottom, n);
		break;
	case 'b': { // REP: the preceding printed character, n more times
		uint8_t ch = t->vt100.last_char;
		unsigned max = term_cols(t) * t->rows;
		for (unsigned i = 0; ch && i < n && i < max; ++i)
			term_print(t, ch);
		break;
	}
	case 'c': // DA
		if (vtparse_param(p, 0, 0) == 0)
			device_attributes(t);
		break;
	case 'g': // TBC
		if (vtparse_param(p, 0, 0) == 0)
			term_set_tab(t, t->cur.col, false);
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
			term_reply(t, "\x1b[0n");
		} else if (vtparse_param(p, 0, 0) == 6) {
			unsigned row = t->cur.row - (t->cur.origin_mode ? t->margin_top : 0);
			term_reply(t, "\x1b[%u;%uR", row + 1, t->cur.col + 1);
		}
		break;
	case 'r': { // DECSTBM
		unsigned top = vtparse_param(p, 0, 1), bottom = vtparse_param(p, 1, t->rows);
		if (bottom > t->rows)
			bottom = t->rows;
		if (top < bottom) {
			t->margin_top = top - 1;
			t->margin_bottom = bottom - 1;
			term_move_to(t, 0, 0);
		}
		break;
	}
	case 'x': { // DECREQTPARM: no parity, 8 bits, 38400 baud, 16x clock
		unsigned req = vtparse_param(p, 0, 0);
		if (req <= 1)
			term_reply(t, "\x1b[%u;1;1;128;128;1;0x", req + 2);
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
				for (unsigned col = 0; col < term_cols(t); ++col)
					screen_put(t->screen, col, term_abs_row(t, row), 'E', PALETTE_TEXT, PALETTE_BACKGROUND);
			term_move_to(t, 0, 0);
		}
		return; // ESC # 3-6 (double height/width): ignored
	}
	if (p->n_intermediates)
		return;
	switch (final) {
	case 'D': term_index_down(t); t->cur.wrap_pending = false; break; // IND
	case 'E': t->cur.col = 0; term_index_down(t); t->cur.wrap_pending = false; break; // NEL
	case 'M': term_index_up(t); t->cur.wrap_pending = false; break; // RI
	case 'H': term_set_tab(t, t->cur.col, true); break; // HTS
	case '7': t->vt100.saved = t->cur; break; // DECSC
	case '8': // DECRC
		t->cur = t->vt100.saved;
		term_move_to(t, t->cur.col, t->cur.row - (t->cur.origin_mode ? t->margin_top : 0));
		t->cur.wrap_pending = t->vt100.saved.wrap_pending;
		break;
	case '=': t->vt100.keypad_app = true; break; // DECKPAM
	case '>': t->vt100.keypad_app = false; break; // DECKPNM
	case 'Z': device_attributes(t); break; // DECID
	case 'c': term_reset(t); break; // RIS
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
		term_tab(t);
		break;
	case LF:
	case VT:
	case FF:
		term_index_down(t);
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
		term_print(t, ch);
		t->vt100.last_char = ch == 0x7f ? 0 : ch;
		return;
	case VTPARSE_EXECUTE: execute(t, ch); break;
	case VTPARSE_ESC_DISPATCH: esc_dispatch(t, p, ch); break;
	case VTPARSE_CSI_DISPATCH: csi_dispatch(t, p, ch); break;
	}
	t->vt100.last_char = 0; // REP repeats only a character printed just before it
}

// ---------------------------------------------------------------------------
// Personality

void vt100_init(term_t *t) {
	vtparse_init(&t->vt100.parser, on_parse, t);
}

void vt100_reset(term_t *t) {
	term_reset_cursor(&t->vt100.saved);
	t->vt100.cursor_keys_app = t->vt100.keypad_app = false;
	t->vt100.last_char = 0; // REP must not repeat a character from before the reset
}

// vtparse_reset may run inside the parser's callback (RIS).
void vt100_cancel(term_t *t) {
	vtparse_reset(&t->vt100.parser);
}
