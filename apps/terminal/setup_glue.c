// SET-UP and what opens over its rows (terminal.h): the panel itself
// (setup.c), the Drives panel (drivepanel.c, CP/M mode), the firmware
// update (fwpanel.c) and the demo (T). Keys arrive from the USB stack
// (setup_feed); anything slow, or touching the SD card or the flash, waits
// for the main loop (setup_service).

#include <assert.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "board.h"
#include "demo.h"
#include "ff.h"
#include "fwupdate.h"
#include "nvflash.h"
#include "palette.h"
#include "raster.h"
#include "sdcard.h"
#include "term_build.h"
#include "terminal.h"
#include "usbmsc.h"
#include "video.h"
#include "xfer.h"

#define STASH_SETUP 'S'

setup_state_t setup;

static_assert(DRIVEPANEL_ROWS <= SETUP_ROWS, "the Drives panel fits SET-UP's rows");
static_assert(FWPANEL_ROWS <= SETUP_ROWS, "the firmware panel fits SET-UP's rows");

// The panels are boxes on the default background, the box in the status
// line's color of the edited settings, like the preview (setup_service
// shows their palette).
static void setup_draw_panel(void) {
	char title[SCREEN_MAX_COLS + 1];
	snprintf(title, sizeof title, "DVI Terminal build %s", TERM_BUILD_ID);
	settings_colors_t c = settings_colors(&setup.ui.values);
	setup_draw(&setup.ui, &screen, SETUP_TOP, title, PALETTE_TEXT, PALETTE_BACKGROUND, c.status_bg);
}

// At the bottom of SET-UP's rows, the rows above it blank.
static void drives_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - DRIVEPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	drivepanel_draw(&setup.drives, &screen, SETUP_TOP + SETUP_ROWS - DRIVEPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
}

// The firmware panel, at the bottom of SET-UP's rows like the Drives panel.
static void fw_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - FWPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	fwpanel_draw(&setup.fw, &screen, SETUP_TOP + SETUP_ROWS - FWPANEL_ROWS, PALETTE_TEXT, PALETTE_BACKGROUND,
		c.status_bg);
}

// The firmware panel's platform. In CP/M mode the monitor keeps the card
// mounted (with the drives' images open); in terminal mode the card is
// mounted only while the panel is open, with the monitor's FATFS (the
// monitor never runs in terminal mode). The stick (volume 1) is usbmsc.c's,
// mounted in both modes.

static bool fw_stick(void) {
	return usbmsc_status().mounted;
}

static bool fw_mount(char *info, size_t n) {
	if (cpm.on) {
		return true;
	}
	FRESULT r = f_mount(monitor_card_fs(), "", 1);
	if (r == FR_OK) {
		return true;
	}
	if (sdcard_type() == SDCARD_NONE) {
		snprintf(info, n, "none found (" BOARD_SD_PINS_TEXT ")");
	} else {
		snprintf(info, n, "no FAT file system on it (error %d)", (int)r);
	}
	return false;
}

static void fw_unmount(bool commit) {
	if (cpm.on && !commit) {
		return; // the monitor's
	}
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
	if (setup.fw_open) {
		fwpanel_close(&setup.fw, &fw_platform);
	}
	setup.fw_open = setup.fw_work = setup.fw_cancel = false;
}

// A key for the firmware panel. The work is left for the main loop.
static void fw_feed(uint8_t c) {
	fwpanel_action_t a = fwpanel_input(&setup.fw, c);
	if (a == FWPANEL_WORK) {
		setup.fw_work = true;
	} else if (a == FWPANEL_CANCEL) {
		setup.fw_cancel = true;
	}
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
void setup_feed(const uint8_t *data, size_t len) {
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
		if (a == SETUP_REDRAW) {
			setup_draw_panel();
		} else if (a != SETUP_NONE) {
			setup.action = a;
		}
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

void setup_enter(bool from_host) {
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

// Put the rows back; with apply, use the edited settings now, and the width
// if it differs (which clears the screen, as DECCOLM does).
static void setup_leave(bool apply) {
	if (!screen_stash_pop(&stash, &screen, STASH_SETUP, PALETTE_TEXT, PALETTE_BACKGROUND)) {
		for (unsigned r = 0; r < SETUP_ROWS; ++r) { // not saved: no room
			screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
		}
	}
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
	if (settings_columns(&setup.ui.values) != screen.cols) {
		term_set_columns(&term, settings_columns(&setup.ui.values));
	}
}

static bool setup_save(void) {
	static uint8_t page[SETTINGS_RECORD_SIZE]; // RAM, for the flash ROM routines
	settings_encode(&setup.ui.values, save_target.seq, page);
	if (!nvflash_write(save_target.slot, page)) {
		return false;
	}
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
		if (work == DRIVEPANEL_SAVE_SET) {
			drivepanel_save_set(&setup.drives);
		} else {
			drivepanel_remove_set(&setup.drives);
		}
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
void draw_demo(unsigned cols, const char *what) {
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
	if (cpm.on) {
		drives_flush();
	}
	watchdog_reboot(0, 0, 0);
	while (true) {
		tight_loop_contents();
	}
}

// Run SET-UP's pending save or cancel, the demo, the reboot, or the Escape
// key timeout, and show the edited Colors and Palette.
void setup_service(void) {
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
	if (setup.action == SETUP_NONE) {
		setup.action = setup_tick(&setup.ui, to_ms_since_boot(get_absolute_time()));
	}
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
			if (settings.mode != (cpm.on ? SETTINGS_MODE_CPM : SETTINGS_MODE_TERMINAL)) {
				restart(); // the other mode starts from a clean power-on
			}
			setup_leave(true);
		} else {
			setup.ui.message = "Saving to flash failed";
			setup_draw_panel();
		}
	}
}
