#include "fwpanel.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "monitor.h" // MONITOR_FIRMWARE, MONITOR_FIRMWARE_OLD

// The text starts inside the box (screen_frame).
#define LEFT 1
#define BAR_WIDTH 50

void fwpanel_open(fwpanel_t *p) {
	*p = (fwpanel_t){.state = FWPANEL_CHECK, .step = "Checking FIRMWARE.UF2 on the SD card..."};
}

fwpanel_action_t fwpanel_input(fwpanel_t *p, uint8_t c) {
	switch (p->state) {
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

bool fwpanel_retire_file(void) {
	f_unlink(MONITOR_FIRMWARE_OLD);
	return f_rename(MONITOR_FIRMWARE, MONITOR_FIRMWARE_OLD) == FR_OK;
}

fwpanel_action_t fwpanel_run(fwpanel_t *p, const fwpanel_platform_t *plat) {
	if (p->state == FWPANEL_CHECK) {
		plat->show(p);
		if (!plat->mount(p->info, sizeof p->info)) {
			fail(p, "No SD card to update from:");
			return FWPANEL_REDRAW;
		}
		p->mounted = true;
		if (!plat->check(MONITOR_FIRMWARE, p->info, sizeof p->info)) {
			fail(p, "FIRMWARE.UF2 cannot be installed:");
			return FWPANEL_REDRAW;
		}
		p->state = FWPANEL_ASK;
		p->step = "Install it and restart (Y/N)?";
		return FWPANEL_REDRAW;
	}
	if (p->state != FWPANEL_STAGE)
		return FWPANEL_NONE;
	plat->show(p);
	progress_ctx_t ctx = {p, plat};
	if (!plat->stage(MONITOR_FIRMWARE, progress, &ctx, p->info, sizeof p->info)) {
		fail(p, "Writing it failed (nothing was changed):");
		return FWPANEL_REDRAW;
	}
	bool renamed = fwpanel_retire_file();
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
	if (p->state != FWPANEL_CHECK)
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
		: p->state == FWPANEL_FAILED ? " Any key: back to SET-UP"
		: " Please wait";
	screen_puts(s, LEFT, top + FWPANEL_ROWS - 2, help, fg, bg);
}
