#ifndef TELNETPANEL_H
#define TELNETPANEL_H

// SET-UP's Telnet panel (N in terminal mode, or F11 straight to its Host
// row, bd xir.10): telnet for a host reached through a Zimodem with
// ATD"host:port" (raw TCP; telnet.h). Portable C, host-tested; the
// terminal (apps/terminal/setup_glue.c, telnet_glue.c) opens it over
// SET-UP's rows.
//
// Up/Down select a row. Telnet steps through its choices with Left/Right
// or Space. On Terminal type and Host printable characters (no spaces) are
// added at the end and Backspace (BS or DEL) takes the last one off.
// Return on Host dials it (TELNETPANEL_DIAL: the panel's choices are saved
// first), on Call hangs up (TELNETPANEL_HANGUP), elsewhere keeps the
// choices in SET-UP's edited settings (TELNETPANEL_APPLY); Escape goes back
// without changes.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

// In a box: the title line, Telnet, Terminal type, Host, Call, a gap, three
// lines of what it means, a gap, message, 2 help lines, the bottom line.
#define TELNETPANEL_ROWS 14
#define TELNETPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	TELNETPANEL_NONE,
	TELNETPANEL_REDRAW,
	TELNETPANEL_APPLY,  // Return: the choices into SET-UP's, then close
	TELNETPANEL_DIAL,   // Return on Host: save the choices, dial values.telnet_host
	TELNETPANEL_HANGUP, // Return on Call while one is up
	TELNETPANEL_CANCEL, // Escape
} telnetpanel_action_t;

#define TELNETPANEL_MODE 0
#define TELNETPANEL_TYPE 1
#define TELNETPANEL_HOST 2
#define TELNETPANEL_CALL 3
#define TELNETPANEL_LINES 4

typedef struct {
	settings_t values;  // SET-UP's edited settings, telnet's changed here
	unsigned line;      // TELNETPANEL_*
	uint8_t esc;        // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;
	const char *message;
	const char *type_default; // the Terminal type's name (shown when no override); the owner's
	const char *call;   // the host of the call dialed from here, NULL: none; the owner's
} telnetpanel_t;

// Opened at line (TELNETPANEL_HOST from F11, TELNETPANEL_CALL in a call).
void telnetpanel_open(telnetpanel_t *p, const settings_t *values, unsigned line);

telnetpanel_action_t telnetpanel_input(telnetpanel_t *p, uint8_t c, uint32_t now_ms);
// Call regularly: a lone ESC becomes TELNETPANEL_CANCEL after the timeout.
telnetpanel_action_t telnetpanel_tick(telnetpanel_t *p, uint32_t now_ms);

// Draw it like SET-UP (setup_draw): a box in border, text in fg on bg.
void telnetpanel_draw(const telnetpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

#endif
