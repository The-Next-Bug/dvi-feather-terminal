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
// Not (yet) implemented: VT52 mode (dmt.16, a sub-mode of this
// personality), the alternate screen (dmt.15), double-width/height lines
// (dmt.12/13), smooth scrolling (dmt.14).

#include <stdint.h>

#include "term.h"

void vt100_init(term_t *t);   // once, from term_init
void vt100_reset(term_t *t);  // the personality's state as RIS leaves it
void vt100_cancel(term_t *t); // abandon a sequence or string in progress

static inline void vt100_byte(term_t *t, uint8_t ch) {
	vtparse_byte(&t->vt100.parser, ch);
}

#endif
