// The WiFi modem's start-up on the board (bd xir.17.2): src/modem/modem.c
// on the terminal UART and the modem's RST line, from power-on in WiFi
// modem mode (the Port setting, WIFI_MODEM builds). While it runs the line
// is its own: the terminal, CP/M's SIO2, the printer and keys leave it
// alone (modem_glue_busy). Cold: in flash (ld/).

#include "terminal.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "modem_lines.h"
#include "serial.h"

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

void modem_glue_describe(char *text, size_t size) {
	if (!started) {
		snprintf(text, size, "Modem: not started");
	} else if (modem.state == MODEM_READY) {
		snprintf(text, size, "Modem %s: %s%s%s", modem.version[0] ? modem.version : "ready",
			modem_joined(&modem) ? modem.ssid : "no network",
			modem_joined(&modem) ? " " : "", modem_joined(&modem) ? modem.ip : "");
	} else {
		snprintf(text, size, "Modem: %s%s%s", modem_step_name(&modem), modem.problem ? ": " : "",
			modem.problem ? modem.problem : "");
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

void modem_glue_describe(char *text, size_t size) {
	snprintf(text, size, "No WiFi modem in this build");
}

#endif
