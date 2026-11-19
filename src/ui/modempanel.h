#ifndef MODEMPANEL_H
#define MODEMPANEL_H

// SET-UP's Modem panel (bd xir.17.4): Return on SET-UP's Port row with the
// WiFi modem in use. The network the firmware keeps for the modem (pushed
// to it at start-up and on Return here), the modem's state, a reset, and
// its factory settings. Portable C, host-tested (test_setup); the terminal
// (apps/terminal/modem_glue.c) opens it over SET-UP's rows and fills in
// the modem's state.
//
// Up/Down select a row. On Network and Password printable characters are
// added at the end and Backspace (BS or DEL) takes the last one off; the
// password shows as stars. Return on either saves the network and joins
// it (MODEMPANEL_JOIN); on Reset modem resets and sets it up again
// (MODEMPANEL_RESET); on Defaults, pressed twice, gives the module its
// factory settings and then ours (MODEMPANEL_DEFAULTS). Escape goes back
// to SET-UP without changes.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

#if WIFI_MODEM

// In a box: the title line, Network, Password, Address, Reset modem,
// Defaults, a gap, Status, Firmware, Problem, a gap, message, 2 help
// lines, the bottom line.
#define MODEMPANEL_ROWS 16
#define MODEMPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	MODEMPANEL_NONE,
	MODEMPANEL_REDRAW,
	MODEMPANEL_JOIN,     // Return on Network or Password: save the network, join it
	MODEMPANEL_RESET,    // Return on Reset modem
	MODEMPANEL_DEFAULTS, // Return twice on Defaults
	MODEMPANEL_CANCEL,   // Escape
} modempanel_action_t;

#define MODEMPANEL_NETWORK 0
#define MODEMPANEL_PASSWORD 1
#define MODEMPANEL_RESET_LINE 2
#define MODEMPANEL_DEFAULTS_LINE 3
#define MODEMPANEL_LINES 4

// The modem as the owner sees it (all its strings the owner's; NULL or ""
// for unknown).
typedef struct {
	const char *state;   // modem_step_name: "ready", "finding its rate", ...
	bool busy;           // starting: the line is its own
	bool joined;
	const char *ssid;    // the network it is set to (ATI3)
	const char *ip;      // its address (ATI2)
	const char *version; // ATI4
	const char *rom;     // the ESP's boot text: reset cause, boot mode
	const char *problem; // the start-up's last problem
} modempanel_status_t;

typedef struct {
	settings_t values; // SET-UP's edited settings, the network changed here
	unsigned line;     // MODEMPANEL_*
	uint8_t esc;       // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;
	bool confirm;      // Defaults pressed once
	const char *message;
	modempanel_status_t status; // the owner's, before each draw
} modempanel_t;

void modempanel_open(modempanel_t *p, const settings_t *values);

modempanel_action_t modempanel_input(modempanel_t *p, uint8_t c, uint32_t now_ms);
// Call regularly: a lone ESC becomes MODEMPANEL_CANCEL after the timeout.
modempanel_action_t modempanel_tick(modempanel_t *p, uint32_t now_ms);

// Draw it like SET-UP (setup_draw): a box in border, text in fg on bg.
void modempanel_draw(const modempanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

#endif

#endif
