#include "drivepanel.h"

#include <stdio.h>
#include <string.h>


// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_COL (LEFT + 6)

static int index_of(const drivepanel_t *p, const char *name) {
	if (name)
		for (unsigned i = 0; i < p->n_images; ++i)
			if (strcmp(p->images[i], name) == 0)
				return (int)i;
	return -1;
}

void drivepanel_open(drivepanel_t *p) {
	memset(p, 0, sizeof *p);
	p->n_images = drives_list(p->images, DRIVES_LIST_MAX);
	for (unsigned d = 0; d < DRIVES; ++d)
		p->choice[d] = p->mounted[d] = index_of(p, drives_name(d));
}

static bool chosen_elsewhere(const drivepanel_t *p, unsigned drive, int image) {
	for (unsigned d = 0; d < DRIVES; ++d)
		if (d != drive && p->choice[d] == image)
			return true;
	return false;
}

// The next image (or none) for the selected drive, skipping those chosen
// for another drive.
static void step(drivepanel_t *p, int dir) {
	int n = (int)p->n_images + 1; // index + 1: 0 is none
	int at = p->choice[p->drive] + 1;
	for (int tries = 0; tries < n; ++tries) {
		at = (at + dir + n) % n;
		if (at == 0 || !chosen_elsewhere(p, p->drive, at - 1))
			break;
	}
	p->choice[p->drive] = at - 1;
}

static drivepanel_action_t cursor_key(drivepanel_t *p, uint8_t final) {
	switch (final) {
	case 'A':
		p->drive = (p->drive + DRIVES - 1) % DRIVES;
		return DRIVEPANEL_REDRAW;
	case 'B':
		p->drive = (p->drive + 1) % DRIVES;
		return DRIVEPANEL_REDRAW;
	case 'C':
		step(p, 1);
		return DRIVEPANEL_REDRAW;
	case 'D':
		step(p, -1);
		return DRIVEPANEL_REDRAW;
	default:
		return DRIVEPANEL_NONE;
	}
}

drivepanel_action_t drivepanel_input(drivepanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	drivepanel_action_t action = DRIVEPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			return had_message ? DRIVEPANEL_REDRAW : DRIVEPANEL_NONE;
		}
		p->esc = 0;
		return DRIVEPANEL_CANCEL;
	}
	if (p->esc == 2) {
		if (c >= 0x40 && c <= 0x7e) {
			p->esc = 0;
			action = cursor_key(p, c);
		}
	} else if (c == 0x1b) {
		p->esc = 1;
		p->esc_ms = now_ms;
	} else if (c == ' ') {
		step(p, 1);
		action = DRIVEPANEL_REDRAW;
	} else if (c == '\r' || c == '\n') {
		return DRIVEPANEL_APPLY;
	}
	return action == DRIVEPANEL_NONE && had_message ? DRIVEPANEL_REDRAW : action;
}

drivepanel_action_t drivepanel_tick(drivepanel_t *p, uint32_t now_ms) {
	if (p->esc == 1 && now_ms - p->esc_ms >= DRIVEPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return DRIVEPANEL_CANCEL;
	}
	return DRIVEPANEL_NONE;
}

unsigned drivepanel_apply(drivepanel_t *p, bool *failed) {
	unsigned changed = 0;
	*failed = false;
	drives_flush();
	// Unmount first: an image may move from one drive to another.
	for (unsigned d = 0; d < DRIVES; ++d)
		if (p->choice[d] != p->mounted[d])
			drives_unmount(d);
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (p->choice[d] == p->mounted[d])
			continue;
		++changed;
		if (p->choice[d] >= 0 && drives_mount(d, p->images[p->choice[d]]) != DRIVES_OK)
			*failed = true;
	}
	if (changed)
		drives_save_config();
	return changed;
}

void drivepanel_draw(const drivepanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, DRIVEPANEL_ROWS, "SET-UP  Drives (CP/M sees a changed drive after ^C)", fg, bg, border);
	for (unsigned d = 0; d < DRIVES; ++d) {
		unsigned row = top + 1 + d;
		char text[SCREEN_MAX_COLS + 1];
		bool selected = d == p->drive;
		snprintf(text, sizeof text, " %c %c:", selected ? '>' : ' ', 'A' + d);
		for (unsigned c = 0; text[c]; ++c)
			screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
		int c = p->choice[d];
		snprintf(text, sizeof text, "< %-8s >", c >= 0 ? p->images[c] : "none");
		screen_puts(s, NAME_COL, row, text, bg, fg);
		const char *info = "";
		if (c >= 0) {
			const drives_geometry_t *g = drives_geometry(drives_image_format(p->images[c]));
			info = g ? g->name : "not a disk image";
		}
		snprintf(text, sizeof text, " %s%s", info, c != p->mounted[d] ? "  (changed)" : "");
		screen_puts(s, NAME_COL + 12, row, text, fg, bg);
	}
	if (p->message)
		screen_puts(s, LEFT + 3, top + DRIVEPANEL_ROWS - 3, p->message, fg, bg);
	screen_puts(s, LEFT, top + DRIVEPANEL_ROWS - 2,
		" Up/Down drive   Left/Right/Space disk   Return mount   Esc back", fg, bg);
}
