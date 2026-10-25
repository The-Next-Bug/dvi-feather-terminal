// DVI serial terminal for the Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock.
//
// Core 1 renders the screen over DVI (video.c). Core 0 receives UART0 into
// a ring buffer (serial.c), feeds it to the VT100 emulator (term.c) that
// owns rows 0-23, queues its replies back on UART0 TX, and rasterizes changed
// lines; row 24 is a status line. With -DSERIAL_FLOW_CONTROL=ON, UART0 uses
// RTS/CTS on GP3/GP2. A USB keyboard on the native USB port (usbkbd.c) types to the host through
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
//
// With the Mode setting at "CP/M machine", the screen and keyboard are
// instead the console of a Z80 CP/M machine (machine.c, z80pack's CPU core)
// run on core 0 between the other work, with its disks on a microSD card
// (sdcard.c, drives.c) and UART0 as its second serial port. The monitor
// (monitor.c) boots it and manages the disks. Changing Mode restarts the
// board.
//
// The program's files: terminal.h.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "audio.h"
#include "bell.h"
#include "board.h"
#include "diag.h"
#include "extram.h"
#include "led.h"
#include "nvflash.h"
#include "palette.h"
#include "rtc.h"
#include "serial.h"
#include "splash.h"
#include "term_build.h"
#include "terminal.h"
#include "usbkbd.h"
#include "usbmsc.h"
#include "video.h"

#ifndef STARTUP_LOGO
#define STARTUP_LOGO 1
#endif

#ifndef STARTUP_DEMO
#define STARTUP_DEMO 0
#endif

#define TERM_COLS 80
#ifndef LOGO_MS
#define LOGO_MS 10000
#endif

static_assert(NVFLASH_SECTORS == SETTINGS_SLOTS && NVFLASH_PAGE_SIZE == SETTINGS_RECORD_SIZE,
	"a settings slot is a flash sector holding one page");

screen_t screen;
term_t term;
// Saved rows (screen.h): the main screen while the alternate screen is up
// (the terminal's rows at up to 132 columns), and the rows under SET-UP.
// At 80 columns both fit at once; at 132, SET-UP over the alternate screen
// has no room and leaves its rows blank.
#define STASH_SIZE (SCREEN_STASH_BYTES(TERM_ROWS, 132) > SCREEN_STASH_BYTES(TERM_ROWS + SETUP_ROWS, 80) \
	? SCREEN_STASH_BYTES(TERM_ROWS, 132) : SCREEN_STASH_BYTES(TERM_ROWS + SETUP_ROWS, 80))
static uint8_t stash_buf[STASH_SIZE];
screen_stash_t stash;
bool term_started;
bool key_pressed;

settings_t settings;
settings_target_t save_target;
settings_colors_t colors;
unsigned default_columns;
blank_t blank;
bool screen_blanked;
// The palette shown (built from the Colors and Palette settings).
static struct {
	uint16_t table[PALETTE_SIZE];
	bool built;
	uint8_t hue, mono;
} palette;

// With VIDEO_DIAG, report core 1's timing on UART0 TX twice a second.
static void diag_poll(void) {
	static absolute_time_t next;
	if (!VIDEO_DIAG || !time_reached(next)) {
		return;
	}
	next = make_timeout_time_ms(500);
	video_stats_t st = video_stats();
	char text[160];
	snprintf(text, sizeof text,
		"video: core1 stage %lu  frames %lu  max encode %lu of %lu cycles/line (%lu%%)  late %lu  clk %lu kHz\r\n",
		(unsigned long)st.core1_stage, (unsigned long)st.frames, (unsigned long)st.max_line_cycles, (unsigned long)st.cycles_budget,
		(unsigned long)(st.max_line_cycles * 100 / st.cycles_budget), (unsigned long)st.late,
		(unsigned long)(clock_get_hz(clk_sys) / 1000));
	uart_puts(BOARD_UART_INST, text);
	diag_heartbeat();
}

// Show the palette of s's Colors and Palette settings (from the next
// frame). From the main loop only: it may wait for a frame.
void show_palette(const settings_t *s) {
	if (palette.built && palette.hue == s->colors && palette.mono == s->palette) {
		return;
	}
	palette_build(palette.table, s->colors, s->palette);
	video_set_palette(palette.table);
	palette.built = true;
	palette.hue = s->colors;
	palette.mono = s->palette;
}

// The serial line settings of s.
static void apply_line(const settings_t *s) {
	unsigned data_bits, stop_bits;
	char parity;
	settings_framing(s, &data_bits, &parity, &stop_bits);
	serial_configure(settings_baud_rate(s), data_bits, parity, stop_bits);
}

// Use settings s: the line, the modes RIS resets to (also set now) and the
// palette (the status line follows at its next redraw). In CP/M mode LNM stays off: CP/M wants Return as CR alone (a
// following LF waits as a key, which stops DIR and TYPE early) and sends
// CR LF itself. The width is used at power-on, by SET-UP and by F11 (RIS
// keeps the current one); local echo is read from `settings` as keys are sent.
void apply_settings(const settings_t *s) {
	apply_line(s);
	term_set_defaults(&term, &(term_defaults_t){.backspace_bs = s->backspace == 0x08,
		.newline_mode = s->newline && !cpm.on, .vt52 = s->term_type == SETTINGS_TERM_VT52,
		.personality = s->term_type == SETTINGS_TERM_ADM3A ? TERM_ADM3A : TERM_VT100});
	default_columns = settings_columns(s);
	blank_init(&blank, s->blank_after * 60000u, to_ms_since_boot(get_absolute_time()));
	colors = settings_colors(s);
	show_palette(s);
	bell_set_level(s->bell);
}

// Wait until `until`, UART0 receiving something (copied into buf) or a key
// press. Returns whether either happened; *n is what arrived.
static bool wait_for_input(absolute_time_t until, uint8_t *buf, size_t max, size_t *n) {
	while (!time_reached(until)) {
		*n = serial_read(buf, max);
		if (*n || key_pressed) {
			return true;
		}
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
	if (status_shown()) {
		screen_clear_line(&screen, STATUS_ROW, colors.status_fg, colors.status_bg);
		screen_puts(&screen, 0, STATUS_ROW, status, colors.status_fg, colors.status_bg);
	}
	video_update(&screen);
	*n = 0;
	// In the hue of the Colors setting (white keeps the logo's own colors,
	// or with the mono palette shows it gray).
	splash_tint_t tint = splash_tint(&splash_logo, colors.logo_fg, settings.palette);
	if (!video_splash(&splash_logo, &tint)) {
		return false;
	}
	bool input = wait_for_input(make_timeout_time_ms(LOGO_MS), buf, max, n);
	video_splash_end();
	return input;
}
#endif

// Cycle the demo until UART0 receives something or a key is pressed; return
// what arrived.
static size_t run_demo(uint8_t *buf, size_t max) {
	for (unsigned cols = 80;; cols = cols == 80 ? 132 : 80) {
		draw_demo(cols, "data or a key: start");
		video_update(&screen);

		size_t n;
		if (wait_for_input(make_timeout_time_ms(DEMO_INTERVAL_MS), buf, max, &n)) {
			return n;
		}
	}
}

// Replies to the host (device attributes, cursor position reports).
static void send_reply(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	if (cpm.on) {
		for (size_t i = 0; i < len; ++i) {
			machine_key(data[i]);
		}
	} else {
		serial_write(data, len);
	}
}

int main(void) {
#if defined(BOARD_PSRAM_CS_PIN) && !PSRAM_BANKS
	// The PSRAM shares the QSPI bus with the flash. Its CE pull-up against
	// the pad's reset pull-down leaves CE barely above its input-high level,
	// so drive it high: nothing uses the PSRAM in this build (PSRAM_BANKS
	// builds leave the pin to the SDK's PSRAM setup, XIP_CS1).
	gpio_init(BOARD_PSRAM_CS_PIN);
	gpio_put(BOARD_PSRAM_CS_PIN, 1);
	gpio_set_dir(BOARD_PSRAM_CS_PIN, GPIO_OUT);
#endif
#ifdef BOARD_USB_VBUS_EN_PIN
	// The board switches the USB port's 5 V (the RP2350 cannot supply it),
	// on the PicoTop also the HDMI connector's +5V, which some monitors
	// want before they look at the signal: so before video_init (bd gy1).
	gpio_init(BOARD_USB_VBUS_EN_PIN);
	gpio_put(BOARD_USB_VBUS_EN_PIN, BOARD_USB_VBUS_EN_LEVEL);
	gpio_set_dir(BOARD_USB_VBUS_EN_PIN, GPIO_OUT);
#endif
	diag_init();
	// Read the settings while the flash is still in its boot-time setup.
	nvflash_init();
	const uint8_t *slots[SETTINGS_SLOTS];
	for (unsigned i = 0; i < SETTINGS_SLOTS; ++i) {
		slots[i] = nvflash_sector(i);
	}
	settings_load(slots, &settings, &save_target);
	colors = settings_colors(&settings); // the status line under the logo uses them

	video_init();
	// The PSRAM's timing for the new clock; it leaves the flash's XIP set
	// up for the boot clock, so first.
	size_t psram = extram_retime();
	// Before anything reads flash at the new clock: the settings saves,
	// with LOGO_IN_FLASH the logo, and the cold code left in flash
	// (ld/section_copy_to_ram_rodata.incl), palette.c's included.
	nvflash_retime();
	// The wall clock from the board's RTC (I2C clocked from clk_peri, final
	// now).
	rtc_init();
	bell_init(); // its sample rate from clk_sys, final now
	show_palette(&settings);
	// After video_init: the baud divisor depends on the final clk_peri.
	// Diagnostic builds write to UART0 with blocking calls, which a CTS that
	// is unwired or held off would block forever: RTS only there.
	serial_init(BOARD_UART_INST, settings_baud_rate(&settings), BOARD_UART_TX_PIN, BOARD_UART_RX_PIN,
#if SERIAL_FLOW_CONTROL
		VIDEO_DIAG ? SERIAL_NO_PIN : BOARD_UART_CTS_PIN, BOARD_UART_RTS_PIN);
#else
		SERIAL_NO_PIN, SERIAL_NO_PIN);
#endif
	apply_line(&settings);
	diag_mark("serial_init done");
	led_init();
	// Before the USB host logs anything: it may keep a crash's log text.
	const char *previous_crash = crash_init();
	usbkbd_init(send_key, setup_key, NULL);
	diag_mark("usbkbd_init done");

	screen_init(&screen, TERM_COLS, PALETTE_TEXT, PALETTE_BACKGROUND);
	video_start(&screen);
	diag_mark("video_start done (core 1 launched)");

	uint8_t buf[256];
	size_t pending = 0;
	bool input = false;
#if STARTUP_LOGO
	input = show_logo(buf, sizeof buf, &pending);
#endif
	if (STARTUP_DEMO && !input) {
		pending = run_demo(buf, sizeof buf);
	}

	cpm.on = settings.mode == SETTINGS_MODE_CPM;
	screen_init(&screen, settings_columns(&settings), PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&term, &screen, 0, TERM_ROWS);
	screen_stash_init(&stash, stash_buf, sizeof stash_buf);
	term_set_stash(&term, &stash);
	apply_settings(&settings);
	term_set_reply(&term, send_reply, NULL);
	term_started = true;
	status_t shown = status_now();
	unsigned shown_resets = term.screen_resets;
	unsigned setup_requests = term.setup_requests;
	unsigned bells = term.bells;
	if (cpm.on) {
		draw_cpm_status();
	} else {
		draw_status(&shown);
	}

	char banner[SCREEN_MAX_COLS + 1];
	snprintf(banner, sizeof banner, "DVI Feather Terminal build %s (%s, %s)\r\n",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	term_puts(&term, banner);
	if (previous_crash) {
		snprintf(banner, sizeof banner, "The last run ended with: %s\r\n", previous_crash);
		term_puts(&term, banner);
	}
	if (cpm.on) {
		// UART input before now was for the terminal, not the Z80.
		term_puts(&term, "Z80 CP/M machine: " BOARD_UART_NAME " (GP" BOARD_STR(BOARD_UART_TX_PIN) "/GP" BOARD_STR(
			BOARD_UART_RX_PIN) ") is its second serial port\r\n");
#if PSRAM_BANKS
		// The banks in the PSRAM: as many as fit, at most 15.
		unsigned banks = (unsigned)(psram / MACHINE_BANK_SIZE);
		machine_set_banks(extram_base(), banks);
		if (psram) {
			snprintf(banner, sizeof banner, "Z80 banks: %u x 48K in PSRAM (%u KB found)\r\n", machine_banks(),
				(unsigned)(psram / 1024));
			term_puts(&term, banner);
		} else {
			term_puts(&term, "No PSRAM found: the Z80 has no banks (CP/M 3 needs one)\r\n");
		}
#else
		(void)psram;
#endif
		machine_init(&cpm_platform);
		audio_attach_ay(true);
		usbkbd_set_reset_key(reset_key);
		monitor_start(&cpm_monitor, true, to_ms_since_boot(get_absolute_time()));
	} else {
#if SERIAL_FLOW_CONTROL
		term_puts(&term, BOARD_UART_PINS_TEXT BOARD_UART_FLOW_TEXT "\r\n\r\n");
#else
		term_puts(&term, BOARD_UART_PINS_TEXT "\r\n\r\n");
#endif
		if (pending) {
			term_write(&term, buf, pending);
		}
	}
	video_update(&screen);

	absolute_time_t next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
	debug_status_start();
	while (true) {
		if (cpm.on) {
			cpm_step();
			// A drive light changed: show it now, not at the next status update.
			if (drive_lights(to_ms_since_boot(get_absolute_time())) != cpm.lit) {
				draw_cpm_status();
			}
			// The Z80's output asked for SET-UP (CSI ? 999 ~).
			if (term.setup_requests != setup_requests) {
				setup_requests = term.setup_requests;
				if (!setup.active) {
					setup_enter(false);
				}
			}
		} else {
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
		}
		keyboard_poll();
		usbmsc_poll(); // a USB stick plugged in or pulled: mount or unmount it
		usblog_poll();
		if (setup.open_requested) {
			setup.open_requested = false;
			if (!setup.active) {
				setup_enter(false);
			}
		}
		if (setup.active) {
			setup_service();
		}
		serial_poll();
		led_poll(screen_blanked);

		uint32_t now = to_ms_since_boot(get_absolute_time());
		if (setup.active) {
			blank_activity(&blank, now); // not idle while SET-UP is open
		}
		screen_blanked = blank_tick(&blank, now);
		video_set_blank(screen_blanked);

		// DECCOLM re-initializes the whole screen, status line included,
		// whether or not the width changes.
		if (term.screen_resets != shown_resets) {
			shown_resets = term.screen_resets;
			if (cpm.on) {
				draw_cpm_status();
			} else {
				draw_status(&shown);
			}
		}
		if (crash_redraw_status()) { // a hang report was over it
			if (cpm.on) {
				draw_cpm_status();
			} else {
				draw_status(&shown);
			}
		}
		if (time_reached(next_status)) {
			next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
			if (debug_status_on()) {
				debug_status_refresh();
			}
			if (cpm.on) {
				draw_cpm_status();
			} else {
				status_t st = status_now();
				if (debug_status_on() || !status_equal(&st, &shown)) {
					shown = st;
					draw_status(&shown);
				}
			}
		}
		// BEL from the host or the CP/M console.
		if (term.bells != bells) {
			bells = term.bells;
			bell_ring();
		}
		status_clock_poll();
		video_update(&screen);
		diag_poll();
		debug_status_pass();
	}
}
