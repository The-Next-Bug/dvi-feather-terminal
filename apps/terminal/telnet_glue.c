// Telnet in terminal mode (bd xir.10): the host's bytes through telnet.c
// before the terminal, the terminal's (keys, replies) through it to the
// host, the window size and terminal type kept up to date, and the call
// dialed from the Telnet panel (F11): ATD"host" to a Zimodem at its AT
// prompt (raw TCP: no T modifier), telnet on for the call, hung up with
// +++ between guard times and ATH. Cold: kept in flash (ld/), only the
// filter (telnet.c, dvt_term) is in SRAM.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "palette.h"
#include "serial.h"
#include "settings.h"
#include "telnet.h"
#include "terminal.h"

_Static_assert(SETTINGS_TELNET_OFF == TELNET_OFF && SETTINGS_TELNET_AUTO == TELNET_AUTO &&
	SETTINGS_TELNET_ON == TELNET_ON, "the setting is telnet.h's mode");
_Static_assert(SETTINGS_TYPE_MAX == TELNET_TYPE_MAX, "the override fits TTYPE");

// Zimodem's escape: no byte for its guard time (S12, 1 s by default)
// either side of +++; printjob.c waits as long.
#define GUARD_MS 1500

enum { HANG_NONE, HANG_GUARD_BEFORE, HANG_GUARD_AFTER };

static telnet_t tn;
static struct {
	bool active;                         // dialed from the panel and not hung up
	char host[SETTINGS_HOST_MAX + 1];
	uint8_t hangup;                      // HANG_*
	uint32_t at_ms;                      // the guard time's start
} call;
static unsigned host_resets_seen;

static void tn_send(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	serial_write(data, len);
}

static telnet_mode_t mode_now(void) {
	return call.active ? TELNET_ON : (telnet_mode_t)settings.telnet;
}

void telnet_glue_init(void) {
	telnet_init(&tn, tn_send, NULL);
	telnet_set_mode(&tn, mode_now());
	host_resets_seen = term.host_resets;
}

void telnet_glue_apply(void) {
	telnet_set_mode(&tn, mode_now());
}

size_t telnet_glue_filter(const uint8_t *in, size_t n, uint8_t *out) {
	return telnet_filter(&tn, in, n, out);
}

void telnet_glue_send(const uint8_t *data, size_t len) {
	if (call.hangup != HANG_NONE) {
		return; // a byte now would spoil the guard time
	}
	telnet_send_data(&tn, data, len);
}

const char *telnet_type_default(void) {
	return term.personality == TERM_ADM3A ? "ADM3A" : term.vt100.vt52 ? "VT52" : "VT100";
}

void telnet_glue_poll(void) {
	if (cpm.on) {
		return;
	}
	if (term.host_resets != host_resets_seen) { // RIS: the negotiation is over
		host_resets_seen = term.host_resets;
		telnet_reset(&tn);
	}
	telnet_set_type(&tn, settings.telnet_type[0] ? settings.telnet_type : telnet_type_default());
	telnet_set_size(&tn, screen.cols, TERM_ROWS);
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (call.hangup == HANG_GUARD_BEFORE && serial_tx_drained() && now - call.at_ms >= GUARD_MS) {
		serial_write((const uint8_t *)"+++", 3);
		call.hangup = HANG_GUARD_AFTER;
		call.at_ms = now;
	} else if (call.hangup == HANG_GUARD_AFTER && serial_tx_drained() && now - call.at_ms >= GUARD_MS) {
		serial_write((const uint8_t *)"ATH\r", 4);
		call.hangup = HANG_NONE;
		call.active = false;
		telnet_glue_apply();
		telnet_reset(&tn); // the next call negotiates from the start
	}
}

bool telnet_glue_dial(const char *host) {
	if (cpm.on || call.hangup != HANG_NONE || !host[0]) {
		return false;
	}
	snprintf(call.host, sizeof call.host, "%s", host);
	call.active = true;
	telnet_glue_apply();
	telnet_reset(&tn);
	char cmd[SETTINGS_HOST_MAX + 8];
	int n = snprintf(cmd, sizeof cmd, "ATD\"%s\"\r", call.host);
	serial_write((const uint8_t *)cmd, (size_t)n);
	return true;
}

void telnet_glue_hangup(void) {
	if (call.active && call.hangup == HANG_NONE) {
		call.hangup = HANG_GUARD_BEFORE;
		call.at_ms = to_ms_since_boot(get_absolute_time());
	}
}

const char *telnet_glue_call(void) {
	return call.active ? call.host : NULL;
}

telnet_status_t telnet_glue_status(void) {
	if (cpm.on || tn.mode == TELNET_OFF) {
		return TELNET_STATUS_OFF;
	}
	if (call.hangup != HANG_NONE) {
		return TELNET_STATUS_HANGUP;
	}
	return tn.seen ? TELNET_STATUS_ACTIVE : TELNET_STATUS_WAITING;
}

// ---- the Telnet panel in SET-UP (N, or F11: direct) ----

// The Telnet panel, at the bottom of SET-UP's rows like the Drives panel.
static void telnet_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - TELNETPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	setup.telnet.call = telnet_glue_call();
	setup.telnet.type_default = telnet_type_default();
	telnetpanel_draw(&setup.telnet, &screen, SETUP_TOP + SETUP_ROWS - TELNETPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
}

// A key for the Telnet panel; its Return is taken in the main loop.
void telnet_feed(uint8_t c, uint32_t now) {
	telnetpanel_action_t a = telnetpanel_input(&setup.telnet, c, now);
	if (a == TELNETPANEL_REDRAW) {
		telnet_draw_panel();
	} else if (a != TELNETPANEL_NONE) {
		setup.telnet_work = a;
	}
}

void setup_open_telnet(bool direct) {
	if (!setup.active) {
		setup_enter(false);
	}
	telnetpanel_open(&setup.telnet, &setup.ui.values,
		telnet_glue_call() ? TELNETPANEL_CALL : direct ? TELNETPANEL_HOST : TELNETPANEL_MODE);
	setup.telnet_open = true;
	setup.telnet_direct = direct;
	setup.telnet_work = TELNETPANEL_NONE;
	telnet_draw_panel();
}

// The Telnet panel's Return or Escape. Its choices go into SET-UP's edited
// settings; Dial saves them (as SET-UP's Return does, with any other
// changes made in SET-UP) and leaves SET-UP to show the call. Opened by
// F11 (direct), Escape leaves SET-UP too and Return saves.
void telnet_service(void) {
	telnetpanel_action_t work = setup.telnet_work;
	setup.telnet_work = TELNETPANEL_NONE;
	if (work == TELNETPANEL_NONE) {
		work = telnetpanel_tick(&setup.telnet, to_ms_since_boot(get_absolute_time()));
		if (work == TELNETPANEL_NONE) {
			return;
		}
	}
	if (work == TELNETPANEL_CANCEL) {
		if (setup.telnet_direct) {
			setup_leave(false);
		} else {
			setup.telnet_open = false;
			setup_draw_panel();
		}
		return;
	}
	if (work == TELNETPANEL_HANGUP) {
		telnet_glue_hangup();
		if (setup.telnet_direct) {
			setup_leave(false);
		} else {
			setup.telnet_open = false;
			setup.ui.message = "Hanging up: +++ and ATH";
			setup_draw_panel();
		}
		return;
	}
	const settings_t *v = &setup.telnet.values;
	settings_t *u = &setup.ui.values;
	bool changed = v->telnet != u->telnet || strcmp(v->telnet_type, u->telnet_type) != 0 ||
		strcmp(v->telnet_host, u->telnet_host) != 0;
	u->telnet = v->telnet;
	settings_set_telnet_type(u, v->telnet_type);
	settings_set_telnet_host(u, v->telnet_host);
	setup.telnet_open = false;
	if (work == TELNETPANEL_DIAL || setup.telnet_direct) {
		char host[SETTINGS_HOST_MAX + 1];
		snprintf(host, sizeof host, "%s", u->telnet_host);
		if (save_and_leave() && work == TELNETPANEL_DIAL) {
			telnet_glue_dial(host);
		}
		return;
	}
	setup.ui.message = changed ? "Return here saves the telnet settings" : NULL;
	setup_draw_panel();
}

