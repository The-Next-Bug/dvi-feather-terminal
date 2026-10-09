#ifndef TERM_CORE_H
#define TERM_CORE_H

// The cell-level terminal (term.c) that the personalities (vt100.c, ...)
// drive: for them only, not for term.h's users. Rows are relative to the
// terminal's band; "the region" is the scrolling region (margin_top ..
// margin_bottom). Everything here works the same whatever the personality;
// what a byte from the host means is up to the personality.

#include <stdbool.h>

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

// 0: cursor to end, 1: start to cursor (inclusive), 2: all.
void term_erase_display(term_t *t, unsigned how);
void term_erase_line(term_t *t, unsigned how);
// Insert / delete n lines at the cursor's row within the region (nothing
// outside it), cursor to column 0.
void term_insert_lines(term_t *t, unsigned n);
void term_delete_lines(term_t *t, unsigned n);

#endif
