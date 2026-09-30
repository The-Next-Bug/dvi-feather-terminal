#include "setup.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "term.h" // term_ansi_colour

#define NAME_WIDTH 18
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
	const settings_field_t *it = &settings_fields[u->item];
	unsigned i = (choice_index(&u->values, u->item) + it->n_choices + (unsigned)step) % it->n_choices;
	*settings_field(&u->values, u->item) = it->values[i];
}

void setup_open(setup_t *u, const settings_t *values) {
	*u = (setup_t){.values = *values};
}

// The final byte of a cursor key sequence (ESC [ x or ESC O x).
static setup_action_t cursor_key(setup_t *u, uint8_t final) {
	switch (final) {
	case 'A':
		u->item = (u->item + SETUP_ITEMS - 1) % SETUP_ITEMS;
		return SETUP_REDRAW;
	case 'B':
		u->item = (u->item + 1) % SETUP_ITEMS;
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

// The preview rows: samples in the colours of values' Colors and Palette.
// The first shows default text, bold and the status line, then each ANSI
// colour as text on the default background; the second, under those, each
// as a background under default text. Spaced out, so no 8-pixel group (at
// 132 columns) holds two samples.
static void draw_colours_preview(screen_t *s, unsigned row, const settings_t *values) {
	settings_colours_t c = settings_colours(values);
	unsigned col = 3 + NAME_WIDTH;
	screen_puts(s, col, row, " Text ", c.fg, c.bg);
	screen_puts(s, col, row + 1, "      ", c.fg, c.bg);
	col += 6;
	for (const char *p = "Bold "; *p; ++p, ++col) {
		screen_put_attr(s, col, row, (uint8_t)*p, c.bold_fg, c.bg, SCREEN_ATTR_BOLD);
		screen_put(s, col, row + 1, ' ', c.fg, c.bg);
	}
	screen_puts(s, col, row, " Status ", c.status_fg, c.status_bg);
	screen_puts(s, col, row + 1, "        ", c.fg, c.bg);
	col += 8;
	for (unsigned i = 0; i < 16; ++i, col += 2) {
		uint8_t text_fg = term_ansi_colour(i), text_bg = c.bg; // i as text
		uint8_t back_fg = c.fg, back_bg = term_ansi_colour(i);  // i as background
		if (values->palette) {
			const term_colours_t d = {c.fg, c.bold_fg, c.bg};
			term_mono_colours(&d, (uint8_t)i, TERM_DEFAULT_COLOUR, false, &text_fg, &text_bg);
			term_mono_colours(&d, TERM_DEFAULT_COLOUR, (uint8_t)i, false, &back_fg, &back_bg);
		}
		uint8_t digit = (uint8_t)"0123456789ABCDEF"[i];
		screen_put(s, col, row, ' ', c.fg, c.bg);
		screen_put(s, col, row + 1, ' ', c.fg, c.bg);
		screen_put(s, col + 1, row, digit, text_fg, text_bg);
		screen_put(s, col + 1, row + 1, digit, back_fg, back_bg);
	}
	screen_put(s, col, row, ' ', c.fg, c.bg);
	screen_put(s, col, row + 1, ' ', c.fg, c.bg);
}

void setup_draw(const setup_t *u, screen_t *s, unsigned top, const char *title, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	for (unsigned r = 0; r < SETUP_ROWS; ++r)
		screen_clear_line(s, top + r, fg, bg);

	snprintf(text, sizeof text, " SET-UP  %s", title);
	screen_puts(s, 0, top, text, fg, bg);

	settings_t values = u->values;
	// The preview goes under the last colour setting.
	const unsigned preview_after = offsetof(settings_t, palette);
	unsigned row = top;
	for (unsigned i = 0; i < SETUP_ITEMS; ++i) {
		const settings_field_t *it = &settings_fields[i];
		++row;
		if (i == preview_after + 1)
			row += 2; // below the preview
		snprintf(text, sizeof text, " %c %s", i == u->item ? '>' : ' ', it->name);
		screen_puts(s, 0, row, text, fg, bg);
		unsigned col = 3 + NAME_WIDTH;
		unsigned current = choice_index(&values, i);
		if (it->n_choices > INLINE_CHOICES) {
			snprintf(text, sizeof text, "< %s >", it->labels[current]);
			screen_puts(s, col + 1, row, text, bg, fg);
			continue;
		}
		for (unsigned c = 0; c < it->n_choices; ++c) {
			snprintf(text, sizeof text, " %s ", it->labels[c]);
			bool on = c == current;
			screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
			col += (unsigned)strlen(it->labels[c]) + 3;
		}
	}
	draw_colours_preview(s, top + 2 + preview_after, &values);

	if (u->message)
		screen_puts(s, 3, top + SETUP_ROWS - 2, u->message, fg, bg);
	screen_puts(s, 0, top + SETUP_ROWS - 1,
		" Up/Down select   Left/Right/Space change   Return save   Esc cancel", fg, bg);
}
