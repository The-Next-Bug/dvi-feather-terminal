// VT52 mode of the VT100 personality (DECANM reset): the VT100's own
// emulation of a VT52, as in the VT100 User Guide. It has no CSI, and
// ESC Y takes two raw bytes, so it has this small parser instead of
// vtparse. C0 controls act as in ANSI mode (vt100_execute).

#include "term_core.h"
#include "vt100.h"

#define ESC 0x1b
#define CAN 0x18
#define SUB 0x1a

enum { GROUND, ESCAPE, ROW, COLUMN }; // term_vt100_t.vt52_state

// ESC Y: row and column biased by 32, from the top left of the screen
// (origin mode does not apply). A row out of range leaves the cursor on
// its row; a column out of range puts it in the last column.
static void address(term_t *t, uint8_t row_byte, uint8_t col_byte) {
	unsigned row = (unsigned)row_byte - 32, col = (unsigned)col_byte - 32;
	if (row_byte >= 32 && row < t->rows)
		t->cur.row = row;
	if (col_byte >= 32)
		t->cur.col = col < term_cols(t) ? col : term_cols(t) - 1;
	t->cur.wrap_pending = false;
}

static void escape(term_t *t, uint8_t final) {
	term_vt100_t *v = &t->vt100;
	switch (final) {
	case 'A': term_cursor_up(t, 1); break;
	case 'B': term_cursor_down(t, 1); break;
	case 'C':
		if (t->cur.col + 1 < term_cols(t))
			++t->cur.col;
		t->cur.wrap_pending = false;
		break;
	case 'D':
		if (t->cur.col > 0)
			--t->cur.col;
		t->cur.wrap_pending = false;
		break;
	case 'F': t->cur.charset[t->cur.gl] = '0'; break; // graphics: DEC Special Graphics
	case 'G': t->cur.charset[t->cur.gl] = 'B'; break; // ASCII
	case 'H':
		t->cur.row = t->cur.col = 0;
		t->cur.wrap_pending = false;
		break;
	case 'I': term_index_up(t); t->cur.wrap_pending = false; break; // reverse line feed
	case 'J': term_erase_display(t, 0); break;
	case 'K': term_erase_line(t, 0); break;
	case 'Y': v->vt52_state = ROW; break;
	case 'Z': term_reply(t, "\x1b/Z"); break; // identify: a VT100 in VT52 mode
	case '=': v->keypad_app = true; break;  // alternate keypad
	case '>': v->keypad_app = false; break;
	case '<': vt100_set_vt52(t, false); break; // ANSI mode
	default: break; // print controller, hold screen: ignored
	}
}

void vt52_byte(term_t *t, uint8_t ch) {
	term_vt100_t *v = &t->vt100;
	if (ch == CAN || ch == SUB) {
		v->vt52_state = GROUND;
		return;
	}
	if (ch == ESC) {
		v->vt52_state = ESCAPE;
		return;
	}
	switch (v->vt52_state) {
	case ROW:
		v->vt52_row = ch;
		v->vt52_state = COLUMN;
		return;
	case COLUMN:
		v->vt52_state = GROUND;
		address(t, v->vt52_row, ch);
		return;
	default:
		break;
	}
	if (ch < 0x20) {
		vt100_execute(t, ch); // also inside ESC x, as in ANSI mode
	} else if (v->vt52_state == ESCAPE) {
		v->vt52_state = GROUND;
		escape(t, ch);
	} else if (ch != 0x7f && (ch < 0x80 || ch >= 0xa0)) {
		term_print(t, ch); // C1 bytes ignored, as vtparse does
	}
}
