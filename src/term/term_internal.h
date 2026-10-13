#ifndef TERM_INTERNAL_H
#define TERM_INTERNAL_H

// The terminal library's own header (bd gr1): for term.c and the
// personalities only, not for term.h's users.
//
// The cell-level terminal (term.c) that the personalities (vt100.c, ...)
// drive. Rows are relative to the terminal's band; "the region" is the
// scrolling region (margin_top .. margin_bottom). Everything here works the same whatever the personality;
// what a byte from the host means is up to the personality.

#include <stdbool.h>
#include <stdint.h>

#include "term.h"

unsigned term_cols(const term_t *t);
unsigned term_abs_row(const term_t *t, unsigned row);

// Colours for erased cells: the current background (as xterm's "background
// colour erase"), default foreground.
void term_erase_colours(const term_t *t, uint8_t *fg, uint8_t *bg);

// printf-style reply to the host (at most 39 bytes); dropped without a
// reply callback.
void term_reply(term_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Move to (col, row), row relative to the scrolling region in origin mode.
void term_move_to(term_t *t, unsigned col, unsigned row);
void term_scroll_up(term_t *t, unsigned top, unsigned bottom, unsigned n);
void term_scroll_down(term_t *t, unsigned top, unsigned bottom, unsigned n);
// Up / down n rows: stop at the region's margin if inside it, else at the
// band's edge.
void term_cursor_up(term_t *t, unsigned n);
void term_cursor_down(term_t *t, unsigned n);
// Down / up a line, scrolling the region at its bottom / top margin.
void term_index_down(term_t *t);
void term_index_up(term_t *t);

// Print a character at the cursor through the invoked character set, in
// the current attributes (insert mode and deferred autowrap included).
// DEL prints nothing.
void term_print(term_t *t, uint8_t ch);

void term_set_tab(term_t *t, unsigned col, bool on);
void term_reset_tabs(term_t *t); // every 8 columns
void term_tab(term_t *t);        // to the next tab stop or the last column

void term_reset_cursor(term_cursor_t *c); // home, default attributes, ASCII
void term_set_reverse_screen(term_t *t, bool on);
// Show the alternate screen (cleared) or go back to the main one.
void term_set_alt_screen(term_t *t, bool on);

// 0: cursor to end, 1: start to cursor (inclusive), 2: all.
void term_erase_display(term_t *t, unsigned how);
void term_erase_line(term_t *t, unsigned how);
// Insert / delete n lines at the cursor's row within the region (nothing
// outside it), cursor to column 0.
void term_insert_lines(term_t *t, unsigned n);
void term_delete_lines(term_t *t, unsigned n);

// ---- vt100.c, vt52.c ----

// The VT100/VT102 personality of the terminal (term.h).
// Portable C, host-tested through test_term.
//
// Bytes go through vtparse (escape sequence parsing) and are applied with
// the cell-level terminal above: printing, C0 controls, cursor movement,
// scrolling regions, erasing, line and character insert/delete (VT102), tab stops, character
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

// ---- adm3a.c ----

// The ADM-3A personality of the terminal (term.h).
// Portable C, host-tested through test_term.
//
// Lear Siegler ADM-3A as terminfo's adm3a: ^H left, ^J down (scrolling at
// the bottom), ^K up, ^L right (none of them erase; all stop at the
// edges), ^M return, ^Z clear and home, ^^ home, ESC = row+32 col+32
// cursor address; printing wraps at once in the last column (no deferred
// wrap), scrolling from the bottom right. Plus the Kaypro II's additions
// (terminfo kaypro), which ADM-3A software never sends: ^X clear to end
// of line, ^W clear to end of screen, ESC E insert line, ESC R delete line.
// No attributes, no character sets, no replies to the host; 80 columns.

void adm3a_reset(term_t *t); // also abandons an ESC = in progress
void adm3a_byte(term_t *t, uint8_t ch);

#endif
