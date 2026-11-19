// Telnet in terminal mode (bd xir.10): the host's bytes through telnet.c
// before the terminal, the terminal's (keys, replies) through it to the
// host, the window size and terminal type kept up to date, and the call
// dialed from the Telnet panel (F11): ATD"host" to a Zimodem at its AT
// prompt (raw TCP: no T modifier), telnet on for the call, hung up with
// +++ between guard times and ATH. With the WiFi modem's lines (bd
// xir.17.5: the Port on the modem, its start-up done) DTR hangs up instead
// (carrier.c; +++ and ATH if the call stays up) and DCD going inactive ends
// the call, whoever ended it. Cold: kept in flash (ld/), only the filter
// (telnet.c, dvt_term) is in SRAM.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "palette.h"
#include "serial.h"
#include "settings.h"
#include "telnet.h"
#include "terminal.h"
#if WIFI_MODEM
#include "carrier.h"
#include "modem_lines.h"
#endif

_Static_assert(SETTINGS_TELNET_OFF == TELNET_OFF && SETTINGS_TELNET_AUTO == TELNET_AUTO &&
	SETTINGS_TELNET_ON == TELNET_ON, "the setting is telnet.h's mode");
_Static_assert(SETTINGS_TYPE_MAX == TELNET_TYPE_MAX, "the override fits TTYPE");

// Zimodem's escape: no byte for its guard time (S12, 1 s by default)
// either side of +++; printjob.c waits as long.
#define GUARD_MS 1500

enum { HANG_NONE, HANG_GUARD_BEFORE, HANG_GUARD_AFTER, HANG_DTR };

static telnet_t tn;
static struct {
	bool active;                         // dialed from the panel and not hung up
	char host[SETTINGS_HOST_MAX + 1];
	uint8_t hangup;                      // HANG_*
	uint32_t at_ms;                      // the guard time's start
} call;
static unsigned host_resets_seen;
#if WIFI_MODEM
static carrier_t carrier;
static bool carrier_on; // carrier initialized for the modem's lines
#endif

// The WiFi modem's DCD and DTR are there to use: the Port on the modem,
// its start-up over (it pulls DCD about while it resets).
static bool modem_lines(void) {
#if WIFI_MODEM
	return settings_wifi(&settings) && !modem_glue_busy();
#else
	return false;
#endif
}

// The call is over (hung up, or the far end went): telnet as the setting
// says, negotiated from the start next time.
static void end_call(void) {
	call.hangup = HANG_NONE;
	call.active = false;
	telnet_glue_apply();
	telnet_reset(&tn);
}

#if WIFI_MODEM
static void carrier_service(uint32_t now) {
	if (!modem_lines()) {
		carrier_on = false;
		return;
	}
	if (!carrier_on) {
		carrier_on = true;
		carrier_init(&carrier, modem_dcd(), now);
	}
	carrier_event_t e = carrier_poll(&carrier, modem_dcd(), now);
	modem_set_dtr(carrier.dtr);
	if (e == CARRIER_LOST) {
		if (call.hangup != HANG_DTR) {
			end_call(); // the far end (or a manual ATH) closed it
		}
		telnet_reset(&tn);
	} else if (e == CARRIER_HUNG_UP && call.hangup == HANG_DTR) {
		end_call();
	} else if (e == CARRIER_STUCK && call.hangup == HANG_DTR) {
		call.hangup = HANG_GUARD_BEFORE; // DTR did nothing: the escape and ATH
		call.at_ms = now;
	}
}
#endif

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
	if (call.hangup != HANG_NONE || modem_glue_busy()) {
		return; // a byte now would spoil the guard time, or the modem's start-up
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
#if WIFI_MODEM
	carrier_service(now);
#endif
	if (call.hangup == HANG_GUARD_BEFORE && serial_tx_drained() && now - call.at_ms >= GUARD_MS) {
		serial_write((const uint8_t *)"+++", 3);
		call.hangup = HANG_GUARD_AFTER;
		call.at_ms = now;
	} else if (call.hangup == HANG_GUARD_AFTER && serial_tx_drained() && now - call.at_ms >= GUARD_MS) {
		serial_write((const uint8_t *)"ATH\r", 4);
		end_call();
	}
}

bool telnet_glue_dial(const char *host) {
	if (cpm.on || call.hangup != HANG_NONE || !host[0] || modem_glue_busy()) {
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
		call.at_ms = to_ms_since_boot(get_absolute_time());
#if WIFI_MODEM
		if (modem_lines() && carrier_on) {
			call.hangup = HANG_DTR;
			carrier_hangup(&carrier, call.at_ms);
			modem_set_dtr(carrier.dtr);
			return;
		}
#endif
		call.hangup = HANG_GUARD_BEFORE;
	}
}

bool telnet_glue_connected(void) {
	if (cpm.on) {
		return false;
	}
#if WIFI_MODEM
	if (carrier_on && carrier.up) {
		return true; // the modem's DCD: any call, dialed here or by hand
	}
#endif
	return call.active || tn.seen;
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
			setup.ui.message = modem_lines() ? "Hanging up (DTR)" : "Hanging up: +++ and ATH";
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

