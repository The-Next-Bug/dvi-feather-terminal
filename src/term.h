#ifndef TERM_H
#define TERM_H

// VT100 emulation on top of screen_t. Portable C (no pico-sdk), host-tested.
//
// Bytes from the host go through vtparse (escape sequence parsing) and are
// applied here: printing with DEC-style deferred autowrap, C0 controls,
// cursor movement, scrolling regions, erasing, line and character
// insert/delete (VT102), tab stops, character sets (G0/G1: ASCII, UK, DEC
// Special Graphics), modes (including DECSCNM reverse screen), save/restore
// cursor, SGR colours, underline and blink, and replies to
// device attribute and status requests, and REP (repeat character, which
// ncurses uses under TERM=xterm-16color).
//
// The terminal owns a band of screen rows (top .. top + rows - 1); rows
// outside it (e.g. a status line) are left alone. Its width is the screen's
// (80 or 132); DECCOLM (CSI ? 3 h/l) switches it, which re-initialises the
// whole screen.
//
// Not (yet) implemented: VT52 mode (dmt.16), the alternate screen (dmt.15),
// double-width/height lines (dmt.12/13), smooth scrolling (dmt.14).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "vtparse.h"

#define TERM_DEFAULT_COLOUR 0xff // SGR 39 / 49: the terminal's default fg/bg

typedef void (*term_reply_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
	uint8_t fg, bg; // ANSI colour 0-15, or TERM_DEFAULT_COLOUR
	bool bold, underline, blink, reverse;
} term_attr_t;

typedef struct {
	unsigned col, row;
	bool wrap_pending, origin_mode;
	term_attr_t attr;
	uint8_t charset[2]; // G0, G1 designators: 'B' ASCII, 'A' UK, '0' DEC graphics
	uint8_t gl;         // which of G0/G1 is invoked (SI/SO)
} term_cursor_t;

typedef struct {
	screen_t *screen;
	unsigned top, rows;
	uint8_t default_fg, default_bg; // RGB222

	vtparse_t parser;
	term_cursor_t cur;   // row relative to top
	term_cursor_t saved; // DECSC
	unsigned margin_top, margin_bottom; // scrolling region, relative, inclusive

	bool autowrap;        // DECAWM
	bool insert_mode;     // IRM
	bool newline_mode;    // LNM: LF, VT and FF also do CR
	bool cursor_keys_app; // DECCKM, for the keyboard
	bool keypad_app;      // DECKPAM / DECKPNM, for the keyboard
	bool reverse_screen;  // DECSCNM

	uint8_t tabs[(SCREEN_MAX_COLS + 7) / 8];
	uint8_t last_char; // for REP: the last character printed, 0 after anything else

	term_reply_t reply;
	void *reply_ctx;
} term_t;

// Reset the terminal (as RIS), clear its rows, home the cursor and show it.
// fg/bg are the colours of SGR 0 text (RGB222).
void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows, uint8_t fg, uint8_t bg);

// Where replies to the host (device attributes, cursor position reports)
// go. Without one they are dropped.
void term_set_reply(term_t *t, term_reply_t reply, void *ctx);

void term_putc(term_t *t, uint8_t c);
void term_write(term_t *t, const uint8_t *data, size_t len);
void term_puts(term_t *t, const char *text);

// The RGB222 colour for ANSI colour n (0-15).
uint8_t term_ansi_colour(unsigned n);

#endif
