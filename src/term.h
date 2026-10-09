#ifndef TERM_H
#define TERM_H

// The terminal: a band of screen rows driven by bytes from the host.
// Portable C (no pico-sdk), host-tested.
//
// It is split by personality, so that what one terminal type means by a
// byte stays out of the others:
// - term.c (this API): the cell-level terminal any personality drives:
//   the cursor, printing with DEC-style deferred autowrap, scrolling (with
//   a scrolling region), erasing, tab stops, character sets, reverse screen,
//   80/132 columns and replies to the host. term_core.h has its helpers.
// - vt100.c (vt100.h): the VT100/VT102 personality, the only one so far:
//   escape sequences through vtparse, C0 controls, modes, SGR, device
//   reports. term_write and term_reset dispatch on term_t.personality.
//
// The terminal owns a band of screen rows (top .. top + rows - 1); rows
// outside it (e.g. a status line) are left alone. Its width is the screen's
// (80 or 132); DECCOLM (CSI ? 3 h/l) switches it, which re-initialises the
// whole screen (even when the width stays the same) and bumps
// screen_resets, so the owner of the other rows knows to redraw them.
//
// CSI ? 999 ~ (private to this terminal) asks for SET-UP: it bumps
// setup_requests, and term_write stops right after it, so the owner can hand
// the bytes that follow to SET-UP instead.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "vtparse.h"

#define TERM_DEFAULT_COLOUR 0x100 // SGR 39 / 49: the terminal's default fg/bg
#define TERM_SETUP_PARAM 999     // CSI ? 999 ~: open SET-UP

typedef enum {
	TERM_VT100, // vt100.c
} term_personality_t;

typedef void (*term_reply_t)(void *ctx, const uint8_t *data, size_t len);

// Modes that RIS resets to (SET-UP settings) rather than off.
typedef struct {
	bool backspace_bs; // DECBKM
	bool newline_mode; // LNM
} term_defaults_t;

typedef struct {
	uint16_t fg, bg; // palette index 0-255, or TERM_DEFAULT_COLOUR
	bool bold, underline, blink, reverse;
} term_attr_t;

typedef struct {
	unsigned col, row;
	bool wrap_pending, origin_mode;
	term_attr_t attr;
	uint8_t charset[2]; // G0, G1 designators: 'B' ASCII, 'A' UK, '0' DEC graphics
	uint8_t gl;         // which of G0/G1 is invoked (SI/SO)
} term_cursor_t;

// The VT100 personality's own state (vt100.c).
typedef struct {
	vtparse_t parser;
	term_cursor_t saved;  // DECSC
	bool cursor_keys_app; // DECCKM, for the keyboard
	bool keypad_app;      // DECKPAM / DECKPNM, for the keyboard
	uint8_t last_char; // for REP: the last character printed, 0 after anything else
} term_vt100_t;

typedef struct {
	screen_t *screen;
	unsigned top, rows;
	term_defaults_t defaults;
	term_personality_t personality;

	term_cursor_t cur;   // row relative to top
	unsigned margin_top, margin_bottom; // scrolling region, relative, inclusive

	bool autowrap;        // DECAWM
	bool insert_mode;     // IRM
	bool newline_mode;    // LNM: LF, VT and FF also do CR
	bool backspace_bs;    // DECBKM: Backspace sends BS (else DEL), for the keyboard
	bool reverse_screen;  // DECSCNM

	uint8_t tabs[(SCREEN_MAX_COLS + 7) / 8];
	unsigned screen_resets; // times DECCOLM cleared the whole screen
	unsigned setup_requests; // times the host sent CSI ? 999 ~

	term_reply_t reply;
	void *reply_ctx;

	term_vt100_t vt100;
} term_t;

// Reset the terminal (as RIS), clear its rows, home the cursor and show it.
// Cells get palette indices: default text PALETTE_TEXT (bold:
// PALETTE_BOLD_TEXT) on PALETTE_BACKGROUND, ANSI colours 0-15 (bold brightens
// 0-7 to 8-15), 256-colour ones as they are and direct ones the nearest
// xterm entry; reverse swaps fg and bg.
void term_init(term_t *t, screen_t *screen, unsigned top, unsigned rows);

// Reset as RIS (ESC c) does, and also abandon any escape sequence or string
// the parser is in the middle of, so it recovers a terminal stuck in an
// unterminated OSC or DCS string. Modes go back to their SET-UP defaults;
// the width (80/132) is left alone.
void term_reset(term_t *t);

// Where replies to the host (device attributes, cursor position reports)
// go. Without one they are dropped.
void term_set_reply(term_t *t, term_reply_t reply, void *ctx);

// Returns how many bytes were used: all of them, unless a SET-UP request
// (CSI ? 999 ~) ended the call early.
size_t term_write(term_t *t, const uint8_t *data, size_t len);
void term_putc(term_t *t, uint8_t c);
void term_puts(term_t *t, const char *text);

// Set these modes now and as the values RIS resets them to.
void term_set_defaults(term_t *t, const term_defaults_t *d);

// Switch to 80 or 132 columns, as DECCOLM does (clears the whole screen).
void term_set_columns(term_t *t, unsigned cols);

#endif
