// DVI serial terminal for the Adafruit Feather RP2040 DVI.
//
// Core 1 renders the screen over DVI (video.c). Core 0 receives UART0 into
// a ring buffer (serial.c), feeds it to the VT100 emulator (term.c) that
// owns rows 0-23, queues its replies back on UART0 TX, and rasterises changed
// lines; row 24 is a status line. With -DSERIAL_FLOW_CONTROL=ON, UART0 uses
// RTS/CTS on GP3/GP2 (the STEMMA QT pins). With USB_KEYBOARD (RP2350 builds),
// a USB keyboard on the native USB port (usbkbd.c) types to the host through
// UART0 TX.
//
// With STARTUP_DEMO (the default), the demo screen alternates between 80
// and 132 columns every DEMO_INTERVAL_MS until the first byte arrives on
// UART0 or a key is pressed; then the terminal starts, with that byte.

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "demo.h"
#include "diag.h"
#include "screen.h"
#include "term.h"
#include "term_build.h"
#include "serial.h"
#include "video.h"

#ifndef STARTUP_DEMO
#define STARTUP_DEMO 1
#endif

#ifndef USB_KEYBOARD
#define USB_KEYBOARD 0
#endif

#if USB_KEYBOARD
#include "usbkbd.h"
#endif

#ifndef SERIAL_FLOW_CONTROL
#define SERIAL_FLOW_CONTROL 0
#endif

#define BAUD 115200
// UART0 hardware flow control pins (RP2040 UART0 CTS/RTS function).
#define CTS_PIN 2
#define RTS_PIN 3
#define TERM_COLS 80
#define DEMO_INTERVAL_MS 5000

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)
#define STATUS_FG RGB222(3, 3, 3)
#define STATUS_BG RGB222(0, 0, 2)

#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define STATUS_INTERVAL_MS 250

static screen_t screen;
static term_t term;
static bool term_started;
static bool key_pressed; // since run_demo started

// With VIDEO_DIAG, report core 1's timing on UART0 TX twice a second.
static void diag_poll(void) {
	static absolute_time_t next;
	if (!VIDEO_DIAG || !time_reached(next))
		return;
	next = make_timeout_time_ms(500);
	video_stats_t st = video_stats();
	char text[160];
	snprintf(text, sizeof text,
		"video: core1 stage %lu  frames %lu  max encode %lu of %lu cycles/line (%lu%%)  late %lu  clk %lu kHz\r\n",
		(unsigned long)st.core1_stage, (unsigned long)st.frames, (unsigned long)st.max_line_cycles, (unsigned long)st.cycles_budget,
		(unsigned long)(st.max_line_cycles * 100 / st.cycles_budget), (unsigned long)st.late,
		(unsigned long)(clock_get_hz(clk_sys) / 1000));
	uart_puts(uart0, text);
	diag_heartbeat();
}

#if USB_KEYBOARD
// Keystrokes go to the host like replies, through the TX queue.
static void send_key(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	key_pressed = true;
	serial_write(data, len);
}

// The terminal modes that change what keys send (none before it starts).
static void keyboard_poll(void) {
	kbd_modes_t modes = {false, false, false};
	if (term_started) {
		modes.cursor_keys_app = term.cursor_keys_app;
		modes.keypad_app = term.keypad_app;
		modes.newline_mode = term.newline_mode;
	}
	usbkbd_poll(&modes);
}

static unsigned keyboards_attached(void) {
	return usbkbd_count();
}
#else
static void keyboard_poll(void) {
}

static unsigned keyboards_attached(void) {
	return 0;
}
#endif

// Cycle the demo until UART0 receives something or a key is pressed; return
// what arrived.
static size_t run_demo(uint8_t *buf, size_t max) {
	char status[SCREEN_MAX_COLS + 1];
	for (unsigned cols = 80;; cols = cols == 80 ? 132 : 80) {
		snprintf(status, sizeof status, " demo | %u x 24 | build %s (%s, %s)",
			cols, TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
		demo_draw(&screen, cols, status);
		video_update(&screen);

		absolute_time_t next = make_timeout_time_ms(DEMO_INTERVAL_MS);
		while (!time_reached(next)) {
			size_t n = serial_read(buf, max);
			if (n || key_pressed)
				return n;
			keyboard_poll();
			serial_poll();
			diag_poll();
		}
	}
}

// Replies to the host (device attributes, cursor position reports).
static void send_reply(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	serial_write(data, len);
}

typedef struct {
	serial_stats_t serial;
	unsigned keyboards;
} status_t;

static status_t status_now(void) {
	return (status_t){serial_stats(), keyboards_attached()};
}

static bool status_equal(const status_t *a, const status_t *b) {
	return a->serial.rx_level == b->serial.rx_level && a->serial.tx_level == b->serial.tx_level &&
		a->serial.errors == b->serial.errors && a->serial.throttled == b->serial.throttled &&
		a->keyboards == b->keyboards;
}

static void draw_status(const status_t *st) {
	char text[SCREEN_MAX_COLS + 1];
	// Buffer levels: bytes waiting to be displayed (rx) and sent (tx).
	int n = snprintf(text, sizeof text, " VT100 %ux24 | %s | %u 8N1%s%s | rx buf %lu  tx buf %lu  err %lu",
		screen.cols, TERM_BUILD_ID, BAUD, SERIAL_FLOW_CONTROL ? " RTS/CTS" : "",
		st->keyboards ? " | kbd" : "",
		(unsigned long)st->serial.rx_level, (unsigned long)st->serial.tx_level,
		(unsigned long)st->serial.errors);
	if (SERIAL_FLOW_CONTROL && n > 0 && (size_t)n < sizeof text)
		snprintf(text + n, sizeof text - (size_t)n, "  hold %lu", (unsigned long)st->serial.throttled);
	screen_clear_line(&screen, STATUS_ROW, STATUS_FG, STATUS_BG);
	screen_puts(&screen, 0, STATUS_ROW, text, STATUS_FG, STATUS_BG);
}

int main(void) {
	diag_init();
	video_init();
	// After video_init: the baud divisor depends on the final clk_peri.
	// Diagnostic builds write to UART0 with blocking calls, which an unwired
	// CTS (pulled up on the Feather) would hold off forever: RTS only there.
	serial_init(uart0, BAUD, PICO_DEFAULT_UART_TX_PIN, PICO_DEFAULT_UART_RX_PIN,
		SERIAL_FLOW_CONTROL && !VIDEO_DIAG ? CTS_PIN : SERIAL_NO_PIN,
		SERIAL_FLOW_CONTROL ? RTS_PIN : SERIAL_NO_PIN);
	diag_mark("serial_init done");
#if USB_KEYBOARD
	usbkbd_init(send_key, NULL);
	diag_mark("usbkbd_init done");
#endif

	screen_init(&screen, TERM_COLS, FG, BG);
	video_start(&screen);
	diag_mark("video_start done (core 1 launched)");

	uint8_t buf[256];
	size_t pending = STARTUP_DEMO ? run_demo(buf, sizeof buf) : 0;

	screen_init(&screen, TERM_COLS, FG, BG);
	term_init(&term, &screen, 0, TERM_ROWS, FG, BG);
	term_set_reply(&term, send_reply, NULL);
	term_started = true;
	status_t shown = status_now();
	unsigned shown_resets = term.screen_resets;
	draw_status(&shown);

	char banner[SCREEN_MAX_COLS + 1];
	snprintf(banner, sizeof banner, "DVI Feather Terminal build %s (%s, %s)\r\n",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	term_puts(&term, banner);
	term_puts(&term, SERIAL_FLOW_CONTROL ? "UART0 on TX (GP0) / RX (GP1), RTS (GP3) / CTS (GP2)\r\n\r\n"
		: "UART0 on TX (GP0) / RX (GP1)\r\n\r\n");
	if (pending)
		term_write(&term, buf, pending);
	video_update(&screen);

	absolute_time_t next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
	while (true) {
		size_t n = serial_read(buf, sizeof buf);
		if (n)
			term_write(&term, buf, n);
		keyboard_poll();
		serial_poll();

		// DECCOLM re-initialises the whole screen, status line included,
		// whether or not the width changes.
		if (term.screen_resets != shown_resets) {
			shown_resets = term.screen_resets;
			draw_status(&shown);
		}
		if (time_reached(next_status)) {
			next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
			status_t st = status_now();
			if (!status_equal(&st, &shown)) {
				shown = st;
				draw_status(&shown);
			}
		}
		video_update(&screen);
		diag_poll();
	}
}
