#include "term_internal.h"


#define BEL 0x07
#define BS 0x08
#define HT 0x09
#define LF 0x0a
#define VT 0x0b
#define FF 0x0c
#define CR 0x0d
#define ETB 0x17 // ^W
#define CAN 0x18 // ^X
#define SUB 0x1a // ^Z
#define ESC 0x1b
#define RS 0x1e  // ^^

enum { GROUND, ESCAPE, ROW, COLUMN }; // term_adm3a_t.state

void adm3a_reset(term_t *t) {
	t->adm3a.state = GROUND;
}

static void home(term_t *t) {
	t->cur.row = t->cur.col = 0;
	t->cur.wrap_pending = false;
}

// ESC = row col, each biased by 32; out of range: the nearest edge.
static void address(term_t *t, uint8_t row_byte, uint8_t col_byte) {
	unsigned row = row_byte >= 32 ? row_byte - 32u : 0, col = col_byte >= 32 ? col_byte - 32u : 0;
	t->cur.row = row < t->rows ? row : t->rows - 1;
	t->cur.col = col < term_cols(t) ? col : term_cols(t) - 1;
	t->cur.wrap_pending = false;
}

// The ADM-3A wraps as soon as the last column is written: the cursor goes
// to the next line's start at once, scrolling from the bottom line.
static void print(term_t *t, uint8_t ch) {
	term_print(t, ch);
	if (t->cur.wrap_pending) {
		t->cur.wrap_pending = false;
		t->cur.col = 0;
		term_index_down(t);
	}
}

static void control(term_t *t, uint8_t ch) {
	switch (ch) {
	case BS:
		if (t->cur.col > 0) {
			--t->cur.col;
		}
		break;
	case HT:
		term_tab(t);
		break;
	case LF:
		term_index_down(t);
		if (t->newline_mode) {
			t->cur.col = 0;
		}
		break;
	case VT:
		if (t->cur.row > 0) {
			--t->cur.row;
		}
		break;
	case FF:
		if (t->cur.col + 1 < term_cols(t)) {
			++t->cur.col;
		}
		break;
	case CR:
		t->cur.col = 0;
		break;
	case ETB:
		term_erase_display(t, 0);
		break;
	case CAN:
		term_erase_line(t, 0);
		break;
	case SUB:
		term_erase_display(t, 2);
		home(t);
		break;
	case RS:
		home(t);
		break;
	case BEL:
		++t->bells;
		break;
	default:
		break; // ^N/^O (keyboard unlock/lock), the rest: nothing
	}
}

static void escape(term_t *t, uint8_t final) {
	switch (final) {
	case '=': t->adm3a.state = ROW; break;
	case 'E': term_insert_lines(t, 1); break;
	case 'R': term_delete_lines(t, 1); break;
	default: break; // the Kaypro's ESC A / ESC G (ASCII / Greek): ignored
	}
}

void adm3a_byte(term_t *t, uint8_t ch) {
	term_adm3a_t *a = &t->adm3a;
	ch &= 0x7f; // a 7-bit terminal: the 8th bit was parity (WordStar sets it)
	switch (a->state) {
	case ESCAPE:
		a->state = GROUND;
		escape(t, ch);
		return;
	case ROW:
		a->row = ch;
		a->state = COLUMN;
		return;
	case COLUMN:
		a->state = GROUND;
		address(t, a->row, ch);
		return;
	default:
		break;
	}
	if (ch == ESC) {
		a->state = ESCAPE;
	} else if (ch < 0x20) {
		control(t, ch);
	} else if (ch < 0x7f) {
		print(t, ch); // DEL: nothing
	}
}
