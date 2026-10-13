#include "fwpanel.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"

// The text starts inside the box (screen_frame).
#define LEFT 1
#define BAR_WIDTH 50

static const char *const paths[FWPANEL_SOURCES] = {"0:" FWPANEL_FILE, "1:" FWPANEL_FILE};
static const char *const old_paths[FWPANEL_SOURCES] = {"0:" FWPANEL_FILE_OLD, "1:" FWPANEL_FILE_OLD};

const char *fwpanel_path(fwpanel_source_t source) {
	return paths[source];
}

void fwpanel_open(fwpanel_t *p) {
	*p = (fwpanel_t){.state = FWPANEL_SEARCH, .step = "Looking for FIRMWARE.UF2 on the SD card and USB stick..."};
}

fwpanel_action_t fwpanel_input(fwpanel_t *p, uint8_t c) {
	switch (p->state) {
	case FWPANEL_PICK:
		if (c == 's' || c == 'S' || c == 'u' || c == 'U') {
			p->source = c == 's' || c == 'S' ? FWPANEL_SD : FWPANEL_USB;
			p->state = FWPANEL_CHECK;
			p->step = "Checking it...";
			return FWPANEL_WORK;
		}
		return c == 'n' || c == 'N' || c == 0x1b ? FWPANEL_CANCEL : FWPANEL_NONE;
	case FWPANEL_ASK:
		if (c == 'y' || c == 'Y') {
			p->state = FWPANEL_STAGE;
			p->step = "Writing it to flash beside the running firmware";
			return FWPANEL_WORK;
		}
		return c == 'n' || c == 'N' || c == 0x1b ? FWPANEL_CANCEL : FWPANEL_NONE;
	case FWPANEL_FAILED:
		return FWPANEL_CANCEL;
	default:
		return FWPANEL_NONE; // busy: keys are dropped
	}
}

static void fail(fwpanel_t *p, const char *step) {
	p->state = FWPANEL_FAILED;
	p->step = step;
}

// The staging's progress callback context.
typedef struct {
	fwpanel_t *p;
	const fwpanel_platform_t *plat;
} progress_ctx_t;

static void progress(void *ctx, uint32_t done, uint32_t total) {
	progress_ctx_t *c = ctx;
	// Redraw only when the bar grows: drawing holds up the writing.
	unsigned before = c->p->total ? (unsigned)((uint64_t)c->p->done * BAR_WIDTH / c->p->total) : 0;
	c->p->done = done;
	c->p->total = total;
	if (!total || (unsigned)((uint64_t)done * BAR_WIDTH / total) != before)
		c->plat->show(c->p);
}

bool fwpanel_retire_file(fwpanel_source_t source) {
	f_unlink(old_paths[source]);
	return f_rename(paths[source], old_paths[source]) == FR_OK;
}

static bool found(fwpanel_source_t source) {
	FILINFO info;
	return f_stat(paths[source], &info) == FR_OK && !(info.fattrib & AM_DIR);
}

// Check p->source's file and ask.
static void check(fwpanel_t *p, const fwpanel_platform_t *plat) {
	bool usb = p->source == FWPANEL_USB;
	if (!plat->check(paths[p->source], p->info, sizeof p->info)) {
		fail(p, usb ? "FIRMWARE.UF2 on the USB stick cannot be installed:"
			: "FIRMWARE.UF2 on the SD card cannot be installed:");
		return;
	}
	p->state = FWPANEL_ASK;
	p->step = usb ? "Install it from the USB stick and restart (Y/N)?"
		: "Install it from the SD card and restart (Y/N)?";
}

// Look on the card and the stick: the file on one is checked, on both the
// user picks.
static void search(fwpanel_t *p, const fwpanel_platform_t *plat) {
	bool card = p->mounted = plat->mount(p->info, sizeof p->info); // info: why not
	bool stick = plat->stick && plat->stick();
	bool on_card = card && found(FWPANEL_SD);
	bool on_stick = stick && found(FWPANEL_USB);
	if (on_card && on_stick) {
		p->state = FWPANEL_PICK;
		p->step = "FIRMWARE.UF2 is on both the SD card and the USB stick.";
		snprintf(p->info, sizeof p->info, "Install which one?");
	} else if (on_card || on_stick) {
		p->source = on_stick ? FWPANEL_USB : FWPANEL_SD;
		check(p, plat);
	} else if (!card && !stick) {
		fail(p, "No SD card or USB stick to update from:"); // the card's reason
	} else {
		fail(p, "No FIRMWARE.UF2 to install:");
		snprintf(p->info, sizeof p->info, "none in the root of %s",
			card && stick ? "the SD card or the USB stick"
			: card ? "the SD card (and no USB stick)" : "the USB stick (and no SD card)");
	}
}

fwpanel_action_t fwpanel_run(fwpanel_t *p, const fwpanel_platform_t *plat) {
	if (p->state == FWPANEL_SEARCH || p->state == FWPANEL_CHECK) {
		plat->show(p);
		if (p->state == FWPANEL_SEARCH)
			search(p, plat);
		else
			check(p, plat);
		return FWPANEL_REDRAW;
	}
	if (p->state != FWPANEL_STAGE)
		return FWPANEL_NONE;
	plat->show(p);
	progress_ctx_t ctx = {p, plat};
	if (!plat->stage(paths[p->source], progress, &ctx, p->info, sizeof p->info)) {
		fail(p, "Writing it failed (nothing was changed):");
		return FWPANEL_REDRAW;
	}
	bool renamed = fwpanel_retire_file(p->source);
	// Leave the card's file system clean: the board restarts from here.
	plat->unmount(true);
	p->mounted = false;
	snprintf(p->info, sizeof p->info, "The board restarts with it in a few seconds.%s",
		renamed ? "" : " (FIRMWARE.UF2 not renamed)");
	p->step = "Installing the new firmware: do not power off.";
	plat->show(p);
	plat->commit();
	// Only on the host: as after a failure.
	fail(p, "The firmware was not installed.");
	return FWPANEL_REDRAW;
}

void fwpanel_close(fwpanel_t *p, const fwpanel_platform_t *plat) {
	if (p->mounted)
		plat->unmount(false);
	p->mounted = false;
}

void fwpanel_draw(const fwpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, FWPANEL_ROWS, "SET-UP  Firmware update", fg, bg, border);
	if (p->step)
		screen_puts(s, LEFT + 2, top + 2, p->step, fg, bg);
	if (p->state != FWPANEL_SEARCH && p->state != FWPANEL_CHECK)
		screen_puts(s, LEFT + 2, top + 3, p->info, fg, bg);
	if (p->state == FWPANEL_STAGE && p->total) {
		char bar[BAR_WIDTH + 16];
		unsigned n = (unsigned)((uint64_t)p->done * BAR_WIDTH / p->total);
		bar[0] = '[';
		for (unsigned i = 0; i < BAR_WIDTH; ++i)
			bar[1 + i] = i < n ? '#' : '.';
		snprintf(bar + 1 + BAR_WIDTH, sizeof bar - 1 - BAR_WIDTH, "] %3u%%",
			(unsigned)((uint64_t)p->done * 100 / p->total));
		screen_puts(s, LEFT + 2, top + 4, bar, fg, bg);
	}
	const char *help = p->state == FWPANEL_ASK ? " Y install and restart   N or Esc back to SET-UP"
		: p->state == FWPANEL_PICK ? " S SD card   U USB stick   N or Esc back to SET-UP"
		: p->state == FWPANEL_FAILED ? " Any key: back to SET-UP"
		: " Please wait";
	screen_puts(s, LEFT, top + FWPANEL_ROWS - 2, help, fg, bg);
}
