// The WiFi modem's start-up on the board (bd xir.17.2): src/modem/modem.c
// on the terminal UART and the modem's RST line, from power-on in WiFi
// modem mode (the Port setting, WIFI_MODEM builds). While it runs the line
// is its own: the terminal, CP/M's SIO2, the printer and keys leave it
// alone (modem_glue_busy). Cold: in flash (ld/).

#include "terminal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "modem_lines.h"
#include "palette.h"
#include "serial.h"
#include "video.h"

#if WIFI_MODEM

#include "modem.h"

// Zimodem's GPIOs for the lines our pins see (S47, S55).
#ifndef BOARD_MODEM_ZIMODEM_DCD
#define BOARD_MODEM_ZIMODEM_DCD 4
#endif
#ifndef BOARD_MODEM_ZIMODEM_DTR
#define BOARD_MODEM_ZIMODEM_DTR 12
#endif

static modem_t modem;
static bool started;

static void io_reset(void *ctx, bool hold) {
	(void)ctx;
	modem_hold_reset(hold);
}

static void io_baud(void *ctx, unsigned baud) {
	(void)ctx;
	serial_configure(baud, 8, 'N', 1); // RTS/CTS stays as apply_line set it
}

static void io_send(void *ctx, const char *text) {
	(void)ctx;
	size_t n = strlen(text);
	while (n) { // short commands: the queue takes them, waiting only if full
		size_t sent = serial_write((const uint8_t *)text, n);
		text += sent;
		n -= sent;
		if (n) {
			serial_poll();
		}
	}
}

static bool io_drained(void *ctx) {
	(void)ctx;
	return serial_tx_drained();
}

static const modem_io_t io = {NULL, io_reset, io_baud, io_send, io_drained};

void modem_glue_start(void) {
	if (!settings_wifi(&settings)) {
		return;
	}
	modem_config_t cfg = {
		.baud = SETTINGS_MODEM_BAUD,
		.echo = settings.mode == SETTINGS_MODE_CPM, // Hayes for CP/M programs; quiet for the terminal
		.dcd_pin = BOARD_MODEM_ZIMODEM_DCD,
		.dtr_pin = BOARD_MODEM_ZIMODEM_DTR,
		.ssid = settings.wifi_ssid,
		.password = settings.wifi_password,
	};
	modem_init(&modem, &io, &cfg);
	modem_start(&modem, to_ms_since_boot(get_absolute_time()));
	started = true;
	modem_owns_line = true;
}

void modem_glue_poll(void) {
	if (!modem_glue_busy()) {
		return;
	}
	uint32_t now = to_ms_since_boot(get_absolute_time());
	uint8_t buf[64];
	size_t n;
	while ((n = serial_read(buf, sizeof buf)) > 0) {
		for (size_t i = 0; i < n; ++i) {
			modem_feed(&modem, buf[i], now);
		}
	}
	modem_poll(&modem, now);
	serial_poll();
	modem_owns_line = modem_busy(&modem);
}

const char *modem_glue_notice(void) {
	static char text[40];
	if (!modem_glue_busy()) {
		return NULL;
	}
	snprintf(text, sizeof text, "WiFi modem: %s", modem_step_name(&modem));
	return text;
}

modem_status_t modem_glue_status(void) {
	if (!started) {
		return MODEM_STATUS_NONE;
	}
	switch (modem.state) {
	case MODEM_STARTING:
		return MODEM_STATUS_STARTING;
	case MODEM_READY:
		return modem_joined(&modem) ? MODEM_STATUS_WIFI : MODEM_STATUS_NO_WIFI;
	case MODEM_FAILED:
		return MODEM_STATUS_FAILED;
	default:
		return MODEM_STATUS_NONE;
	}
}

bool modem_glue_carrier(void) {
	return started && modem_dcd();
}

// HWINFO's record (port 12): the module, what the start-up found, its
// lines. HWINFO wraps the detail; a long network name cuts the lines off.
#define PIN(n) "GP" BOARD_STR(n)
#define MODEM_LINES BOARD_UART_NAME " " BOARD_STR(BOARD_MODEM_BAUD) " RTS/CTS, DCD/DTR/RST/BOOT " \
	PIN(BOARD_MODEM_DCD_PIN) "/" BOARD_STR(BOARD_MODEM_DTR_PIN) "/" BOARD_STR(BOARD_MODEM_RST_PIN) "/" \
	BOARD_STR(BOARD_MODEM_BOOT_PIN)
#define MODEM_CHIP BOARD_MODEM_CHIP_NAME " (" BOARD_MODEM_NAME ")"

size_t modem_record(uint8_t *buf) {
	char text[192]; // the longest names fit; hwinfo_record cuts to HWINFO_MAX
	uint8_t st = HWINFO_ST_PRESENT;
	const char *version = modem.version[0] ? modem.version : "?";
	switch (modem_glue_status()) {
	case MODEM_STATUS_NONE:
		return hwinfo_record(buf, HWINFO_ST_ABSENT, "WiFi modem",
			MODEM_CHIP ": off, Port is the serial line (SET-UP); " MODEM_LINES);
	case MODEM_STATUS_STARTING:
		snprintf(text, sizeof text, MODEM_CHIP ": %s; " MODEM_LINES, modem_step_name(&modem));
		break;
	case MODEM_STATUS_WIFI:
		snprintf(text, sizeof text, MODEM_CHIP ", Zimodem %s, on %s as %s%s; " MODEM_LINES, version, modem.ssid,
			modem.ip, modem_glue_carrier() ? ", connected" : "");
		break;
	case MODEM_STATUS_NO_WIFI:
		snprintf(text, sizeof text, MODEM_CHIP ", Zimodem %s, no network (SET-UP's Modem panel); " MODEM_LINES,
			version);
		break;
	default:
		st = HWINFO_ST_NOT_FOUND;
		snprintf(text, sizeof text, MODEM_CHIP " did not answer (SET-UP's Modem panel: Reset modem); " MODEM_LINES);
		break;
	}
	return hwinfo_record(buf, st, "WiFi modem", text);
}

// ---- the Modem panel in SET-UP (Return on the Port row, bd xir.17.4) ----

static_assert(MODEMPANEL_ROWS <= SETUP_ROWS, "the Modem panel fits SET-UP's rows");

static void panel_status(modempanel_status_t *st) {
	*st = (modempanel_status_t){
		.state = started ? modem_step_name(&modem) : "not started",
		.busy = modem_glue_busy(),
		.joined = started && modem_joined(&modem),
		.ssid = modem.ssid,
		.ip = modem.ip,
		.version = modem.version,
		.rom = modem.rom,
		.problem = modem.problem,
	};
}

// What the panel shows of the modem, to redraw it when that changes.
static char panel_shown[160];

static void panel_fingerprint(char *out, size_t size) {
	snprintf(out, size, "%s|%d|%s|%s|%s|%s", modem_step_name(&modem), modem_glue_busy(), modem.ip, modem.ssid,
		modem.version, modem.problem ? modem.problem : "");
}

static void modem_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - MODEMPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	panel_status(&setup.modem.status);
	panel_fingerprint(panel_shown, sizeof panel_shown);
	modempanel_draw(&setup.modem, &screen, SETUP_TOP + SETUP_ROWS - MODEMPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
}

void modem_panel_open(void) {
	modempanel_open(&setup.modem, &setup.ui.values);
	setup.modem_open = true;
	setup.modem_work = MODEMPANEL_NONE;
	modem_draw_panel();
}

void modem_panel_feed(uint8_t c, uint32_t now) {
	modempanel_action_t a = modempanel_input(&setup.modem, c, now);
	if (a == MODEMPANEL_REDRAW) {
		modem_draw_panel();
	} else if (a != MODEMPANEL_NONE) {
		setup.modem_work = a;
	}
}

// Return on Network or Password: the network saved (into SET-UP's edited
// settings too, so its own Return keeps it), then pushed to the module.
static void panel_join(uint32_t now) {
	const settings_t *v = &setup.modem.values;
	if (v->wifi_ssid[0] && !modem_network_sendable(v->wifi_ssid, v->wifi_password)) {
		setup.modem.message = "Zimodem cannot take a comma or quote in the name, a quote in the password";
		return;
	}
	settings_t s = settings;
	settings_set_wifi_ssid(&s, v->wifi_ssid);
	settings_set_wifi_password(&s, v->wifi_password);
	setup.modem.message = "Saving...";
	modem_draw_panel();
	video_update(&screen);
	if (!settings_store(&s)) {
		setup.modem.message = "Saving to flash failed";
		return;
	}
	settings_set_wifi_ssid(&setup.ui.values, v->wifi_ssid);
	settings_set_wifi_password(&setup.ui.values, v->wifi_password);
	modem_join(&modem, now); // cfg points at settings' texts, saved just now
	modem_owns_line = modem_busy(&modem);
	setup.modem.message = v->wifi_ssid[0] ? "Saved; joining the network" : "Saved: the module keeps its own network";
}

void modem_panel_service(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	modempanel_action_t work = setup.modem_work;
	setup.modem_work = MODEMPANEL_NONE;
	if (work == MODEMPANEL_NONE) {
		work = modempanel_tick(&setup.modem, now);
	}
	switch (work) {
	case MODEMPANEL_CANCEL:
		setup.modem_open = false;
		setup_draw_panel();
		return;
	case MODEMPANEL_JOIN:
		panel_join(now);
		break;
	case MODEMPANEL_RESET:
		modem_start(&modem, now);
		modem_owns_line = true;
		setup.modem.message = "Resetting it; the line is the modem's until it is set up";
		break;
	case MODEMPANEL_DEFAULTS:
		modem_factory(&modem, now);
		modem_owns_line = modem_busy(&modem);
		setup.modem.message = "Its factory settings, then ours: this takes a while";
		break;
	default:
		break;
	}
	char now_shown[sizeof panel_shown];
	panel_fingerprint(now_shown, sizeof now_shown);
	if (work != MODEMPANEL_NONE || strcmp(now_shown, panel_shown) != 0) {
		modem_draw_panel();
	}
}

#else

void modem_glue_start(void) {
}

const char *modem_glue_notice(void) {
	return NULL;
}

void modem_glue_poll(void) {
}

modem_status_t modem_glue_status(void) {
	return MODEM_STATUS_NONE;
}

bool modem_glue_carrier(void) {
	return false;
}

size_t modem_record(uint8_t *buf) {
	(void)buf;
	return 0;
}

#endif
