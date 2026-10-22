#ifndef SETUP_H
#define SETUP_H

// SET-UP: a panel over the bottom text rows for editing the settings kept
// in flash. Portable C (no pico-sdk), host-tested; the terminal (apps/terminal/setup_glue.c) opens it,
// feeds it key bytes and saves.
//
// Keys arrive as the bytes a VT100 keyboard sends (from the USB keyboard or
// from the host): Up/Down (ESC [ A/B or ESC O A/B) select a setting,
// Left/Right and Space change it, Return saves, Escape cancels, T shows the
// demo screen, R reboots the board, U opens the firmware update (fwpanel.h),
// C the Clock panel (clockpanel.h), and with machine_menu D opens the Drives
// panel and M leaves for the Z80 machine's menu. A lone ESC
// is only known to be the Escape key once SETUP_ESC_TIMEOUT_MS pass without
// the rest of a sequence (setup_tick).

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

#define SETUP_ITEMS SETTINGS_MAIN_FIELDS // rows: settings_main_field gives each one's field
// In a box (screen_frame): the top line with the title, the settings with
// two preview rows under the color ones, message, two help rows, the
// bottom line.
#define SETUP_ROWS (SETUP_ITEMS + 7)
#define SETUP_ESC_TIMEOUT_MS 100

typedef enum {
	SETUP_NONE,   // nothing changed
	SETUP_REDRAW, // the panel changed
	SETUP_SAVE,   // Return: save values
	SETUP_CANCEL, // Escape: leave without saving
	SETUP_MACHINE, // M (with machine_menu): leave for the Z80 machine's menu
	SETUP_DRIVES,  // D (with machine_menu): the Drives panel (drivepanel.h)
	SETUP_DEMO,    // T: show the demo screen (demo.h) until a key
	SETUP_REBOOT,  // R: restart the board (unsaved changes are dropped)
	SETUP_FIRMWARE, // U: the firmware update panel (fwpanel.h)
	SETUP_CLOCK,    // C: the Clock panel (clockpanel.h)
} setup_action_t;

typedef struct {
	settings_t values;   // as edited
	unsigned item;       // the selected setting
	uint8_t esc;         // 0, or 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;     // when the ESC arrived
	const char *message; // shown in the panel until the next key; NULL: none
	bool machine_menu;   // offer M for the Z80 machine's menu (set after setup_open)
	bool no_bell;        // the board has no bell: Bell shows off, fixed (set after setup_open)
} setup_t;

void setup_open(setup_t *u, const settings_t *values);

// One byte of key input, received at now_ms.
setup_action_t setup_input(setup_t *u, uint8_t c, uint32_t now_ms);

// Call regularly: a lone ESC becomes SETUP_CANCEL after the timeout.
setup_action_t setup_tick(setup_t *u, uint32_t now_ms);

// Draw the panel on rows top .. top + SETUP_ROWS - 1: a box in border, the
// text in fg on bg, the selected setting's name in bold and every current
// choice reversed (told apart by more than color, for the mono palette);
// title is shown after "SET-UP" in the top line. Under Colors and
// Palette, a preview of the chosen colors: default text, bold and status
// line, and the 16 ANSI colors as text and as backgrounds.
void setup_draw(const setup_t *u, screen_t *s, unsigned top, const char *title, uint8_t fg, uint8_t bg,
	uint8_t border);

#endif
