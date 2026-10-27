// SET-UP and what opens over its rows (terminal.h): the panel itself
// (setup.c), the Drives panel (drivepanel.c, CP/M mode), the Clock panel
// (clockpanel.c), the firmware update (fwpanel.c) and the demo (T). Keys arrive from the USB stack
// (setup_feed); anything slow, or touching the SD card or the flash, waits
// for the main loop (setup_service).

#include <assert.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "audio.h"
#include "ay8910.h"
#include "bell.h"
#include "board.h"
#include "demo.h"
#include "ff.h"
#include "fwupdate.h"
#include "hstx_dvi.h"
#include "nvflash.h"
#include "palette.h"
#include "raster.h"
#include "rtc.h"
#include "sdcard.h"
#include "term_build.h"
#include "terminal.h"
#include "tune.h"
#include "usbkbd.h"
#include "usbmsc.h"
#include "video.h"
#include "xfer.h"

#define STASH_SETUP 'S'

setup_state_t setup;

static_assert(DRIVEPANEL_ROWS <= SETUP_ROWS, "the Drives panel fits SET-UP's rows");
static_assert(FWPANEL_ROWS <= SETUP_ROWS, "the firmware panel fits SET-UP's rows");
static_assert(CLOCKPANEL_ROWS <= SETUP_ROWS, "the Clock panel fits SET-UP's rows");

static void ay_give_back(void);

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

// The Clock panel, at the bottom of SET-UP's rows like the Drives panel.
static void clock_draw_panel(void) {
	settings_colors_t c = settings_colors(&setup.ui.values);
	for (unsigned r = 0; r < SETUP_ROWS - CLOCKPANEL_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	clockpanel_draw(&setup.clock, &screen, SETUP_TOP + SETUP_ROWS - CLOCKPANEL_ROWS, PALETTE_TEXT,
		PALETTE_BACKGROUND, c.status_bg);
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

// A key for the Clock panel. Setting the clock (the RTC's I2C) is left for
// the main loop.
static void clock_feed(uint8_t c, uint32_t now) {
	clockpanel_action_t a = clockpanel_input(&setup.clock, c, now);
	if (a == CLOCKPANEL_REDRAW) {
		clock_draw_panel();
	} else if (a == CLOCKPANEL_APPLY) {
		setup.clock_apply = true;
	} else if (a == CLOCKPANEL_CANCEL) {
		setup.clock_open = false;
		setup_draw_panel();
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
		!setup.fw_cancel && !setup.clock_apply; ++i) {
		if (setup.fw_open) {
			fw_feed(data[i]);
			continue;
		}
		if (setup.clock_open) {
			clock_feed(data[i], now);
			continue;
		}
		if (setup.drives_open) {
			drives_feed(data[i], now);
			continue;
		}
#if USB_RESTART_KEY
		if (data[i] == 'h' || data[i] == 'H') { // test builds (bd nxv): restart the USB host
			usbkbd_request_reset("hkey");
			continue;
		}
#endif
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
	setup.ui.no_rtscts = !BOARD_HAS_RTSCTS;
	setup.bell_heard = current.bell;
	setup.sound_heard = current.sound_level;
	setup.stereo_heard = current.sound_stereo;
	setup.active = true;
	setup.drives_open = false;
	setup.drives_work = DRIVEPANEL_NONE;
	setup.fw_open = setup.fw_work = setup.fw_cancel = false;
	setup.clock_open = setup.clock_apply = false;
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
	setup.clock_open = setup.clock_apply = false;
	fw_close();
	ay_give_back(); // a chime still sounding
	if (!apply) {
		show_palette(&settings); // back from SET-UP's preview
		bell_set_level(settings.bell);
		audio_set_ay_level(settings.sound_level);
		audio_set_stereo(settings.sound_stereo);
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

// The Clock and sound panel's Return: the clock set if a time was typed
// in, its settings' choices kept in SET-UP's edited settings (its Return
// saves them); or its Escape key timeout, and the time it shows ticking.
static void clock_service(void) {
	if (!setup.clock_apply) {
		clockpanel_action_t a = clockpanel_tick(&setup.clock, to_ms_since_boot(get_absolute_time()), rtc_now());
		if (a == CLOCKPANEL_REDRAW) {
			clock_draw_panel();
		} else if (a == CLOCKPANEL_CANCEL) {
			setup.clock_open = false;
			setup_draw_panel();
		}
		return;
	}
	setup.clock_apply = false;
	setup.clock_open = false;
	uint32_t secs;
	bool set = clockpanel_time(&setup.clock, &secs);
	if (set) {
		rtc_set(secs);
	}
	const settings_t *v = &setup.clock.values;
	settings_t *u = &setup.ui.values;
	bool changed = v->clock != u->clock || v->clock_seconds != u->clock_seconds || v->clock_date != u->clock_date
		|| v->bell != u->bell || v->sound_level != u->sound_level || v->sound_dac != u->sound_dac
		|| v->sound_stereo != u->sound_stereo;
	u->clock = v->clock;
	u->clock_seconds = v->clock_seconds;
	u->clock_date = v->clock_date;
	u->bell = v->bell;
	u->sound_level = v->sound_level;
	u->sound_dac = v->sound_dac;
	u->sound_stereo = v->sound_stereo;
	setup.ui.message = set && changed ? "Clock set; Return here saves the clock and sound settings"
		: set ? "Clock set"
		: changed ? "Return here saves the clock and sound settings"
		: NULL;
	setup_draw_panel();
}

// The demo screen at cols, with `what` on its status line.
void draw_demo(unsigned cols, const char *what) {
	char status[SCREEN_MAX_COLS + 1];
	snprintf(status, sizeof status, " demo | %u x 24 | build %s (%s, %s) | %s",
		cols, TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE, what);
	// The mode as sent (bd cpx.4: it said 800x600p60 at 1024x600 too).
	const uint32_t frame_us = hstx_dvi_frame_us();
	char mode[24];
	snprintf(mode, sizeof mode, "%ux%up%lu", hstx_timing.h_active, hstx_timing.v_active,
		(unsigned long)((1000000u + frame_us / 2) / frame_us));
	demo_draw(&screen, cols, mode, raster_font_name(raster_layout(cols)), status);
}

// SET-UP's sounds on the AY (tune.c): the demo's tune, and the chime for a
// new Sound chip level or Stereo. The Z80 is stopped while SET-UP is open,
// so they borrow its chip, attached meanwhile in terminal mode, in the
// edited Stereo (the program's AY_STEREO_REG set aside); its registers are
// put back when the piece ends (the chime), the demo ends or SET-UP closes.
static tune_t ay_tune;
static uint8_t ay_saved[AY_STEREO_REG + 1];
static bool ay_borrowed;

static void ay_play(uint8_t piece) {
	if (!bell_present()) {
		return;
	}
	if (!ay_borrowed) {
		for (uint8_t r = 0; r <= AY_STEREO_REG; ++r) {
			ay_saved[r] = audio_ay_read(r);
		}
		audio_ay_write(AY_STEREO_REG, 0);
		audio_attach_ay(true);
		ay_borrowed = true;
	}
	tune_start(&ay_tune, piece, audio_ay_write, to_ms_since_boot(get_absolute_time()));
}

static void ay_give_back(void) {
	if (!ay_borrowed) {
		return;
	}
	ay_borrowed = false;
	for (uint8_t r = 0; r <= AY_STEREO_REG; ++r) {
		audio_ay_write(r, ay_saved[r]);
	}
	audio_attach_ay(cpm.on);
}

static void ay_service(void) {
	if (ay_borrowed && !tune_poll(&ay_tune, to_ms_since_boot(get_absolute_time()))) {
		ay_give_back();
	}
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
	if (setup.ui.values.sound_level != 0) {
		ay_play(TUNE_KOROBEINIKI);
	}
}

static void setup_demo_service(void) {
	if (setup.demo_end) {
		ay_give_back();
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
// key timeout; show the edited Colors and Palette and sound the edited Bell.
void setup_service(void) {
	show_palette(&setup.ui.values);
	// The sound levels as edited (in the Clock and sound panel while it is
	// open): a new Bell heard at once, as the palette is seen; the AY's
	// level heard on whatever it plays. Back from the panel without them:
	// quietly.
	const settings_t *v = setup.clock_open ? &setup.clock.values : &setup.ui.values;
	if (v->bell != setup.bell_heard) {
		setup.bell_heard = v->bell;
		bell_set_level(setup.bell_heard);
		if (setup.clock_open) {
			bell_ring();
		}
	}
	if (v->sound_level != setup.sound_heard) {
		setup.sound_heard = v->sound_level;
		audio_set_ay_level(setup.sound_heard);
		if (setup.clock_open && setup.sound_heard != 0) {
			ay_play(TUNE_CHIME); // heard at the new level
		}
	}
	if (v->sound_stereo != setup.stereo_heard) {
		setup.stereo_heard = v->sound_stereo;
		audio_set_stereo(setup.stereo_heard);
		if (setup.clock_open && v->sound_level != 0) {
			ay_play(TUNE_CHIME); // on A: from the left in ABC and ACB
		}
	}
	ay_service();
	if (setup.demo) {
		setup_demo_service();
		return;
	}
	if (setup.drives_open && setup.action == SETUP_NONE) {
		drives_service();
		return;
	}
	if (setup.clock_open && setup.action == SETUP_NONE) {
		clock_service();
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
	} else if (a == SETUP_CLOCK) {
		rtc_state_t st = rtc_state();
		clockpanel_open(&setup.clock, &setup.ui.values, rtc_now(), st != RTC_NONE && st != RTC_MISSING,
			BOARD_AUDIO_I2S != 0);
		setup.clock_open = true;
		clock_draw_panel();
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
