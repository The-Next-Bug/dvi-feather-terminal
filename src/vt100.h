#ifndef VT100_H
#define VT100_H

// The VT100/VT102 personality of the terminal (term.h), for term.c only.
// Portable C, host-tested through test_term.
//
// Bytes go through vtparse (escape sequence parsing) and are applied with
// term_core.h: printing, C0 controls, cursor movement, scrolling regions,
// erasing, line and character insert/delete (VT102), tab stops, character
// sets (G0/G1: ASCII, UK, DEC Special Graphics), modes (including DECSCNM
// reverse screen and DECCOLM), save/restore cursor, SGR colours (as palette
// indices: palette.h), underline and blink, DECBKM (what Backspace sends,
// for the keyboard), replies to device attribute and status requests, and
// REP (repeat character, which ncurses uses under TERM=xterm-16color).
// Its own state is term_t.vt100.
//
// VT52 mode (DECANM: CSI ? 2 l, back with ESC <) is part of this
// personality, decoded by vt52.c instead of vtparse.
//
// Not (yet) implemented: the alternate screen (dmt.15), double-width/height
// lines (dmt.12/13), smooth scrolling (dmt.14).

#include <stdbool.h>
#include <stdint.h>

#include "term.h"

void vt100_init(term_t *t);   // once, from term_init
void vt100_reset(term_t *t);  // the personality's state as RIS leaves it
void vt100_cancel(term_t *t); // abandon a sequence or string in progress

// VT52 mode (DECANM reset) on or off; the power-on/RIS one is term_t.defaults.vt52.
void vt100_set_vt52(term_t *t, bool on);

// C0 controls, the same in ANSI and VT52 mode (for vt52.c).
void vt100_execute(term_t *t, uint8_t ch);

void vt52_byte(term_t *t, uint8_t ch); // vt52.c

static inline void vt100_byte(term_t *t, uint8_t ch) {
	if (t->vt100.vt52)
		vt52_byte(t, ch);
	else
		vtparse_byte(&t->vt100.parser, ch);
}

#endif
