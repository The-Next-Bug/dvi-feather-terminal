#include "modempanel.h"

#if WIFI_MODEM

#include <stdio.h>
#include <string.h>

// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_WIDTH 14
#define VALUE_COL (LEFT + 3 + NAME_WIDTH)

void modempanel_open(modempanel_t *p, const settings_t *values) {
	memset(p, 0, sizeof *p);
	p->values = *values;
}

static modempanel_action_t cursor_key(modempanel_t *p, uint8_t final) {
	if (final == 'A' || final == 'B') {
		p->line = (p->line + MODEMPANEL_LINES + (final == 'A' ? -1u : 1u)) % MODEMPANEL_LINES;
		p->confirm = false;
		return MODEMPANEL_REDRAW;
	}
	return MODEMPANEL_NONE;
}

// A key on Network or Password: printable characters (spaces too) added at
// the end, Backspace takes the last off.
static modempanel_action_t text_key(modempanel_t *p, uint8_t c) {
	bool ssid = p->line == MODEMPANEL_NETWORK;
	char *t = ssid ? p->values.wifi_ssid : p->values.wifi_password;
	size_t max = ssid ? SETTINGS_SSID_MAX : SETTINGS_PASSWORD_MAX;
	size_t n = strlen(t);
	if (c == 0x08 || c == 0x7f) {
		if (n) {
			t[n - 1] = '\0'; // NUL-padded still
			return MODEMPANEL_REDRAW;
		}
		return MODEMPANEL_NONE;
	}
	if (c >= 0x20 && c < 0x7f) {
		if (n < max) {
			t[n] = (char)c;
			return MODEMPANEL_REDRAW;
		}
		p->message = ssid ? "The network's name is at its longest" : "The password is at its longest";
		return MODEMPANEL_REDRAW;
	}
	return MODEMPANEL_NONE;
}

static modempanel_action_t enter(modempanel_t *p) {
	switch (p->line) {
	case MODEMPANEL_RESET_LINE:
		return MODEMPANEL_RESET;
	case MODEMPANEL_DEFAULTS_LINE:
		if (!p->confirm) {
			p->confirm = true;
			p->message = "Return again: the module's factory settings, then ours";
			return MODEMPANEL_REDRAW;
		}
		p->confirm = false;
		return MODEMPANEL_DEFAULTS;
	default:
		return MODEMPANEL_JOIN;
	}
}

modempanel_action_t modempanel_input(modempanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	modempanel_action_t action = MODEMPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			return had_message ? MODEMPANEL_REDRAW : MODEMPANEL_NONE;
		}
		p->esc = 0;
		return MODEMPANEL_CANCEL; // ESC and something else: the Escape key (or Alt+key)
	}
	if (p->esc == 2) {
		// Parameters (e.g. ESC [ 3 ~) are skipped up to the final byte.
		if (c >= 0x40 && c <= 0x7e) {
			p->esc = 0;
			action = cursor_key(p, c);
		}
	} else if (c == 0x1b) {
		p->esc = 1;
		p->esc_ms = now_ms;
	} else if (c == '\r' || c == '\n') {
		return enter(p);
	} else if (p->line == MODEMPANEL_NETWORK || p->line == MODEMPANEL_PASSWORD) {
		action = text_key(p, c);
	}
	if (p->confirm && p->message == NULL && action != MODEMPANEL_NONE) {
		p->confirm = false;
	}
	return action == MODEMPANEL_NONE && had_message ? MODEMPANEL_REDRAW : action;
}

modempanel_action_t modempanel_tick(modempanel_t *p, uint32_t now_ms) {
	if (p->esc == 1 && now_ms - p->esc_ms >= MODEMPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return MODEMPANEL_CANCEL;
	}
	return MODEMPANEL_NONE;
}

// A row's name, the selected one marked and in bold.
static void draw_name(screen_t *s, unsigned row, const char *name, bool selected, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', name);
	for (unsigned c = 0; text[c]; ++c) {
		screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
	}
}

static const char *or_none(const char *t, const char *none) {
	return t && t[0] ? t : none;
}

void modempanel_draw(const modempanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, MODEMPANEL_ROWS, "SET-UP  WiFi modem", fg, bg, border);
	char text[SCREEN_MAX_COLS + 1];

	// Network and Password: the text, a block cursor while selected; the
	// password as stars.
	const char *ssid = p->values.wifi_ssid;
	bool on = p->line == MODEMPANEL_NETWORK;
	draw_name(s, top + 1, "Network", on, fg, bg);
	screen_puts(s, VALUE_COL + 1, top + 1, ssid[0] || on ? ssid : "(the module's own)", fg, bg);
	if (on) {
		screen_put(s, VALUE_COL + 1 + (unsigned)strlen(ssid), top + 1, ' ', bg, fg);
	}
	on = p->line == MODEMPANEL_PASSWORD;
	size_t n = strlen(p->values.wifi_password);
	memset(text, '*', n);
	text[n] = '\0';
	draw_name(s, top + 2, "Password", on, fg, bg);
	screen_puts(s, VALUE_COL + 1, top + 2, n || on ? text : "(none)", fg, bg);
	if (on) {
		screen_put(s, VALUE_COL + 1 + (unsigned)n, top + 2, ' ', bg, fg);
	}
	draw_name(s, top + 3, "Address", false, fg, bg);
	screen_puts(s, VALUE_COL + 1, top + 3, "DHCP", fg, bg);
	draw_name(s, top + 4, "Reset modem", p->line == MODEMPANEL_RESET_LINE, fg, bg);
	screen_puts(s, VALUE_COL + 1, top + 4, "Return: reset it and set it up again", fg, bg);
	draw_name(s, top + 5, "Defaults", p->line == MODEMPANEL_DEFAULTS_LINE, fg, bg);
	screen_puts(s, VALUE_COL + 1, top + 5, "Return twice: its factory settings, then ours", fg, bg);

	// The modem as it is.
	const modempanel_status_t *st = &p->status;
	if (st->busy) {
		snprintf(text, sizeof text, "   Status    %s...", or_none(st->state, "starting"));
	} else if (st->joined) {
		snprintf(text, sizeof text, "   Status    %s, on %.32s, %s", or_none(st->state, "ready"), or_none(st->ssid, "?"),
			or_none(st->ip, "?"));
	} else {
		snprintf(text, sizeof text, "   Status    %s, no network%s%.32s", or_none(st->state, "?"),
			st->ssid && st->ssid[0] ? ": set to " : "", st->ssid ? st->ssid : "");
	}
	screen_puts(s, LEFT, top + 7, text, fg, bg);
	snprintf(text, sizeof text, "   Firmware  %.24s   boot: %.32s", or_none(st->version, "?"), or_none(st->rom, "?"));
	screen_puts(s, LEFT, top + 8, text, fg, bg);
	snprintf(text, sizeof text, "   Problem   %.60s", or_none(st->problem, "none"));
	screen_puts(s, LEFT, top + 9, text, fg, bg);

	if (p->message) {
		screen_puts(s, LEFT + 3, top + MODEMPANEL_ROWS - 4, p->message, fg, bg);
	}
	screen_puts(s, LEFT, top + MODEMPANEL_ROWS - 3, " Up/Down row   Network, Password: type, Backspace", fg, bg);
	screen_puts(s, LEFT, top + MODEMPANEL_ROWS - 2,
		" Return: Network, Password save and join; Reset, Defaults as shown   Esc back", fg, bg);
}

#endif
