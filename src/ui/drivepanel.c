#include "drivepanel.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>


// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_COL (LEFT + 6)

static int index_of(const drivepanel_t *p, const char *name) {
	if (name) {
		for (unsigned i = 0; i < p->n_images; ++i) {
			if (strcmp(p->images[i], name) == 0) {
				return (int)i;
			}
		}
	}
	return -1;
}

// The set the choices are (a set with images not on the card: as picked,
// with those drives none), else -1.
static void match_set(drivepanel_t *p) {
	p->set = -1;
	for (unsigned i = 0; i < p->n_sets; ++i) {
		unsigned d = 0;
		while (d < DRIVES && p->set_choice[i][d] == p->choice[d]) {
			++d;
		}
		if (d == DRIVES) {
			p->set = (int)i;
			return;
		}
	}
}

// Read the sets on the card. An image not on the card, or on two drives
// (a set written by hand), is left as none and counted.
static void read_sets(drivepanel_t *p) {
	p->n_sets = drives_sets(p->sets, DRIVES_SETS_MAX);
	for (unsigned i = 0; i < p->n_sets; ++i) {
		char images[DRIVES][DRIVES_NAME_MAX + 1];
		if (!drives_set_read(p->sets[i], images)) {
			memset(images, 0, sizeof images);
		}
		p->set_missing[i] = 0;
		for (unsigned d = 0; d < DRIVES; ++d) {
			int at = images[d][0] ? index_of(p, images[d]) : -1;
			for (unsigned e = 0; at >= 0 && e < d; ++e) {
				if (p->set_choice[i][e] == at) {
					at = -1;
				}
			}
			if (images[d][0] && at < 0) {
				++p->set_missing[i];
			}
			p->set_choice[i][d] = (int8_t)at;
		}
	}
	match_set(p);
}

void drivepanel_open(drivepanel_t *p) {
	memset(p, 0, sizeof *p);
	p->n_images = drives_list(p->images, DRIVES_LIST_MAX);
	for (unsigned d = 0; d < DRIVES; ++d) {
		p->choice[d] = p->mounted[d] = index_of(p, drives_name(d));
	}
	read_sets(p);
}

// ---- the Disks page (bd 5rq) ----

// Keep the selected disk in the list and on the screen.
static void show_disk(drivepanel_t *p) {
	if (p->disk >= p->n_images) {
		p->disk = p->n_images ? p->n_images - 1 : 0;
	}
	if (p->disk < p->disk_top) {
		p->disk_top = p->disk;
	} else if (p->disk >= p->disk_top + DRIVEPANEL_DISK_ROWS) {
		p->disk_top = p->disk - DRIVEPANEL_DISK_ROWS + 1;
	}
	if (p->disk_top + DRIVEPANEL_DISK_ROWS > p->n_images) {
		p->disk_top = p->n_images > DRIVEPANEL_DISK_ROWS ? p->n_images - DRIVEPANEL_DISK_ROWS : 0;
	}
}

// Read the images again after renaming or deleting one: the drives'
// choices kept by name (from renamed to), the sets read again, select
// selected if it is there.
static void relist(drivepanel_t *p, const char *from, const char *to, const char *select) {
	char chosen[DRIVES][DRIVES_NAME_MAX + 1];
	for (unsigned d = 0; d < DRIVES; ++d) {
		const char *name = p->choice[d] >= 0 ? p->images[p->choice[d]] : "";
		strcpy(chosen[d], from && strcmp(name, from) == 0 ? to : name);
	}
	p->n_images = drives_list(p->images, DRIVES_LIST_MAX);
	for (unsigned d = 0; d < DRIVES; ++d) {
		p->mounted[d] = index_of(p, drives_name(d));
		p->choice[d] = chosen[d][0] ? index_of(p, chosen[d]) : -1;
	}
	read_sets(p);
	int at = index_of(p, select);
	if (at >= 0) {
		p->disk = (unsigned)at;
	}
	show_disk(p);
}

// The drive name is mounted on ("" if none), as "on C:".
static const char *mounted_on(const char *name, char text[8]) {
	text[0] = '\0';
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (drives_name(d) && strcmp(drives_name(d), name) == 0) {
			snprintf(text, 8, "on %c:", 'A' + d);
		}
	}
	return text;
}

// R or D on the selected disk: its prompt, or why not.
static void start_disk_prompt(drivepanel_t *p, drivepanel_prompt_t prompt) {
	if (p->n_images == 0) {
		p->message = "No disks on the card";
	} else if (strcmp(p->images[p->disk], DRIVES_RAMDISK) == 0) {
		p->message = prompt == DRIVEPANEL_PROMPT_RENAME ? "The RAM disk cannot be renamed"
			: "The RAM disk cannot be deleted: it is blank at power-on";
	} else {
		p->prompt = prompt;
		strcpy(p->name, prompt == DRIVEPANEL_PROMPT_RENAME ? p->images[p->disk] : "");
	}
}

static drivepanel_action_t disks_cursor_key(drivepanel_t *p, uint8_t final, uint8_t param) {
	unsigned last = p->n_images ? p->n_images - 1 : 0;
	if (final == '~') {
		final = param == 3 ? 'X' : param == 5 ? 'P' : param == 6 ? 'N' : param == 1 || param == 7 ? 'H'
			: param == 4 || param == 8 ? 'F' : 0;
	}
	switch (final) {
	case 'A':
		p->disk = p->disk ? p->disk - 1 : 0;
		break;
	case 'B':
		p->disk = p->disk < last ? p->disk + 1 : last;
		break;
	case 'P': // Page Up
		p->disk = p->disk > DRIVEPANEL_DISK_ROWS ? p->disk - DRIVEPANEL_DISK_ROWS : 0;
		break;
	case 'N': // Page Down
		p->disk = p->disk + DRIVEPANEL_DISK_ROWS < last ? p->disk + DRIVEPANEL_DISK_ROWS : last;
		break;
	case 'H':
		p->disk = 0;
		break;
	case 'F':
		p->disk = last;
		break;
	case 'X': // Delete
		start_disk_prompt(p, DRIVEPANEL_PROMPT_DELETE);
		break;
	default:
		return DRIVEPANEL_NONE;
	}
	show_disk(p);
	return DRIVEPANEL_REDRAW;
}

static bool chosen_elsewhere(const drivepanel_t *p, unsigned drive, int image) {
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (d != drive && p->choice[d] == image) {
			return true;
		}
	}
	return false;
}

// The next image (or none) for the selected drive, skipping those chosen
// for another drive.
static void step(drivepanel_t *p, int dir) {
	int n = (int)p->n_images + 1; // index + 1: 0 is none
	int at = p->choice[p->drive] + 1;
	for (int tries = 0; tries < n; ++tries) {
		at = (at + dir + n) % n;
		if (at == 0 || !chosen_elsewhere(p, p->drive, at - 1)) {
			break;
		}
	}
	p->choice[p->drive] = at - 1;
	match_set(p);
}

// The next set's images for the drives.
static void step_set(drivepanel_t *p, int dir) {
	if (p->n_sets == 0) {
		p->message = "No sets on the card yet: S saves these drives as one";
		return;
	}
	int n = (int)p->n_sets;
	int at = p->set < 0 ? (dir > 0 ? 0 : n - 1) : (p->set + dir + n) % n;
	for (unsigned d = 0; d < DRIVES; ++d) {
		p->choice[d] = p->set_choice[at][d];
	}
	p->set = at;
	if (p->set_missing[at]) {
		snprintf(p->message_buf, sizeof p->message_buf, "%u of %s's disks %s not on the card", p->set_missing[at],
			p->sets[at], p->set_missing[at] == 1 ? "is" : "are");
		p->message = p->message_buf;
	}
}

static void start_remove(drivepanel_t *p) {
	if (p->set < 0) {
		p->message = "These drives are not a set: pick one on the Set row to remove it";
		return;
	}
	p->prompt = DRIVEPANEL_PROMPT_REMOVE;
}

static drivepanel_action_t cursor_key(drivepanel_t *p, uint8_t final, uint8_t param) {
	switch (final) {
	case 'A': // the Set row, then A: to H:, round
		if (p->on_set) {
			p->on_set = false;
			p->drive = DRIVES - 1;
		} else if (p->drive == 0) {
			p->on_set = true;
		} else {
			--p->drive;
		}
		return DRIVEPANEL_REDRAW;
	case 'B':
		if (p->on_set) {
			p->on_set = false;
			p->drive = 0;
		} else if (p->drive == DRIVES - 1) {
			p->on_set = true;
		} else {
			++p->drive;
		}
		return DRIVEPANEL_REDRAW;
	case 'C':
	case 'D':
		if (p->on_set) {
			step_set(p, final == 'C' ? 1 : -1);
		} else {
			step(p, final == 'C' ? 1 : -1);
		}
		return DRIVEPANEL_REDRAW;
	case '~':
		if (param != 3) { // Delete: ESC [ 3 ~
			return DRIVEPANEL_NONE;
		}
		start_remove(p);
		return DRIVEPANEL_REDRAW;
	default:
		return DRIVEPANEL_NONE;
	}
}

// A key while a prompt is up; Escape is handled by the caller.
static drivepanel_action_t prompt_key(drivepanel_t *p, uint8_t c) {
	if (p->prompt == DRIVEPANEL_PROMPT_REMOVE) {
		p->prompt = DRIVEPANEL_PROMPT_NONE;
		return c == 'y' || c == 'Y' ? DRIVEPANEL_REMOVE_SET : DRIVEPANEL_REDRAW;
	}
	if (p->prompt == DRIVEPANEL_PROMPT_DELETE) {
		p->prompt = DRIVEPANEL_PROMPT_NONE;
		return c == 'y' || c == 'Y' ? DRIVEPANEL_DELETE_DISK : DRIVEPANEL_REDRAW;
	}
	size_t n = strlen(p->name);
	if ((c == '\r' || c == '\n') && p->prompt == DRIVEPANEL_PROMPT_RENAME) {
		if (!drives_set_name_ok(p->name)) { // the same rule as images' names (drives_normalize)
			p->message = "A disk's name is 1-8 letters and digits";
			return DRIVEPANEL_REDRAW;
		}
		p->prompt = DRIVEPANEL_PROMPT_NONE;
		return strcmp(p->name, p->images[p->disk]) == 0 ? DRIVEPANEL_REDRAW : DRIVEPANEL_RENAME_DISK;
	}
	if (c == '\r' || c == '\n') {
		if (!drives_set_name_ok(p->name)) {
			p->message = "A set's name is 1-8 letters and digits";
			return DRIVEPANEL_REDRAW;
		}
		p->prompt = DRIVEPANEL_PROMPT_NONE;
		return DRIVEPANEL_SAVE_SET;
	}
	if ((c == 0x08 || c == 0x7f) && n > 0) {
		p->name[n - 1] = '\0';
	} else if (isalnum(c) && n < DRIVES_NAME_MAX) {
		p->name[n] = (char)toupper(c);
		p->name[n + 1] = '\0';
	}
	return DRIVEPANEL_REDRAW;
}

// Escape: closes a prompt, else the panel.
static drivepanel_action_t escape(drivepanel_t *p) {
	if (p->prompt == DRIVEPANEL_PROMPT_NONE) {
		return DRIVEPANEL_CANCEL;
	}
	p->prompt = DRIVEPANEL_PROMPT_NONE;
	return DRIVEPANEL_REDRAW;
}

drivepanel_action_t drivepanel_input(drivepanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	drivepanel_action_t action = DRIVEPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			p->esc_param = 0;
			return had_message ? DRIVEPANEL_REDRAW : DRIVEPANEL_NONE;
		}
		p->esc = 0;
		return escape(p);
	}
	if (p->esc == 2) {
		if (c >= 0x40 && c <= 0x7e) {
			p->esc = 0;
			action = p->prompt      ? DRIVEPANEL_NONE
				: p->disks_page ? disks_cursor_key(p, c, p->esc_param)
				: cursor_key(p, c, p->esc_param);
		} else if (c >= '0' && c <= '9' && p->esc_param == 0) {
			p->esc_param = (uint8_t)(c - '0');
		} else if (c == ';') {
			p->esc_param = 0xff; // only the first parameter counts
		}
	} else if (c == 0x1b) {
		p->esc = 1;
		p->esc_ms = now_ms;
	} else if (p->prompt) {
		action = prompt_key(p, c);
	} else if (c == '\t') {
		p->disks_page = !p->disks_page;
		show_disk(p);
		action = DRIVEPANEL_REDRAW;
	} else if (p->disks_page) {
		if (c == 'r' || c == 'R' || c == 'd' || c == 'D') {
			start_disk_prompt(p, c == 'r' || c == 'R' ? DRIVEPANEL_PROMPT_RENAME : DRIVEPANEL_PROMPT_DELETE);
			action = DRIVEPANEL_REDRAW;
		}
	} else if (c == ' ') {
		if (p->on_set) {
			step_set(p, 1);
		} else {
			step(p, 1);
		}
		action = DRIVEPANEL_REDRAW;
	} else if (c == 's' || c == 'S') {
		p->prompt = DRIVEPANEL_PROMPT_NAME;
		strcpy(p->name, p->set >= 0 ? p->sets[p->set] : "");
		action = DRIVEPANEL_REDRAW;
	} else if (c == 'r' || c == 'R') {
		start_remove(p);
		action = DRIVEPANEL_REDRAW;
	} else if (c == '\r' || c == '\n') {
		return DRIVEPANEL_APPLY;
	}
	return action == DRIVEPANEL_NONE && had_message ? DRIVEPANEL_REDRAW : action;
}

drivepanel_action_t drivepanel_tick(drivepanel_t *p, uint32_t now_ms) {
	if (p->esc == 1 && now_ms - p->esc_ms >= DRIVEPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return escape(p);
	}
	return DRIVEPANEL_NONE;
}

unsigned drivepanel_apply(drivepanel_t *p, bool *failed) {
	unsigned changed = 0;
	*failed = false;
	drives_flush();
	// Unmount first: an image may move from one drive to another.
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (p->choice[d] != p->mounted[d]) {
			drives_unmount(d);
		}
	}
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (p->choice[d] == p->mounted[d]) {
			continue;
		}
		++changed;
		if (p->choice[d] >= 0 && drives_mount(d, p->images[p->choice[d]]) != DRIVES_OK) {
			*failed = true;
		}
	}
	if (changed) {
		drives_save_config();
	}
	return changed;
}

void drivepanel_save_set(drivepanel_t *p) {
	char images[DRIVES][DRIVES_NAME_MAX + 1];
	for (unsigned d = 0; d < DRIVES; ++d) {
		strcpy(images[d], p->choice[d] >= 0 ? p->images[p->choice[d]] : "");
	}
	bool replaced = false;
	for (unsigned i = 0; i < p->n_sets; ++i) {
		replaced |= strcmp(p->sets[i], p->name) == 0;
	}
	if (!replaced && p->n_sets == DRIVES_SETS_MAX) {
		snprintf(p->message_buf, sizeof p->message_buf, "No room: %u sets at most (R removes one)",
			DRIVES_SETS_MAX);
	} else if (!drives_set_write(p->name, images)) {
		snprintf(p->message_buf, sizeof p->message_buf, "Set %s could not be saved", p->name);
	} else {
		snprintf(p->message_buf, sizeof p->message_buf, "Saved set %s%s", p->name, replaced ? " (replaced)" : "");
		read_sets(p);
	}
	p->message = p->message_buf;
}

void drivepanel_remove_set(drivepanel_t *p) {
	if (p->set < 0) {
		return;
	}
	char name[DRIVES_NAME_MAX + 1];
	strcpy(name, p->sets[p->set]);
	snprintf(p->message_buf, sizeof p->message_buf,
		drives_set_remove(name) ? "Removed set %s" : "Set %s could not be removed", name);
	p->message = p->message_buf;
	read_sets(p);
}

static const char *disk_error(drives_result_t r) {
	switch (r) {
	case DRIVES_NOT_FOUND:
		return "is not on the card";
	case DRIVES_EXISTS:
		return "is taken";
	case DRIVES_RESERVED:
		return "is the RAM disk's";
	default:
		return "could not be changed (card error)";
	}
}

void drivepanel_rename_disk(drivepanel_t *p) {
	if (p->disk >= p->n_images) {
		return;
	}
	char from[DRIVES_NAME_MAX + 1], to[DRIVES_NAME_MAX + 1];
	strcpy(from, p->images[p->disk]);
	strcpy(to, p->name);
	drives_flush();
	drives_result_t r = drives_rename(from, to);
	if (r == DRIVES_OK) {
		snprintf(p->message_buf, sizeof p->message_buf, "Renamed %s to %s", from, to);
	} else {
		snprintf(p->message_buf, sizeof p->message_buf, "Not renamed: %s %s", r == DRIVES_EXISTS ? to : from,
			disk_error(r));
	}
	p->message = p->message_buf;
	relist(p, r == DRIVES_OK ? from : NULL, to, r == DRIVES_OK ? to : from);
}

void drivepanel_delete_disk(drivepanel_t *p) {
	if (p->disk >= p->n_images) {
		return;
	}
	char name[DRIVES_NAME_MAX + 1];
	strcpy(name, p->images[p->disk]);
	drives_flush();
	drives_result_t r = drives_remove(name);
	if (r == DRIVES_OK) {
		snprintf(p->message_buf, sizeof p->message_buf, "Deleted %s", name);
	} else {
		snprintf(p->message_buf, sizeof p->message_buf, "Not deleted: %s %s", name, disk_error(r));
	}
	p->message = p->message_buf;
	relist(p, NULL, NULL, NULL);
}

// One row: the marker and label, the choice in reverse, then info.
static void draw_row(screen_t *s, unsigned row, const char *label, bool selected, const char *choice,
	const char *info, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', label);
	for (unsigned c = 0; text[c]; ++c) {
		screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
	}
	snprintf(text, sizeof text, "< %-8s >", choice);
	screen_puts(s, NAME_COL, row, text, bg, fg);
	snprintf(text, sizeof text, " %s", info);
	screen_puts(s, NAME_COL + 12, row, text, fg, bg);
}

// The Disks page: a heading, then a window of the list.
static void draw_disks(const drivepanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	if (p->n_images == 0) {
		snprintf(text, sizeof text, "   No disks on the card: the monitor's I installs the built-in ones");
	} else {
		snprintf(text, sizeof text, "   %u disk%s in /DISKS80%s", p->n_images, p->n_images == 1 ? "" : "s",
			p->n_images > DRIVEPANEL_DISK_ROWS ? " (Up/Down, Page Up/Down scroll)" : "");
	}
	screen_puts(s, LEFT, top + 1, text, fg, bg);
	// ^ and v beside the first and last rows: more above or below.
	for (unsigned r = 0; r < DRIVEPANEL_DISK_ROWS && p->disk_top + r < p->n_images; ++r) {
		unsigned i = p->disk_top + r;
		const char *name = p->images[i];
		const drives_geometry_t *g = drives_geometry(drives_image_format(name));
		char where[8], format[32], info[SCREEN_MAX_COLS + 1];
		mounted_on(name, where);
		snprintf(format, sizeof format, "%s%s", g ? g->name : "not a disk image",
			strcmp(name, DRIVES_RAMDISK) == 0 ? " in PSRAM" : "");
		snprintf(info, sizeof info, "%-20s%s%s", format, where[0] ? "mounted " : "", where);
		bool more = (r == 0 && p->disk_top > 0) || (r == DRIVEPANEL_DISK_ROWS - 1 && i + 1 < p->n_images);
		char label[3] = {more ? (r == 0 ? '^' : 'v') : ' ', ' ', '\0'};
		draw_row(s, top + 2 + r, label, i == p->disk, name, info, fg, bg);
	}
}

void drivepanel_draw(const drivepanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	if (p->disks_page) {
		screen_frame(s, top, DRIVEPANEL_ROWS, "SET-UP  Disks on the card  (Tab: the drives)", fg, bg, border);
		draw_disks(p, s, top, fg, bg);
		unsigned msg_row = top + DRIVEPANEL_ROWS - 4;
		char text[SCREEN_MAX_COLS + 1];
		if (p->prompt && p->message) { // why the typed name was refused
			screen_puts(s, LEFT + 3, msg_row - 1, p->message, fg, bg);
		}
		if (p->prompt == DRIVEPANEL_PROMPT_RENAME) {
			snprintf(text, sizeof text, "Rename %s to: %s_  (Return renames, Esc cancels)", p->images[p->disk],
				p->name);
			screen_puts(s, LEFT + 3, msg_row, text, fg, bg);
		} else if (p->prompt == DRIVEPANEL_PROMPT_DELETE) {
			char where[8];
			mounted_on(p->images[p->disk], where);
			snprintf(text, sizeof text, "Delete %s%s%s and all on it? (Y/N)", p->images[p->disk],
				where[0] ? ", mounted " : "", where);
			screen_puts(s, LEFT + 3, msg_row, text, fg, bg);
		} else if (p->message) {
			screen_puts(s, LEFT + 3, msg_row, p->message, fg, bg);
		}
		screen_puts(s, LEFT, top + DRIVEPANEL_ROWS - 3,
			" Up/Down/PgUp/PgDn disk   R rename   D/Delete delete   Tab drives   Esc back", fg, bg);
		screen_puts(s, LEFT, top + DRIVEPANEL_ROWS - 2,
			" A renamed disk stays mounted; the sets naming it follow", fg, bg);
		return;
	}
	screen_frame(s, top, DRIVEPANEL_ROWS, "SET-UP  Drives  (Tab: the disks; CP/M 2.2 sees a change after ^C)", fg, bg,
		border);
	draw_row(s, top + 1, "Set", p->on_set, p->set >= 0 ? p->sets[p->set] : "-",
		p->n_sets == 0 ? "no sets on the card yet"
		: p->set < 0  ? "the drives below are no set"
		: "",
		fg, bg);
	for (unsigned d = 0; d < DRIVES; ++d) {
		char label[3] = {(char)('A' + d), ':', '\0'};
		char info[SCREEN_MAX_COLS + 1];
		int c = p->choice[d];
		const char *format = "", *where = "";
		if (c >= 0) {
			const drives_geometry_t *g = drives_geometry(drives_image_format(p->images[c]));
			format = g ? g->name : "not a disk image";
			if (strcmp(p->images[c], DRIVES_RAMDISK) == 0) {
				where = " in PSRAM, lost at power-off";
			}
		}
		snprintf(info, sizeof info, "%s%s%s", format, where, c != p->mounted[d] ? "  (changed)" : "");
		draw_row(s, top + 2 + d, label, !p->on_set && d == p->drive, c >= 0 ? p->images[c] : "none", info, fg, bg);
	}
	unsigned msg_row = top + DRIVEPANEL_ROWS - 4;
	char text[SCREEN_MAX_COLS + 1];
	if (p->prompt && p->message) { // why the typed name was refused
		screen_puts(s, LEFT + 3, msg_row - 1, p->message, fg, bg);
	}
	if (p->prompt == DRIVEPANEL_PROMPT_NAME) {
		snprintf(text, sizeof text, "Save these drives as set: %s_  (Return saves, Esc cancels)", p->name);
		screen_puts(s, LEFT + 3, msg_row, text, fg, bg);
	} else if (p->prompt == DRIVEPANEL_PROMPT_REMOVE) {
		snprintf(text, sizeof text, "Remove set %s from the card? (Y/N)", p->sets[p->set]);
		screen_puts(s, LEFT + 3, msg_row, text, fg, bg);
	} else if (p->message) {
		screen_puts(s, LEFT + 3, msg_row, p->message, fg, bg);
	}
	screen_puts(s, LEFT, top + DRIVEPANEL_ROWS - 3,
		" Up/Down row   Left/Right/Space disk or set   Return mount   Esc back", fg, bg);
	screen_puts(s, LEFT, top + DRIVEPANEL_ROWS - 2,
		" S save these drives as a set   R/Delete remove the set shown   Tab disks", fg, bg);
}
