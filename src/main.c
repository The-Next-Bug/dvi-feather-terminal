// DVI serial terminal for the Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock.
//
// Core 1 renders the screen over DVI (video.c). Core 0 receives UART0 into
// a ring buffer (serial.c), feeds it to the VT100 emulator (term.c) that
// owns rows 0-23, queues its replies back on UART0 TX, and rasterises changed
// lines; row 24 is a status line. With -DSERIAL_FLOW_CONTROL=ON, UART0 uses
// RTS/CTS on GP3/GP2. With USB_KEYBOARD (the default), a USB keyboard on the native USB port (usbkbd.c) types to the host through
// UART0 TX.
//
// At power-on/reset, with STARTUP_LOGO (the default), the logo (splash.h)
// shows for LOGO_MS; with STARTUP_DEMO (opt-in), the demo screen then
// alternates between 80 and 132 columns every DEMO_INTERVAL_MS. Either ends
// early when the first byte arrives on UART0 or a key is pressed; then the
// terminal starts, with that byte.
//
// SET-UP (setup.c) opens over the bottom text rows on the USB keyboard's
// F12 or Pause key, or when the host sends CSI ? 999 ~ (then keys come from UART0
// RX too, so it can be driven from the host's keyboard). The settings are
// kept in flash (nvflash.c, settings.c); the saved width is the power-on one.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "demo.h"
#include "diag.h"
#include "keyname.h"
#include "nvflash.h"
#include "screen.h"
#include "settings.h"
#include "setup.h"
#include "term.h"
#include "term_build.h"
#include "serial.h"
#include "video.h"

#ifndef STARTUP_LOGO
#define STARTUP_LOGO 1
#endif

#ifndef STARTUP_DEMO
#define STARTUP_DEMO 0
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

// UART0 hardware flow control pins (UART0 CTS/RTS function).
#define CTS_PIN 2
#define RTS_PIN 3
#define TERM_COLS 80
#define DEMO_INTERVAL_MS 5000
#ifndef LOGO_MS
#define LOGO_MS 10000
#endif

// The logo's screen; the terminal's colours are a setting.
#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)

#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define STATUS_INTERVAL_MS 250
#define SETUP_TOP (TERM_ROWS - SETUP_ROWS)

static_assert(NVFLASH_SECTORS == SETTINGS_SLOTS && NVFLASH_PAGE_SIZE == SETTINGS_RECORD_SIZE,
	"a settings slot is a flash sector holding one page");

static screen_t screen;
static term_t term;
static bool term_started;
static bool key_pressed; // before the terminal started: ends the logo and demo

static settings_t settings;          // as saved in flash
static settings_target_t save_target;
static settings_colours_t colours;   // in use (the status line's too)

// The last key sequence sent to the host, for the status line.
static struct {
	uint8_t data[8];
	uint8_t len;
	uint32_t count; // keys sent: tells a repeat of the same key apart
} last_key;

// SET-UP state; the panel's rows are saved while it covers them.
static struct {
	bool active, from_host;
	bool open_requested;  // by the SET-UP key
	setup_action_t action; // a save or cancel waiting for the main loop
	setup_t ui;
	screen_line_t saved[SETUP_ROWS];
	uint32_t saved_reverse;
	bool saved_cursor;
} setup;

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

static void setup_draw_panel(void) {
	char title[SCREEN_MAX_COLS + 1];
	snprintf(title, sizeof title, "  DVI Terminal build %s", TERM_BUILD_ID);
	setup_draw(&setup.ui, &screen, SETUP_TOP, title, colours.status_fg, colours.status_bg);
}

// Key bytes for SET-UP. A save or cancel is left for the main loop (the
// keyboard calls this from inside the USB stack); later bytes are dropped.
static void setup_feed(const uint8_t *data, size_t len) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	for (size_t i = 0; i < len && setup.action == SETUP_NONE; ++i) {
		setup_action_t a = setup_input(&setup.ui, data[i], now);
		if (a == SETUP_REDRAW)
			setup_draw_panel();
		else if (a != SETUP_NONE)
			setup.action = a;
	}
}

static void setup_enter(bool from_host) {
	// The modes now, whatever set them.
	settings_t current = settings;
	current.columns = screen.cols;
	current.backspace = term.backspace_bs ? 0x08 : 0x7f;
	current.newline = term.newline_mode;
	setup_open(&setup.ui, &current);
	setup.active = true;
	setup.from_host = from_host;
	setup.action = SETUP_NONE;
	for (unsigned r = 0; r < SETUP_ROWS; ++r)
		screen_save_row(&screen, SETUP_TOP + r, &setup.saved[r]);
	setup.saved_reverse = screen.reverse_rows;
	setup.saved_cursor = screen.cursor_visible;
	screen_set_reverse_rows(&screen, screen.reverse_rows & ~(((1u << SETUP_ROWS) - 1) << SETUP_TOP));
	screen.cursor_visible = false;
	setup_draw_panel();
}

// The serial line settings of s.
static void apply_line(const settings_t *s) {
	unsigned data_bits, stop_bits;
	char parity;
	settings_framing(s, &data_bits, &parity, &stop_bits);
	serial_configure(settings_baud_rate(s), data_bits, parity, stop_bits);
}

// Use settings s: the line, the modes RIS resets to (also set now) and the
// colours. The width is only used at power-on and by SET-UP; local echo is
// read from `settings` as keys are sent.
static void apply_settings(const settings_t *s) {
	apply_line(s);
	term_set_defaults(&term, &(term_defaults_t){.backspace_bs = s->backspace == 0x08,
		.newline_mode = s->newline});
	colours = settings_colours(s);
	term_set_colours(&term, colours.fg, colours.bold_fg, colours.bg);
	term_set_palette(&term, s->palette);
}

// Put the rows back; with apply, use the edited settings now, and the width
// if it differs (which clears the screen, as DECCOLM does).
static void setup_leave(bool apply) {
	for (unsigned r = 0; r < SETUP_ROWS; ++r)
		screen_restore_row(&screen, SETUP_TOP + r, &setup.saved[r]);
	screen_set_reverse_rows(&screen, setup.saved_reverse);
	screen.cursor_visible = setup.saved_cursor;
	setup.active = false;
	if (!apply)
		return;
	apply_settings(&setup.ui.values);
	if (setup.ui.values.columns != screen.cols)
		term_set_columns(&term, setup.ui.values.columns);
}

static bool setup_save(void) {
	static uint8_t page[SETTINGS_RECORD_SIZE]; // RAM, for the flash ROM routines
	settings_encode(&setup.ui.values, save_target.seq, page);
	if (!nvflash_write(save_target.slot, page))
		return false;
	settings = setup.ui.values;
	save_target.slot = (save_target.slot + 1) % SETTINGS_SLOTS;
	++save_target.seq;
	return true;
}

// Run SET-UP's pending save or cancel, or the Escape key timeout.
static void setup_service(void) {
	if (setup.action == SETUP_NONE)
		setup.action = setup_tick(&setup.ui, to_ms_since_boot(get_absolute_time()));
	setup_action_t a = setup.action;
	setup.action = SETUP_NONE;
	if (a == SETUP_CANCEL) {
		setup_leave(false);
	} else if (a == SETUP_SAVE) {
		// The erase holds core 0 for tens of ms: say so first.
		setup.ui.message = "Saving...";
		setup_draw_panel();
		video_update(&screen);
		if (setup_save()) {
			setup_leave(true);
		} else {
			setup.ui.message = "Saving to flash failed";
			setup_draw_panel();
		}
	}
}

#if USB_KEYBOARD
// Keystrokes go to the host like replies, through the TX queue (or to
// SET-UP while it is open). With local echo they are shown too.
static void send_key(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	key_pressed = true;
	if (setup.active) {
		setup_feed(data, len);
		return;
	}
	serial_write(data, len);
	last_key.len = (uint8_t)(len < sizeof last_key.data ? len : sizeof last_key.data);
	memcpy(last_key.data, data, last_key.len);
	++last_key.count;
	if (term_started && settings.local_echo)
		term_write(&term, data, len);
}

// The SET-UP key opens SET-UP, or leaves it without saving.
static void setup_key(void *ctx) {
	(void)ctx;
	key_pressed = true;
	if (!term_started)
		return;
	if (setup.active)
		setup.action = SETUP_CANCEL;
	else
		setup.open_requested = true;
}

// The terminal modes that change what keys send (none before it starts).
static void keyboard_poll(void) {
	kbd_modes_t modes = {.backspace_bs = settings.backspace == 0x08};
	if (term_started) {
		modes.cursor_keys_app = term.cursor_keys_app;
		modes.keypad_app = term.keypad_app;
		modes.newline_mode = term.newline_mode;
		modes.backspace_bs = term.backspace_bs;
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

// Wait until `until`, UART0 receiving something (copied into buf) or a key
// press. Returns whether either happened; *n is what arrived.
static bool wait_for_input(absolute_time_t until, uint8_t *buf, size_t max, size_t *n) {
	while (!time_reached(until)) {
		*n = serial_read(buf, max);
		if (*n || key_pressed)
			return true;
		keyboard_poll();
		serial_poll();
		diag_poll();
	}
	*n = 0;
	return false;
}

#if STARTUP_LOGO
// Show the logo for LOGO_MS, with the build on the status line. Returns
// whether input ended it; *n is what arrived.
static bool show_logo(uint8_t *buf, size_t max, size_t *n) {
	char status[SCREEN_MAX_COLS + 1];
	snprintf(status, sizeof status, " DVI Terminal | build %s (%s, %s)",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, status, colours.status_fg, colours.status_bg);
	video_update(&screen);
	*n = 0;
	// In the hue of the Colors setting (white keeps the logo's own colours,
	// or with the mono palette shows it grey).
	splash_tint_t tint = splash_tint(&splash_logo, colours.fg, settings.palette);
	if (!video_splash(&splash_logo, &tint))
		return false;
	bool input = wait_for_input(make_timeout_time_ms(LOGO_MS), buf, max, n);
	video_splash_end();
	return input;
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

		size_t n;
		if (wait_for_input(make_timeout_time_ms(DEMO_INTERVAL_MS), buf, max, &n))
			return n;
	}
}

// Replies to the host (device attributes, cursor position reports).
static void send_reply(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	serial_write(data, len);
}

typedef struct {
	unsigned keyboards;
	uint32_t keys_sent; // last_key.count
	settings_t settings;
} status_t;

static status_t status_now(void) {
	return (status_t){keyboards_attached(), last_key.count, settings};
}

static bool status_equal(const status_t *a, const status_t *b) {
	return a->keyboards == b->keyboards && a->keys_sent == b->keys_sent &&
		memcmp(&a->settings, &b->settings, sizeof a->settings) == 0;
}

static void draw_status(const status_t *st) {
	char key[32] = "";
	if (st->keys_sent) {
		char name[24];
		keyname(last_key.data, last_key.len, name, sizeof name);
		snprintf(key, sizeof key, " | key %s", name);
	}
	unsigned data_bits, stop_bits;
	char parity;
	settings_framing(&st->settings, &data_bits, &parity, &stop_bits);
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " VT100 %ux24 | %s | %u %u%c%u%s%s%s%s",
		screen.cols, TERM_BUILD_ID, settings_baud_rate(&st->settings), data_bits, parity, stop_bits,
		SERIAL_FLOW_CONTROL ? " RTS/CTS" : "", st->keyboards ? " | kbd" : "",
		st->settings.local_echo ? " | echo" : "", key);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colours.status_fg, colours.status_bg);
}

int main(void) {
	diag_init();
	// Read the settings while the flash is still in its boot-time setup.
	nvflash_init();
	const uint8_t *slots[SETTINGS_SLOTS];
	for (unsigned i = 0; i < SETTINGS_SLOTS; ++i)
		slots[i] = nvflash_sector(i);
	settings_load(slots, &settings, &save_target);
	colours = settings_colours(&settings); // the status line under the logo uses them

	video_init();
	// Before anything reads flash at the new clock: the settings saves and,
	// with LOGO_IN_FLASH, the logo.
	nvflash_retime();
	// After video_init: the baud divisor depends on the final clk_peri.
	// Diagnostic builds write to UART0 with blocking calls, which a CTS that
	// is unwired or held off would block forever: RTS only there.
	serial_init(uart0, settings_baud_rate(&settings), PICO_DEFAULT_UART_TX_PIN, PICO_DEFAULT_UART_RX_PIN,
		SERIAL_FLOW_CONTROL && !VIDEO_DIAG ? CTS_PIN : SERIAL_NO_PIN,
		SERIAL_FLOW_CONTROL ? RTS_PIN : SERIAL_NO_PIN);
	apply_line(&settings);
	diag_mark("serial_init done");
#if USB_KEYBOARD
	usbkbd_init(send_key, setup_key, NULL);
	diag_mark("usbkbd_init done");
#endif

	screen_init(&screen, TERM_COLS, FG, BG);
	video_start(&screen);
	diag_mark("video_start done (core 1 launched)");

	uint8_t buf[256];
	size_t pending = 0;
	bool input = false;
#if STARTUP_LOGO
	input = show_logo(buf, sizeof buf, &pending);
#endif
	if (STARTUP_DEMO && !input)
		pending = run_demo(buf, sizeof buf);

	screen_init(&screen, settings.columns, colours.fg, colours.bg);
	term_init(&term, &screen, 0, TERM_ROWS, colours.fg, colours.bg);
	apply_settings(&settings);
	term_set_reply(&term, send_reply, NULL);
	term_started = true;
	status_t shown = status_now();
	unsigned shown_resets = term.screen_resets;
	unsigned setup_requests = term.setup_requests;
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
		// While SET-UP is open from the keyboard, host data waits in the
		// receive buffer; opened by the host, what it sends are keys.
		size_t n = setup.active && !setup.from_host ? 0 : serial_read(buf, sizeof buf);
		if (setup.active) {
			setup_feed(buf, n);
		} else {
			size_t used = n ? term_write(&term, buf, n) : 0;
			if (term.setup_requests != setup_requests) {
				setup_requests = term.setup_requests;
				setup_enter(true);
				setup_feed(buf + used, n - used);
			}
		}
		keyboard_poll();
		if (setup.open_requested) {
			setup.open_requested = false;
			if (!setup.active)
				setup_enter(false);
		}
		if (setup.active)
			setup_service();
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
