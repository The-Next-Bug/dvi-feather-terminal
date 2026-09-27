#ifndef TERM_H
#define TERM_H

// A "dumb" teletype on top of screen_t: printable characters, CR, LF, BS,
// TAB and scrolling. Portable C (no pico-sdk), host-tested. The VT100
// parser will replace term_putc's control handling.
//
// The terminal owns a band of screen rows (top .. top + rows - 1); rows
// outside it (e.g. a status line) are left alone.
//
// Wrapping follows the VT100: a character written in the last column leaves
// the cursor there with a wrap pending, and the next printable character
// wraps first. CR, BS and LF clear a pending wrap.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"

#define TERM_TAB_WIDTH 8

typedef struct {
	screen_t *screen;
	unsigned top, rows;
	unsigned col, row; // cursor, row relative to top
	bool wrap_pending;
	uint8_t fg, bg;
} term_t;

// Clear the terminal's rows, home the cursor and show it.
void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows, uint8_t fg, uint8_t bg);

void term_putc(term_t *t, uint8_t c);
void term_write(term_t *t, const uint8_t *data, size_t len);
void term_puts(term_t *t, const char *text);

#endif
