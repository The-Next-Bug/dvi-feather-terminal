#include "setup.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_CHOICES 4

// A setting: a byte of settings_t and the values it can take.
typedef struct {
	const char *name;
	size_t offset;
	uint8_t n_choices;
	uint8_t choices[MAX_CHOICES];
	const char *labels[MAX_CHOICES];
} item_t;

static const item_t items[SETUP_ITEMS] = {
	{"Columns", offsetof(settings_t, columns), 2, {80, 132}, {"80", "132"}},
	{"Backspace sends", offsetof(settings_t, backspace), 2, {0x7f, 0x08}, {"DEL", "BS"}},
	{"Local echo", offsetof(settings_t, local_echo), 2, {0, 1}, {"off", "on"}},
};

#define NAME_WIDTH 16

static uint8_t *field(settings_t *v, const item_t *it) {
	return (uint8_t *)v + it->offset;
}

// Index of the item's current value (the first choice if it is none).
static unsigned choice_index(const settings_t *v, const item_t *it) {
	uint8_t value = ((const uint8_t *)v)[it->offset];
	for (unsigned i = 0; i < it->n_choices; ++i)
		if (it->choices[i] == value)
			return i;
	return 0;
}

static void change(setup_t *u, int step) {
	const item_t *it = &items[u->item];
	unsigned i = (choice_index(&u->values, it) + it->n_choices + (unsigned)step) % it->n_choices;
	*field(&u->values, it) = it->choices[i];
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

void setup_draw(const setup_t *u, screen_t *s, unsigned top, const char *title, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	for (unsigned r = 0; r < SETUP_ROWS; ++r)
		screen_clear_line(s, top + r, fg, bg);

	snprintf(text, sizeof text, " SET-UP  %s", title);
	screen_puts(s, 0, top, text, fg, bg);

	for (unsigned i = 0; i < SETUP_ITEMS; ++i) {
		const item_t *it = &items[i];
		unsigned row = top + 1 + i;
		snprintf(text, sizeof text, " %c %s", i == u->item ? '>' : ' ', it->name);
		screen_puts(s, 0, row, text, fg, bg);
		unsigned col = 3 + NAME_WIDTH;
		unsigned current = choice_index(&u->values, it);
		for (unsigned c = 0; c < it->n_choices; ++c) {
			snprintf(text, sizeof text, " %s ", it->labels[c]);
			bool on = c == current;
			screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
			col += (unsigned)strlen(it->labels[c]) + 3;
		}
	}

	if (u->message)
		screen_puts(s, 3, top + 1 + SETUP_ITEMS, u->message, fg, bg);
	screen_puts(s, 0, top + SETUP_ROWS - 1,
		" Up/Down select   Left/Right/Space change   Return save   Esc cancel", fg, bg);
}
