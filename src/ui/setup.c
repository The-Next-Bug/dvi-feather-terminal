#include "setup.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "palette.h"

#define NAME_WIDTH 18
// The panel's text starts inside its box (screen_frame).
#define LEFT 1
// Settings with more choices than this show only the current one, as
// "< value >", so the row fits in 80 columns.
#define INLINE_CHOICES 4

// Index of the item's current value (the first choice if it is none).
static unsigned choice_index(settings_t *v, unsigned item) {
	const settings_field_t *it = &settings_fields[item];
	uint8_t value = *settings_field(v, item);
	for (unsigned i = 0; i < it->n_choices; ++i)
		if (it->values[i] == value)
			return i;
	return 0;
}

static void change(setup_t *u, int step) {
	if (settings_field_fixed(&u->values, u->item))
		return;
	const settings_field_t *it = &settings_fields[u->item];
	unsigned i = (choice_index(&u->values, u->item) + it->n_choices + (unsigned)step) % it->n_choices;
	*settings_field(&u->values, u->item) = it->values[i];
}

// Select the next setting in direction step (1 or -1) that can be changed.
static void select_next(setup_t *u, int step) {
	for (unsigned n = 0; n < SETUP_ITEMS; ++n) {
		u->item = (u->item + SETUP_ITEMS + (unsigned)step) % SETUP_ITEMS;
		if (!settings_field_fixed(&u->values, u->item))
			return;
	}
}

void setup_open(setup_t *u, const settings_t *values) {
	*u = (setup_t){.values = *values};
	if (settings_field_fixed(&u->values, u->item))
		select_next(u, 1);
}

// The final byte of a cursor key sequence (ESC [ x or ESC O x).
static setup_action_t cursor_key(setup_t *u, uint8_t final) {
	switch (final) {
	case 'A':
		select_next(u, -1);
		return SETUP_REDRAW;
	case 'B':
		select_next(u, 1);
		return SETUP_REDRAW;
	case 'C':
		change(u, 1);
		return SETUP_REDRAW;
	case 'D':
		change(u, -1);
		return SETUP_REDRAW;
	default:
		return SETUP_NONE;
	}
}

setup_action_t setup_input(setup_t *u, uint8_t c, uint32_t now_ms) {
	bool had_message = u->message != NULL;
	u->message = NULL;
	setup_action_t action = SETUP_NONE;

	if (u->esc == 1) {
		if (c == '[' || c == 'O') {
			u->esc = 2;
			return had_message ? SETUP_REDRAW : SETUP_NONE;
		}
		// ESC and something else: the Escape key (or Alt+key).
		u->esc = 0;
		return SETUP_CANCEL;
	}
	if (u->esc == 2) {
		// Parameters (e.g. ESC [ 2 ~) are skipped up to the final byte.
		if (c >= 0x40 && c <= 0x7e) {
			u->esc = 0;
			action = cursor_key(u, c);
		}
		return action == SETUP_NONE && had_message ? SETUP_REDRAW : action;
	}

	switch (c) {
	case 0x1b:
		u->esc = 1;
		u->esc_ms = now_ms;
		break;
	case ' ':
		change(u, 1);
		action = SETUP_REDRAW;
		break;
	case '\r':
	case '\n':
		return SETUP_SAVE;
	case 'm':
	case 'M':
		if (u->machine_menu)
			return SETUP_MACHINE;
		break;
	case 'd':
	case 'D':
		if (u->machine_menu)
			return SETUP_DRIVES;
		break;
	case 't':
	case 'T':
		return SETUP_DEMO;
	case 'r':
	case 'R':
		return SETUP_REBOOT; // in both modes: the last resort
	case 'u':
	case 'U':
		return SETUP_FIRMWARE;
	default:
		break;
	}
	return action == SETUP_NONE && had_message ? SETUP_REDRAW : action;
}

setup_action_t setup_tick(setup_t *u, uint32_t now_ms) {
	if (u->esc == 1 && now_ms - u->esc_ms >= SETUP_ESC_TIMEOUT_MS) {
		u->esc = 0;
		return SETUP_CANCEL;
	}
	return SETUP_NONE;
}

// The preview rows: samples of default text, bold and the status line,
// then each ANSI colour as text on the default background and, under
// those, as a background. They are palette indices like everything else,
// so they show the palette on screen: the owner shows values' palette
// while SET-UP is open (only the status line's indices follow values
// here). Spaced out (once so that no 8-pixel group at 132 columns held two
// samples, for the PicoDVI encoder).
static void draw_colours_preview(screen_t *s, unsigned row, const settings_t *values) {
	settings_colours_t c = settings_colours(values);
	const uint8_t fg = PALETTE_TEXT, bg = PALETTE_BACKGROUND;
	unsigned col = LEFT + 3 + NAME_WIDTH;
	screen_puts(s, col, row, " Text ", fg, bg);
	screen_puts(s, col, row + 1, "      ", fg, bg);
	col += 6;
	for (const char *p = "Bold "; *p; ++p, ++col) {
		screen_put_attr(s, col, row, (uint8_t)*p, PALETTE_BOLD_TEXT, bg, SCREEN_ATTR_BOLD);
		screen_put(s, col, row + 1, ' ', fg, bg);
	}
	screen_puts(s, col, row, " Status ", c.status_fg, c.status_bg);
	screen_puts(s, col, row + 1, "        ", fg, bg);
	col += 8;
	for (unsigned i = 0; i < 16; ++i, col += 2) {
		// On the dark backgrounds the digit is bright, on the others black.
		uint8_t back_fg = i == PALETTE_BLACK || i == PALETTE_BLUE || i == PALETTE_DIM ? PALETTE_BOLD_TEXT
			: PALETTE_BLACK;
		uint8_t digit = (uint8_t)"0123456789ABCDEF"[i];
		screen_put(s, col, row, ' ', fg, bg);
		screen_put(s, col, row + 1, ' ', fg, bg);
		screen_put(s, col + 1, row, digit, (uint8_t)i, bg);
		screen_put(s, col + 1, row + 1, digit, back_fg, (uint8_t)i);
	}
	screen_put(s, col, row, ' ', fg, bg);
	screen_put(s, col, row + 1, ' ', fg, bg);
}

void setup_draw(const setup_t *u, screen_t *s, unsigned top, const char *title, uint8_t fg, uint8_t bg,
	uint8_t border) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, "SET-UP  %s", title);
	screen_frame(s, top, SETUP_ROWS, text, fg, bg, border);

	settings_t values = u->values;
	// The preview goes under the last colour setting.
	const unsigned preview_after = offsetof(settings_t, palette);
	unsigned row = top;
	for (unsigned i = 0; i < SETUP_ITEMS; ++i) {
		const settings_field_t *it = &settings_fields[i];
		++row;
		if (i == preview_after + 1)
			row += 2; // below the preview
		// The selected setting: marked, and its name in bold.
		bool selected = i == u->item;
		snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', it->name);
		for (unsigned c = 0; text[c]; ++c)
			screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
		unsigned col = LEFT + 3 + NAME_WIDTH;
		const char *fixed = settings_field_fixed(&values, i);
		if (fixed) {
			// Only the value in use (Columns is the one field that can be
			// fixed), then what decides it.
			settings_t used = values;
			used.columns = (uint8_t)settings_columns(&values);
			const char *label = it->labels[choice_index(&used, i)];
			snprintf(text, sizeof text, " %s ", label);
			screen_puts(s, col, row, text, bg, fg);
			snprintf(text, sizeof text, "(%s)", fixed);
			screen_puts(s, col + (unsigned)strlen(label) + 3, row, text, fg, bg);
			continue;
		}
		unsigned current = choice_index(&values, i);
		if (it->n_choices > INLINE_CHOICES) {
			snprintf(text, sizeof text, "< %s >", it->labels[current]);
			screen_puts(s, col + 1, row, text, bg, fg);
			continue;
		}
		// Every choice, the current one reversed.
		for (unsigned c = 0; c < it->n_choices; ++c) {
			snprintf(text, sizeof text, " %s ", it->labels[c]);
			bool on = c == current;
			screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
			col += (unsigned)strlen(it->labels[c]) + 3;
		}
	}
	draw_colours_preview(s, top + 2 + preview_after, &values);

	if (u->message)
		screen_puts(s, LEFT + 3, top + SETUP_ROWS - 4, u->message, fg, bg);
	screen_puts(s, LEFT, top + SETUP_ROWS - 3, " Up/Down select  Left/Right change  Return save  Esc cancel",
		fg, bg);
	screen_puts(s, LEFT, top + SETUP_ROWS - 2, u->machine_menu
		? " T demo  U firmware update  R reboot  D drives  M monitor"
		: " T demo  U firmware update  R reboot", fg, bg);
}
