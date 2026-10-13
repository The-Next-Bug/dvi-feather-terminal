// DVI serial terminal for the Raspberry Pi Pico 2 (RP2350) with the Pico DVI Sock.
//
// Core 1 renders the screen over DVI (video.c). Core 0 receives UART0 into
// a ring buffer (serial.c), feeds it to the VT100 emulator (term.c) that
// owns rows 0-23, queues its replies back on UART0 TX, and rasterises changed
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#include "blank.h"
#include "board.h"
#include "demo.h"
#include "diag.h"
#include "drivepanel.h"
#include "drives.h"
#include "ff.h"
#include "fwpanel.h"
#include "fwupdate.h"
#include "keyname.h"
#include "led.h"
#include "machine.h"
#include "monitor.h"
#include "nvflash.h"
#include "palette.h"
#include "raster.h"
#include "sdcard.h"
#include "screen.h"
#include "settings.h"
#include "setup.h"
#include "term.h"
#include "term_build.h"
#include "serial.h"
#include "video.h"
#include "xfer.h"

#ifndef STARTUP_LOGO
#define STARTUP_LOGO 1
#endif

#ifndef STARTUP_DEMO
#define STARTUP_DEMO 0
#endif

#include "usbkbd.h"
#include "usbmsc.h"

#ifndef SERIAL_FLOW_CONTROL
#define SERIAL_FLOW_CONTROL 0
#endif

#define TERM_COLS 80
#define DEMO_INTERVAL_MS 5000
#ifndef LOGO_MS
#define LOGO_MS 10000
#endif


#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define STATUS_INTERVAL_MS 250
#define CPU_SLICE_US 10000 // the Z80 runs this long between the other work
#define CPM_KEY_QUEUE 64   // a power of two
#define DRIVE_LIGHT_MS 150 // a drive letter stays lit this long after an access
#define SETUP_TOP (TERM_ROWS - SETUP_ROWS)

static_assert(NVFLASH_SECTORS == SETTINGS_SLOTS && NVFLASH_PAGE_SIZE == SETTINGS_RECORD_SIZE,
	"a settings slot is a flash sector holding one page");

static screen_t screen;
static term_t term;
// Saved rows (screen.h): the main screen while the alternate screen is up
// (the terminal's rows at up to 132 columns), and the rows under SET-UP.
// At 80 columns both fit at once; at 132, SET-UP over the alternate screen
// has no room and leaves its rows blank.
#define STASH_SIZE (SCREEN_STASH_BYTES(TERM_ROWS, 132) > SCREEN_STASH_BYTES(TERM_ROWS + SETUP_ROWS, 80) \
	? SCREEN_STASH_BYTES(TERM_ROWS, 132) : SCREEN_STASH_BYTES(TERM_ROWS + SETUP_ROWS, 80))
#define STASH_SETUP 'S'
static uint8_t stash_buf[STASH_SIZE];
static screen_stash_t stash;
static bool term_started;
static bool key_pressed; // before the terminal started: ends the logo and demo

static settings_t settings;          // as saved in flash
static settings_target_t save_target;
static settings_colours_t colours;   // in use: the status line's
static unsigned default_columns;     // the SET-UP width in use: what F11 goes back to
static blank_t blank;                // the Blank after setting: keys keep the screen lit
static bool screen_blanked;          // blank_tick's answer: the LED's heartbeat runs then
// The palette shown (built from the Colors and Palette settings).
static struct {
	uint16_t table[PALETTE_SIZE];
	bool built;
	uint8_t hue, mono;
} palette;

// The last key sequence sent to the host, for the status line.
static struct {
	uint8_t data[8];
	uint8_t len;
	uint32_t count; // keys sent: tells a repeat of the same key apart
} last_key;

// SET-UP state; the panel's rows are saved (stash) while it covers them.
static struct {
	bool active, from_host;
	bool open_requested;  // by the SET-UP key
	setup_action_t action; // a save or cancel waiting for the main loop
	setup_t ui;
	uint32_t saved_reverse;
	bool saved_cursor;
	// CP/M mode: the Drives panel (D), over the same rows; its card work
	// (mount, save or remove a set) waits for the main loop.
	bool drives_open;
	drivepanel_action_t drives_work;
	drivepanel_t drives;
	// The firmware update panel (U), over the same rows; its work and its
	// cancel wait for the main loop.
	bool fw_open, fw_work, fw_cancel;
	fwpanel_t fw;
	// The demo screen (T), over the whole screen until a key.
	bool demo, demo_end;
	unsigned demo_cols, term_cols;
	absolute_time_t demo_switch;
} setup;

static_assert(DRIVEPANEL_ROWS <= SETUP_ROWS, "the Drives panel fits SET-UP's rows");
static_assert(FWPANEL_ROWS <= SETUP_ROWS, "the firmware panel fits SET-UP's rows");

// The Z80 machine (CP/M mode). Keys are queued here and handed to the CPU
// or the monitor from the main loop, never from inside the USB stack.
static struct {
	bool on;      // Mode was CP/M at power-on
	bool running; // the CPU runs; otherwise the monitor has the console
	bool reset_requested; // F11, from inside the USB stack
	uint8_t keys[CPM_KEY_QUEUE];
	unsigned key_head, key_tail;
	uint32_t khz; // last measured emulated clock
	uint32_t light_until[DRIVES]; // ms: the drive's letter is lit until then
	uint32_t usb_light_until; // ms: USB is lit until then
	uint16_t lit; // drive letters (bits 0-7) and USB (bit 8) shown lit on the status line
} cpm;

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
	uart_puts(BOARD_UART_INST, text);
	diag_heartbeat();
}

// Show the palette of s's Colors and Palette settings (from the next
// frame). From the main loop only: it may wait for a frame.
static void show_palette(const settings_t *s) {
	if (palette.built && palette.hue == s->colours && palette.mono == s->palette)
		return;
	palette_build(palette.table, s->colours, s->palette);
	video_set_palette(palette.table);
	palette.built = true;
	palette.hue = s->colours;
	palette.mono = s->palette;
}

// The panels are boxes on the default background, the box in the status
// line's colour of the edited settings, like the preview (setup_service
// shows their palette).
static void setup_draw_panel(void) {
	char title[SCREEN_MAX_COLS + 1];
	snprintf(title, sizeof title, "DVI Terminal build %s", TERM_BUILD_ID);
	settings_colours_t c = settings_colours(&setup.ui.values);
	setup_draw(&setup.ui, &screen, SETUP_TOP, title, PALETTE_TEXT, PALETTE_BACKGROUND, c.status_bg);
}

// At the bottom of SET-UP's rows, the rows above it blank.
static void drives_draw_panel(void) {
	settings_colours_t c = settings_colours(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - DRIVEPANEL_ROWS; ++r)
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	drivepanel_draw(&setup.drives, &screen, SETUP_TOP + SETUP_ROWS - DRIVEPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
}

// The firmware panel, at the bottom of SET-UP's rows like the Drives panel.
static void fw_draw_panel(void) {
	settings_colours_t c = settings_colours(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - FWPANEL_ROWS; ++r)
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	fwpanel_draw(&setup.fw, &screen, SETUP_TOP + SETUP_ROWS - FWPANEL_ROWS, PALETTE_TEXT, PALETTE_BACKGROUND,
		c.status_bg);
}

// The firmware panel's platform. In CP/M mode the monitor keeps the card
// mounted (with the drives' images open); in terminal mode the card is
// mounted only while the panel is open, with the monitor's FATFS (the
// monitor never runs in terminal mode). The stick (volume 1) is usbmsc.c's,
// mounted in both modes.

static usbmsc_status_t usb_status(void);

static bool fw_stick(void) {
	return usb_status().mounted;
}

static bool fw_mount(char *info, size_t n) {
	if (cpm.on)
		return true;
	FRESULT r = f_mount(monitor_card_fs(), "", 1);
	if (r == FR_OK)
		return true;
	if (sdcard_type() == SDCARD_NONE)
		snprintf(info, n, "none found (" BOARD_SD_PINS_TEXT ")");
	else
		snprintf(info, n, "no FAT file system on it (error %d)", (int)r);
	return false;
}

static void fw_unmount(bool commit) {
	if (cpm.on && !commit)
		return; // the monitor's
	if (cpm.on) {
		xfer_reset(); // also closes what XLS/XPIP had open on the stick
		drives_flush();
		drives_unmount_all();
	}
	// The stick stays mounted: the rename left its file system in sync.
	f_unmount("");
}

// Show the panel now: the long steps hold up the main loop.
static void fw_show(const fwpanel_t *p) {
	(void)p;
	fw_draw_panel();
	video_update(&screen);
}

// The commit leaves the screen as fw_show last drew it: core 1 keeps
// showing it from RAM while the flash is rewritten.
static const fwpanel_platform_t fw_platform = {
	.mount = fw_mount,
	.stick = fw_stick,
	.check = fwupdate_check,
	.stage = fwupdate_stage,
	.unmount = fw_unmount,
	.commit = fwupdate_commit,
	.show = fw_show,
};

static void fw_close(void) {
	if (setup.fw_open)
		fwpanel_close(&setup.fw, &fw_platform);
	setup.fw_open = setup.fw_work = setup.fw_cancel = false;
}

// A key for the firmware panel. The work is left for the main loop.
static void fw_feed(uint8_t c) {
	fwpanel_action_t a = fwpanel_input(&setup.fw, c);
	if (a == FWPANEL_WORK)
		setup.fw_work = true;
	else if (a == FWPANEL_CANCEL)
		setup.fw_cancel = true;
}

// A key for the Drives panel. Mounting and the sets' files are left for
// the main loop.
static void drives_feed(uint8_t c, uint32_t now) {
	drivepanel_action_t a = drivepanel_input(&setup.drives, c, now);
	if (a == DRIVEPANEL_REDRAW) {
		drives_draw_panel();
	} else if (a == DRIVEPANEL_APPLY || a == DRIVEPANEL_SAVE_SET || a == DRIVEPANEL_REMOVE_SET) {
		setup.drives_work = a;
	} else if (a == DRIVEPANEL_CANCEL) {
		setup.drives_open = false;
		setup_draw_panel();
	}
}

// Key bytes for SET-UP. A save, cancel or anything touching the SD card is
// left for the main loop (the keyboard calls this from inside the USB
// stack); later bytes are dropped.
static void setup_feed(const uint8_t *data, size_t len) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (setup.demo) {
		setup.demo_end |= len > 0; // any key; the rest of it is dropped
		return;
	}
	for (size_t i = 0; i < len && setup.action == SETUP_NONE && !setup.drives_work && !setup.fw_work &&
		!setup.fw_cancel; ++i) {
		if (setup.fw_open) {
			fw_feed(data[i]);
			continue;
		}
		if (setup.drives_open) {
			drives_feed(data[i], now);
			continue;
		}
		setup_action_t a = setup_input(&setup.ui, data[i], now);
		if (a == SETUP_REDRAW)
			setup_draw_panel();
		else if (a != SETUP_NONE)
			setup.action = a;
	}
}

// Save the rows under the panel and draw it there.
static void setup_cover(void) {
	screen_stash_push(&stash, &screen, STASH_SETUP, SETUP_TOP, SETUP_ROWS);
	setup.saved_reverse = screen.reverse_rows;
	screen_set_reverse_rows(&screen, screen.reverse_rows & ~(((1u << SETUP_ROWS) - 1) << SETUP_TOP));
	screen.cursor_visible = false;
	setup_draw_panel();
}

static void setup_enter(bool from_host) {
	// The modes now, whatever set them.
	settings_t current = settings;
	current.columns = screen.cols;
	current.backspace = term.backspace_bs ? 0x08 : 0x7f;
	current.term_type = term.personality == TERM_ADM3A ? SETTINGS_TERM_ADM3A
		: term.vt100.vt52 ? SETTINGS_TERM_VT52 : SETTINGS_TERM_VT100;
	// CP/M mode keeps LNM off (apply_settings): the saved choice stands.
	current.newline = cpm.on ? settings.newline : term.newline_mode;
	setup_open(&setup.ui, &current);
	setup.ui.machine_menu = cpm.on;
	setup.active = true;
	setup.drives_open = false;
	setup.drives_work = DRIVEPANEL_NONE;
	setup.fw_open = setup.fw_work = setup.fw_cancel = false;
	setup.from_host = from_host;
	setup.action = SETUP_NONE;
	setup.demo = false;
	setup.saved_cursor = screen.cursor_visible;
	setup_cover();
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
static void apply_settings(const settings_t *s) {
	apply_line(s);
	term_set_defaults(&term, &(term_defaults_t){.backspace_bs = s->backspace == 0x08,
		.newline_mode = s->newline && !cpm.on, .vt52 = s->term_type == SETTINGS_TERM_VT52,
		.personality = s->term_type == SETTINGS_TERM_ADM3A ? TERM_ADM3A : TERM_VT100});
	default_columns = settings_columns(s);
	blank_init(&blank, s->blank_after * 60000u, to_ms_since_boot(get_absolute_time()));
	colours = settings_colours(s);
	show_palette(s);
}

// Put the rows back; with apply, use the edited settings now, and the width
// if it differs (which clears the screen, as DECCOLM does).
static void setup_leave(bool apply) {
	if (!screen_stash_pop(&stash, &screen, STASH_SETUP, PALETTE_TEXT, PALETTE_BACKGROUND))
		for (unsigned r = 0; r < SETUP_ROWS; ++r) // not saved: no room
			screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	screen_set_reverse_rows(&screen, setup.saved_reverse);
	screen.cursor_visible = setup.saved_cursor;
	setup.active = false;
	setup.drives_open = false;
	setup.drives_work = DRIVEPANEL_NONE;
	fw_close();
	if (!apply) {
		show_palette(&settings); // back from SET-UP's preview
		return;
	}
	apply_settings(&setup.ui.values);
	if (settings_columns(&setup.ui.values) != screen.cols)
		term_set_columns(&term, settings_columns(&setup.ui.values));
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

// The Drives panel's pending card work, or its Escape key timeout.
static void drives_service(void) {
	drivepanel_action_t work = setup.drives_work;
	setup.drives_work = DRIVEPANEL_NONE;
	if (work == DRIVEPANEL_SAVE_SET || work == DRIVEPANEL_REMOVE_SET) {
		if (work == DRIVEPANEL_SAVE_SET)
			drivepanel_save_set(&setup.drives);
		else
			drivepanel_remove_set(&setup.drives);
		drives_draw_panel();
	} else if (work == DRIVEPANEL_APPLY) {
		bool failed;
		unsigned changed = drivepanel_apply(&setup.drives, &failed);
		drivepanel_open(&setup.drives); // as now mounted
		setup.drives.message = failed ? "A disk could not be mounted (not a disk image?)"
			: changed ? "Done (under CP/M 2.2, ^C at the prompt to use the changed drives)"
			: "No changes";
		drives_draw_panel();
	} else if (drivepanel_tick(&setup.drives, to_ms_since_boot(get_absolute_time())) == DRIVEPANEL_CANCEL) {
		setup.drives_open = false;
		setup_draw_panel();
	}
}

// The demo screen at cols, with `what` on its status line.
static void draw_demo(unsigned cols, const char *what) {
	char status[SCREEN_MAX_COLS + 1];
	snprintf(status, sizeof status, " demo | %u x 24 | build %s (%s, %s) | %s",
		cols, TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE, what);
	demo_draw(&screen, cols, raster_font_name(raster_layout(cols)), status);
}

// SET-UP's demo (T): in the edited palette, alternating widths until a key.
// The terminal's screen is not kept (a copy would cost 13 KB of SRAM): it
// comes back cleared, at its width, as after DECCOLM, under SET-UP again.
static void setup_demo_start(void) {
	screen_stash_drop(&stash, STASH_SETUP); // saved again over the cleared screen
	setup.demo = true;
	setup.demo_end = false;
	setup.term_cols = screen.cols;
	setup.demo_cols = screen.cols;
	setup.demo_switch = make_timeout_time_ms(DEMO_INTERVAL_MS);
	draw_demo(setup.demo_cols, "any key: back to SET-UP");
}

static void setup_demo_service(void) {
	if (setup.demo_end) {
		setup.demo = false;
		term_set_columns(&term, setup.term_cols); // clears; the status line follows
		setup_cover();
	} else if (time_reached(setup.demo_switch)) {
		setup.demo_switch = make_timeout_time_ms(DEMO_INTERVAL_MS);
		setup.demo_cols = setup.demo_cols == 80 ? 132 : 80;
		draw_demo(setup.demo_cols, "any key: back to SET-UP");
	}
}

// Restart the board from power-on (boot logo, settings read again): say so,
// write back the drives' buffers, reboot.
static void restart(void) {
	setup.ui.message = "Restarting...";
	setup_draw_panel();
	video_update(&screen);
	if (cpm.on)
		drives_flush();
	watchdog_reboot(0, 0, 0);
	while (true)
		tight_loop_contents();
}

// Run SET-UP's pending save or cancel, the demo, the reboot, or the Escape
// key timeout, and show the edited Colors and Palette.
static void setup_service(void) {
	show_palette(&setup.ui.values);
	if (setup.demo) {
		setup_demo_service();
		return;
	}
	if (setup.drives_open && setup.action == SETUP_NONE) {
		drives_service();
		return;
	}
	if (setup.fw_open && setup.action == SETUP_NONE) {
		if (setup.fw_cancel) {
			fw_close();
			setup_draw_panel();
		} else if (setup.fw_work) {
			setup.fw_work = false;
			fwpanel_run(&setup.fw, &fw_platform); // staging ends in the restart
			fw_draw_panel();
		}
		return;
	}
	if (setup.action == SETUP_NONE)
		setup.action = setup_tick(&setup.ui, to_ms_since_boot(get_absolute_time()));
	setup_action_t a = setup.action;
	setup.action = SETUP_NONE;
	if (a == SETUP_CANCEL) {
		setup_leave(false);
	} else if (a == SETUP_DEMO) {
		setup_demo_start();
	} else if (a == SETUP_REBOOT) {
		restart(); // the last resort when the terminal is stuck
	} else if (a == SETUP_FIRMWARE) {
		fwpanel_open(&setup.fw);
		setup.fw_open = setup.fw_work = true; // the check
		fw_draw_panel();
	} else if (a == SETUP_DRIVES) {
		drivepanel_open(&setup.drives);
		setup.drives_open = true;
		drives_draw_panel();
	} else if (a == SETUP_MACHINE) {
		setup_leave(false);
		cpm.running = false;
		monitor_enter(NULL, true);
	} else if (a == SETUP_SAVE) {
		// The erase holds core 0 for tens of ms: say so first.
		setup.ui.message = "Saving...";
		setup_draw_panel();
		video_update(&screen);
		if (setup_save()) {
			if (settings.mode != (cpm.on ? SETTINGS_MODE_CPM : SETTINGS_MODE_TERMINAL))
				restart(); // the other mode starts from a clean power-on
			setup_leave(true);
		} else {
			setup.ui.message = "Saving to flash failed";
			setup_draw_panel();
		}
	}
}

// With the Status line setting hidden, row 24 is left blank (the text area
// stays 24 rows) and the status line drawers stop here.
static bool status_shown(void) {
	if (settings.status_line)
		return true;
	screen_clear_line(&screen, STATUS_ROW, PALETTE_TEXT, PALETTE_BACKGROUND);
	return false;
}

// A key while the screen is blanked only wakes it (it is dropped, not
// typed blind); any other key restarts the blanking delay.
static bool key_wakes(void) {
	return term_started && blank_activity(&blank, to_ms_since_boot(get_absolute_time()));
}

// Keystrokes go to the host like replies, through the TX queue (or to
// SET-UP while it is open). With local echo they are shown too.
static void send_key(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	key_pressed = true;
	if (key_wakes())
		return;
	if (setup.active) {
		setup_feed(data, len);
		return;
	}
	if (cpm.on) {
		for (size_t i = 0; i < len && cpm.key_head - cpm.key_tail < CPM_KEY_QUEUE; ++i)
			cpm.keys[cpm.key_head++ % CPM_KEY_QUEUE] = data[i];
	} else {
		serial_write(data, len);
	}
	last_key.len = (uint8_t)(len < sizeof last_key.data ? len : sizeof last_key.data);
	memcpy(last_key.data, data, last_key.len);
	++last_key.count;
	if (term_started && settings.local_echo && !cpm.on)
		term_write(&term, data, len);
}

// The SET-UP key opens SET-UP, or leaves it without saving.
static void setup_key(void *ctx) {
	(void)ctx;
	key_pressed = true;
	if (!term_started || key_wakes())
		return;
	if (setup.demo)
		setup.demo_end = true; // back to SET-UP, like any key
	else if (setup.active)
		setup.action = SETUP_CANCEL;
	else
		setup.open_requested = true;
}

// F11 in CP/M mode: the Z80 machine's reset button (cpm_step acts on it).
static void reset_key(void *ctx) {
	(void)ctx;
	key_pressed = true;
	if (term_started && !key_wakes() && !setup.active)
		cpm.reset_requested = true;
}

// The terminal modes that change what keys send (none before it starts).
// SET-UP and the CP/M monitor read keys as a VT100 in ANSI mode sends them
// (ESC [ A ...), whatever the host has set: only the host's program gets
// its own modes.
static void keyboard_poll(void) {
	kbd_modes_t modes = {.backspace_bs = settings.backspace == 0x08};
	if (term_started) {
		modes.newline_mode = term.newline_mode;
		modes.backspace_bs = term.backspace_bs;
		if (setup.active || (cpm.on && !cpm.running)) {
			// VT100 keys for SET-UP or the monitor
		} else if (term.personality == TERM_ADM3A) {
			modes.adm3a = true;
		} else {
			modes.cursor_keys_app = term.vt100.cursor_keys_app;
			modes.keypad_app = term.vt100.keypad_app;
			modes.vt52 = term.vt100.vt52;
		}
	}
	usbkbd_poll(&modes);
}

static unsigned keyboards_attached(void) {
	return usbkbd_count();
}

static usbmsc_status_t usb_status(void) {
	return usbmsc_status();
}

static void usb_log_text(char *out, size_t size) {
	usbkbd_log_text(out, size);
}

static bool usb_activity(void) {
	return usbmsc_activity();
}

// The USB stick on the status line: "USB" while one is volume 1 (USB: to
// XLS and XPIP), "+n" for others waiting to take over, "?" if one could
// not be used; empty if none is in.
static void usb_label(char *out, size_t size) {
	usbmsc_status_t u = usb_status();
	out[0] = '\0';
	if (!u.mounted && !u.waiting && !u.unusable)
		return;
	int n = snprintf(out, size, "%s", u.mounted ? "USB" : "usb");
	if (u.waiting && n > 0 && (size_t)n < size)
		n += snprintf(out + n, size - (size_t)n, "+%u", u.waiting);
	if (u.unusable && n > 0 && (size_t)n < size)
		snprintf(out + n, size - (size_t)n, "?");
}

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
	if (status_shown()) {
		screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
		screen_puts(&screen, 0, STATUS_ROW, status, colours.status_fg, colours.status_bg);
	}
	video_update(&screen);
	*n = 0;
	// In the hue of the Colors setting (white keeps the logo's own colours,
	// or with the mono palette shows it grey).
	splash_tint_t tint = splash_tint(&splash_logo, colours.logo_fg, settings.palette);
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
	for (unsigned cols = 80;; cols = cols == 80 ? 132 : 80) {
		draw_demo(cols, "data or a key: start");
		video_update(&screen);

		size_t n;
		if (wait_for_input(make_timeout_time_ms(DEMO_INTERVAL_MS), buf, max, &n))
			return n;
	}
}

// Replies to the host (device attributes, cursor position reports).
static void send_reply(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	if (cpm.on) {
		for (size_t i = 0; i < len; ++i)
			machine_key(data[i]);
	} else {
		serial_write(data, len);
	}
}

// ---- CP/M mode: the Z80 machine's platform and the monitor's ----

static uint64_t cpm_clock_us(void) {
	return time_us_64();
}

static void cpm_console_out(uint8_t c) {
	term_write(&term, &c, 1);
}

static void cpm_aux_out(uint8_t c) {
	serial_write(&c, 1);
}

static bool cpm_aux_in(uint8_t *c) {
	return serial_read(c, 1) == 1;
}

static uint8_t cpm_disk_read(unsigned drive, unsigned track, unsigned sector, uint8_t *buf) {
	return drives_read(drive, track, sector, buf);
}

static uint8_t cpm_disk_write(unsigned drive, unsigned track, unsigned sector, const uint8_t *buf) {
	return drives_write(drive, track, sector, buf);
}

// Queued keys to the CPU while it runs (as many as it has room for), else
// to the monitor.
static void cpm_feed_keys(uint32_t now_ms) {
	while (cpm.key_tail != cpm.key_head) {
		uint8_t c = cpm.keys[cpm.key_tail % CPM_KEY_QUEUE];
		if (cpm.running) {
			if (!machine_key(c))
				return;
		} else if (monitor_key(c, now_ms) == MONITOR_RUN) {
			cpm.running = true;
		}
		++cpm.key_tail;
	}
}

// The CPU waits (HALT, the speed limit): keep the keyboard, UART0 and the
// screen going.
static void cpm_idle(void) {
	keyboard_poll();
	serial_poll();
	led_poll(screen_blanked);
	if (cpm.running)
		cpm_feed_keys(to_ms_since_boot(get_absolute_time()));
	video_update(&screen);
}

static uint8_t cpm_disk_format(unsigned drive) {
	return (uint8_t)drives_format(drive);
}

static uint8_t cpm_disk_mounts(unsigned drive) {
	return drives_mounts(drive);
}

static const machine_platform_t cpm_platform = {
	.clock_us = cpm_clock_us,
	.idle = cpm_idle,
	.console_out = cpm_console_out,
	.aux_out = cpm_aux_out,
	.aux_in = cpm_aux_in,
	.disk_read = cpm_disk_read,
	.disk_write = cpm_disk_write,
	.disk_format = cpm_disk_format,
	.disk_mounts = cpm_disk_mounts,
	.xfer = xfer_command,
};

static void cpm_write(const char *text) {
	term_puts(&term, text);
}

static const char *cpm_card_info(void) {
	static char text[32];
	sdcard_type_t type = sdcard_type();
	if (type == SDCARD_NONE)
		return NULL;
	uint32_t mb = sdcard_blocks() / 2048;
	snprintf(text, sizeof text, "%s, %lu.%lu GB", type == SDCARD_SDHC ? "SDHC/SDXC" : "SDSC",
		(unsigned long)(mb / 1024), (unsigned long)(mb % 1024 * 10 / 1024));
	return text;
}

static void cpm_flush(void) {
	video_update(&screen);
}

static const monitor_platform_t cpm_monitor = {
	.write = cpm_write,
	.card_info = cpm_card_info,
	.card_pins = BOARD_SD_PINS_TEXT,
	.flush = cpm_flush,
	.usb_info = usbmsc_info,
};

// F11: reset the machine and boot from drive A, as the reset button of a
// real one would; with no disk in A, the monitor instead. The terminal is
// reset too (colours, modes, margins, a stuck escape sequence) and goes back
// to the SET-UP width, so a program that left it unreadable cannot keep it so.
static void cpm_reset(void) {
	cpm.reset_requested = false;
	cpm.key_tail = cpm.key_head; // typed-ahead keys were for the old run
	drives_flush();
	term_reset(&term);
	if (screen.cols != default_columns)
		term_set_columns(&term, default_columns); // the status line follows
	if (!drives_name(0)) {
		cpm.running = false;
		monitor_enter("Reset: no disk in drive A", false);
		return;
	}
	term_puts(&term, "\r\n*** Reset\r\n");
	machine_reset();
	cpm.running = true;
}

// Run the CPU for a slice, or the monitor's countdown.
static void cpm_step(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (cpm.reset_requested)
		cpm_reset();
	cpm_feed_keys(now);
	if (!setup.active) {
		if (cpm.running) {
			if (machine_run(CPU_SLICE_US) == MACHINE_STOPPED) {
				cpm.running = false;
				monitor_enter(machine_stop_reason(), false);
			}
		} else if (monitor_tick(now) == MONITOR_RUN) {
			cpm.running = true;
		}
	}
	drives_poll(now);
}

// The Status line setting's debug choice: core 1's timing and core 0's
// main-loop time, refreshed every STATUS_INTERVAL_MS in both modes.
static struct {
	char text[SCREEN_MAX_COLS + 1]; // as last drawn: redrawn as is between refreshes
	uint32_t last_ms, frames, late;  // at the last refresh
	uint32_t line_peak;              // compose cycles, since the debug line came on
	uint32_t render_peak_us;         // likewise
	uint32_t pass_start_us, pass_max_us, pass_peak_us; // main-loop passes
} debug_status;

static bool debug_status_on(void) {
	return settings.status_line == SETTINGS_STATUS_DEBUG || settings.status_line == SETTINGS_STATUS_DEBUG_USB;
}

// One main-loop pass ends and the next starts.
static void debug_status_pass(void) {
	uint32_t now = time_us_32(), us = now - debug_status.pass_start_us;
	debug_status.pass_start_us = now;
	if (us > debug_status.pass_max_us)
		debug_status.pass_max_us = us;
}

static unsigned percent(uint32_t part, uint32_t whole) {
	return whole ? (unsigned)((part * 100 + whole / 2) / whole) : 0;
}

// " late T+N | line C/P% | F fps | loop M/Q ms | render R/S us | Z MHz":
// late lines in all (+ since the last refresh); the longest scanline
// compose as a share of a line's time, this refresh / the peak; frames a
// second; the longest main-loop pass and the longest line render on core 0
// (core 1 can show that line half drawn meanwhile), now / peak; in CP/M
// mode the Z80's speed.
static void debug_status_refresh(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	video_stats_t st = video_stats();
	uint32_t ms = now - debug_status.last_ms;
	if (ms > 4 * STATUS_INTERVAL_MS) {
		// Just turned on (or the first time): the peaks start again.
		debug_status.line_peak = debug_status.pass_peak_us = debug_status.render_peak_us = 0;
		ms = 0;
	}
	if (st.max_line_cycles > debug_status.line_peak)
		debug_status.line_peak = st.max_line_cycles;
	uint32_t pass_us = debug_status.pass_max_us;
	debug_status.pass_max_us = 0;
	if (pass_us > debug_status.pass_peak_us)
		debug_status.pass_peak_us = pass_us;
	if (st.max_render_us > debug_status.render_peak_us)
		debug_status.render_peak_us = st.max_render_us;
	unsigned fps = ms ? (unsigned)(((st.frames - debug_status.frames) * 1000 + ms / 2) / ms) : 0;
	char *t = debug_status.text;
	size_t size = sizeof debug_status.text;
	int n = snprintf(t, size, " late %lu+%lu | line %u/%u%% | %u fps | loop %lu/%lu ms | render %lu/%lu us",
		(unsigned long)st.late, (unsigned long)(ms ? st.late - debug_status.late : 0),
		percent(st.max_line_cycles, st.cycles_budget), percent(debug_status.line_peak, st.cycles_budget), fps,
		(unsigned long)((pass_us + 500) / 1000), (unsigned long)((debug_status.pass_peak_us + 500) / 1000),
		(unsigned long)st.max_render_us, (unsigned long)debug_status.render_peak_us);
	if (cpm.on && cpm.running && n > 0 && (size_t)n < size)
		snprintf(t + n, size - (size_t)n, " | %lu.%02lu MHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	debug_status.last_ms = now;
	debug_status.frames = st.frames;
	debug_status.late = st.late;
	// debug usb: the USB host's last events instead, after the time, which
	// stops if the main loop (keyboard, USB, CP/M) does.
	if (settings.status_line == SETTINGS_STATUS_DEBUG_USB) {
		n = snprintf(t, size, " %lus | usb:", (unsigned long)(now / 1000));
		if (n > 0 && (size_t)n < size)
			usb_log_text(t + n, size - (size_t)n);
	}
}

static void draw_debug_status(void) {
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, debug_status.text, colours.status_fg, colours.status_bg);
}

// Drives accessed light up on the status line, like a drive's busy light;
// so does USB (bit 8) while the stick is read or written.
static uint16_t drive_lights(uint32_t now) {
	uint8_t accessed = drives_activity();
	uint16_t lit = 0;
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (accessed & 1u << d)
			cpm.light_until[d] = now + DRIVE_LIGHT_MS;
		if ((int32_t)(cpm.light_until[d] - now) > 0)
			lit |= (uint16_t)(1u << d);
	}
	if (usb_activity())
		cpm.usb_light_until = now + DRIVE_LIGHT_MS;
	if ((int32_t)(cpm.usb_light_until - now) > 0)
		lit |= 1u << DRIVES;
	return lit;
}

static void draw_cpm_status(void) {
	static uint32_t next_speed_ms;
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if ((int32_t)(now - next_speed_ms) >= 0) {
		next_speed_ms = now + 1000;
		cpm.khz = machine_speed_khz();
	}
	cpm.lit = drive_lights(now);
	if (setup.demo || !status_shown())
		return; // the demo has the status line
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
	char text[SCREEN_MAX_COLS + 1] = " CP/M |";
	unsigned letter_col[DRIVES];
	// The drives' letters, "-" for an empty one; SET-UP's Drives panel
	// shows what is mounted.
	size_t n = strlen(text);
	for (unsigned d = 0; d < DRIVES && n + 2 < sizeof text; ++d) {
		letter_col[d] = (unsigned)n + 1;
		text[n++] = ' ';
		text[n++] = drives_name(d) ? (char)('A' + d) : '-';
		text[n] = '\0';
	}
	char usb[12];
	usb_label(usb, sizeof usb);
	unsigned usb_col = 0;
	if (usb[0] && n + 4 + strlen(usb) < sizeof text) {
		usb_col = (unsigned)n + 3;
		snprintf(text + n, sizeof text - n, " | %s", usb);
		n = strlen(text);
	}
	if (cpm.running)
		snprintf(text + n, sizeof text - n, " | %lu.%02lu MHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	else
		snprintf(text + n, sizeof text - n, " | monitor");
	n = strlen(text);
	snprintf(text + n, sizeof text - n, "%s | %s", keyboards_attached() ? " | kbd" : "", TERM_BUILD_ID);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colours.status_fg, colours.status_bg);
	for (unsigned d = 0; d < DRIVES; ++d)
		if (cpm.lit & 1u << d && drives_name(d))
			screen_put(&screen, letter_col[d], STATUS_ROW, (uint8_t)('A' + d), colours.status_bg,
				colours.status_fg);
	if (usb_col && cpm.lit & 1u << DRIVES)
		for (unsigned i = 0; i < 3; ++i)
			screen_put(&screen, usb_col + i, STATUS_ROW, (uint8_t)usb[i], colours.status_bg, colours.status_fg);
}

typedef struct {
	unsigned keyboards;
	uint32_t keys_sent; // last_key.count
	settings_t settings;
	char usb[12];       // usb_label
	const char *type;   // term_type_name: the host can switch VT52 mode
} status_t;

// The terminal type now, for the status line.
static const char *term_type_name(void) {
	return term.personality == TERM_ADM3A ? "ADM-3A" : term.vt100.vt52 ? "VT52" : "VT100";
}

static status_t status_now(void) {
	status_t st = {keyboards_attached(), last_key.count, settings, "", term_type_name()};
	usb_label(st.usb, sizeof st.usb);
	return st;
}

static bool status_equal(const status_t *a, const status_t *b) {
	return a->keyboards == b->keyboards && a->keys_sent == b->keys_sent &&
		memcmp(&a->settings, &b->settings, sizeof a->settings) == 0 && strcmp(a->usb, b->usb) == 0 &&
		a->type == b->type;
}

static void draw_status(const status_t *st) {
	if (setup.demo || !status_shown())
		return; // the demo has the status line; DECCOLM after it redraws
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
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
	snprintf(text, sizeof text, " %s %ux24 | %s | %u %u%c%u%s%s%s%s%s%s",
		st->type, screen.cols, TERM_BUILD_ID, settings_baud_rate(&st->settings), data_bits, parity, stop_bits,
		SERIAL_FLOW_CONTROL ? " RTS/CTS" : "", st->keyboards ? " | kbd" : "", st->usb[0] ? " | " : "", st->usb,
		st->settings.local_echo ? " | echo" : "", key);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colours.status_fg, colours.status_bg);
}

int main(void) {
#ifdef BOARD_PSRAM_CS_PIN
	// The PSRAM shares the QSPI bus with the flash. Its CE pull-up against
	// the pad's reset pull-down leaves CE barely above its input-high level,
	// so drive it high: nothing uses the PSRAM yet.
	gpio_init(BOARD_PSRAM_CS_PIN);
	gpio_put(BOARD_PSRAM_CS_PIN, 1);
	gpio_set_dir(BOARD_PSRAM_CS_PIN, GPIO_OUT);
#endif
	diag_init();
	// Read the settings while the flash is still in its boot-time setup.
	nvflash_init();
	const uint8_t *slots[SETTINGS_SLOTS];
	for (unsigned i = 0; i < SETTINGS_SLOTS; ++i)
		slots[i] = nvflash_sector(i);
	settings_load(slots, &settings, &save_target);
	colours = settings_colours(&settings); // the status line under the logo uses them

	video_init();
	// Before anything reads flash at the new clock: the settings saves,
	// with LOGO_IN_FLASH the logo, and the cold code left in flash
	// (ld/section_copy_to_ram_rodata.incl), palette.c's included.
	nvflash_retime();
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
	if (STARTUP_DEMO && !input)
		pending = run_demo(buf, sizeof buf);

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
	if (cpm.on)
		draw_cpm_status();
	else
		draw_status(&shown);

	char banner[SCREEN_MAX_COLS + 1];
	snprintf(banner, sizeof banner, "DVI Feather Terminal build %s (%s, %s)\r\n",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	term_puts(&term, banner);
	if (cpm.on) {
		// UART input before now was for the terminal, not the Z80.
		term_puts(&term, "Z80 CP/M machine: " BOARD_UART_NAME " (GP" BOARD_STR(BOARD_UART_TX_PIN) "/GP" BOARD_STR(
			BOARD_UART_RX_PIN) ") is its second serial port\r\n");
		machine_init(&cpm_platform);
		usbkbd_set_reset_key(reset_key);
		monitor_start(&cpm_monitor, true, to_ms_since_boot(get_absolute_time()));
	} else {
#if SERIAL_FLOW_CONTROL
		term_puts(&term, BOARD_UART_PINS_TEXT BOARD_UART_FLOW_TEXT "\r\n\r\n");
#else
		term_puts(&term, BOARD_UART_PINS_TEXT "\r\n\r\n");
#endif
		if (pending)
			term_write(&term, buf, pending);
	}
	video_update(&screen);

	absolute_time_t next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
	debug_status.pass_start_us = time_us_32();
	while (true) {
		if (cpm.on) {
			cpm_step();
			// A drive light changed: show it now, not at the next status update.
			if (drive_lights(to_ms_since_boot(get_absolute_time())) != cpm.lit)
				draw_cpm_status();
			// The Z80's output asked for SET-UP (CSI ? 999 ~).
			if (term.setup_requests != setup_requests) {
				setup_requests = term.setup_requests;
				if (!setup.active)
					setup_enter(false);
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
		if (setup.open_requested) {
			setup.open_requested = false;
			if (!setup.active)
				setup_enter(false);
		}
		if (setup.active)
			setup_service();
		serial_poll();
		led_poll(screen_blanked);

		uint32_t now = to_ms_since_boot(get_absolute_time());
		if (setup.active)
			blank_activity(&blank, now); // not idle while SET-UP is open
		screen_blanked = blank_tick(&blank, now);
		video_set_blank(screen_blanked);

		// DECCOLM re-initialises the whole screen, status line included,
		// whether or not the width changes.
		if (term.screen_resets != shown_resets) {
			shown_resets = term.screen_resets;
			if (cpm.on)
				draw_cpm_status();
			else
				draw_status(&shown);
		}
		if (time_reached(next_status)) {
			next_status = make_timeout_time_ms(STATUS_INTERVAL_MS);
			if (debug_status_on())
				debug_status_refresh();
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
		video_update(&screen);
		diag_poll();
		debug_status_pass();
	}
}
