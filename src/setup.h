#ifndef SETUP_H
#define SETUP_H

// SET-UP: a panel over the bottom text rows for editing the settings kept
// in flash. Portable C (no pico-sdk), host-tested; main.c opens it, feeds it
// key bytes and saves.
//
// Keys arrive as the bytes a VT100 keyboard sends (from the USB keyboard or
// from the host): Up/Down (ESC [ A/B or ESC O A/B) select a setting,
// Left/Right and Space change it, Return saves, Escape cancels. A lone ESC
// is only known to be the Escape key once SETUP_ESC_TIMEOUT_MS pass without
// the rest of a sequence (setup_tick).

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

#define SETUP_ITEMS 3
#define SETUP_ROWS (SETUP_ITEMS + 3) // title, the settings, message, help
#define SETUP_ESC_TIMEOUT_MS 100

typedef enum {
	SETUP_NONE,   // nothing changed
	SETUP_REDRAW, // the panel changed
	SETUP_SAVE,   // Return: save values
	SETUP_CANCEL, // Escape: leave without saving
} setup_action_t;

typedef struct {
	settings_t values;   // as edited
	unsigned item;       // the selected setting
	uint8_t esc;         // 0, or 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;     // when the ESC arrived
	const char *message; // shown in the panel until the next key; NULL: none
} setup_t;

void setup_open(setup_t *u, const settings_t *values);

// One byte of key input, received at now_ms.
setup_action_t setup_input(setup_t *u, uint8_t c, uint32_t now_ms);

// Call regularly: a lone ESC becomes SETUP_CANCEL after the timeout.
setup_action_t setup_tick(setup_t *u, uint32_t now_ms);

// Draw the panel on rows top .. top + SETUP_ROWS - 1 in fg/bg (the current
// choices reversed); title is shown after "SET-UP".
void setup_draw(const setup_t *u, screen_t *s, unsigned top, const char *title, uint8_t fg, uint8_t bg);

#endif
