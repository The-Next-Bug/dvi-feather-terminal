#ifndef PRINTPANEL_H
#define PRINTPANEL_H

// SET-UP's Printer panel (P, bd wt6.5): TTY2 as a printer through a
// Zimodem WiFi modem (printjob.h). Portable C, host-tested; the terminal
// (apps/terminal/setup_glue.c) opens it over SET-UP's rows.
//
// Up/Down select a row. The settings' rows (settings.h's
// SETTINGS_PAGE_PRINTER fields) step through their choices with
// Left/Right or Space. On the Target row printable characters are added at
// the end and Backspace (BS or DEL) takes the last one off. Return keeps
// them in SET-UP's edited settings, which SET-UP's Return saves; Escape
// goes back without changes.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

// In a box: the title line, the three settings, Target, a gap, two lines
// of what it means, the last job, a gap, message, 2 help lines, the bottom
// line.
#define PRINTPANEL_ROWS 14
#define PRINTPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	PRINTPANEL_NONE,
	PRINTPANEL_REDRAW,
	PRINTPANEL_APPLY,  // Return: the settings into SET-UP's, then close
	PRINTPANEL_CANCEL, // Escape
} printpanel_action_t;

#define PRINTPANEL_TARGET 3 // the Target row, after the three settings
#define PRINTPANEL_LINES 4

typedef struct {
	settings_t values;  // SET-UP's edited settings, the printer's changed here
	unsigned line;      // 0-2 the settings, PRINTPANEL_TARGET
	uint8_t esc;        // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;
	const char *message;
	const char *last_job; // what the last job did (the owner's; NULL: none yet)
} printpanel_t;

void printpanel_open(printpanel_t *p, const settings_t *values);

printpanel_action_t printpanel_input(printpanel_t *p, uint8_t c, uint32_t now_ms);
// Call regularly: a lone ESC becomes PRINTPANEL_CANCEL after the timeout.
printpanel_action_t printpanel_tick(printpanel_t *p, uint32_t now_ms);

// Draw it like SET-UP (setup_draw): a box in border, text in fg on bg.
void printpanel_draw(const printpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

#endif
