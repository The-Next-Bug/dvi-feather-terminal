#include "telnetpanel.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_WIDTH 16
#define VALUE_COL (LEFT + 3 + NAME_WIDTH)

#define MODE_FIELD offsetof(settings_t, telnet)

void telnetpanel_open(telnetpanel_t *p, const settings_t *values, unsigned line) {
	memset(p, 0, sizeof *p);
	p->values = *values;
	p->line = line < TELNETPANEL_LINES ? line : TELNETPANEL_MODE;
}

static void change(telnetpanel_t *p, int step) {
	if (p->line != TELNETPANEL_MODE) {
		return;
	}
	const settings_field_t *it = &settings_fields[MODE_FIELD];
	uint8_t *value = settings_field(&p->values, MODE_FIELD);
	unsigned i = 0;
	while (i < it->n_choices && it->values[i] != *value) {
		++i;
	}
	i = i < it->n_choices ? (i + it->n_choices + (unsigned)step) % it->n_choices : 0;
	*value = it->values[i];
}

static void select_line(telnetpanel_t *p, int dir) {
	p->line = (p->line + TELNETPANEL_LINES + (unsigned)dir) % TELNETPANEL_LINES;
}

static telnetpanel_action_t cursor_key(telnetpanel_t *p, uint8_t final) {
	switch (final) {
	case 'A':
		select_line(p, -1);
		return TELNETPANEL_REDRAW;
	case 'B':
		select_line(p, 1);
		return TELNETPANEL_REDRAW;
	case 'C':
	case 'D':
		change(p, final == 'C' ? 1 : -1);
		return TELNETPANEL_REDRAW;
	default:
		return TELNETPANEL_NONE;
	}
}

// A key on a text row: printable characters but space added at the end,
// Backspace takes the last off.
static telnetpanel_action_t text_key(telnetpanel_t *p, uint8_t c) {
	bool host = p->line == TELNETPANEL_HOST;
	char *t = host ? p->values.telnet_host : p->values.telnet_type;
	size_t max = host ? SETTINGS_HOST_MAX : SETTINGS_TYPE_MAX;
	size_t n = strlen(t);
	if (c == 0x08 || c == 0x7f) {
		if (n) {
			t[n - 1] = '\0'; // NUL-padded still
			return TELNETPANEL_REDRAW;
		}
		return TELNETPANEL_NONE;
	}
	if (c > 0x20 && c < 0x7f) {
		if (n < max) {
			t[n] = (char)c;
			return TELNETPANEL_REDRAW;
		}
		p->message = host ? "The host is at its longest" : "The terminal type is at its longest";
		return TELNETPANEL_REDRAW;
	}
	return TELNETPANEL_NONE;
}

static telnetpanel_action_t enter(telnetpanel_t *p) {
	if (p->line == TELNETPANEL_HOST) {
		if (!p->values.telnet_host[0]) {
			p->message = "Type the host to dial: name or address, :port if not 23";
			return TELNETPANEL_REDRAW;
		}
		return TELNETPANEL_DIAL;
	}
	if (p->line == TELNETPANEL_CALL) {
		if (!p->call) {
			p->message = "No call: dial from the Host row";
			return TELNETPANEL_REDRAW;
		}
		return TELNETPANEL_HANGUP;
	}
	return TELNETPANEL_APPLY;
}

telnetpanel_action_t telnetpanel_input(telnetpanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	telnetpanel_action_t action = TELNETPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			return had_message ? TELNETPANEL_REDRAW : TELNETPANEL_NONE;
		}
		p->esc = 0;
		return TELNETPANEL_CANCEL; // ESC and something else: the Escape key (or Alt+key)
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
	} else if (p->line == TELNETPANEL_TYPE || p->line == TELNETPANEL_HOST) {
		action = text_key(p, c);
	} else if (c == ' ') {
		change(p, 1);
		action = TELNETPANEL_REDRAW;
	}
	return action == TELNETPANEL_NONE && had_message ? TELNETPANEL_REDRAW : action;
}

telnetpanel_action_t telnetpanel_tick(telnetpanel_t *p, uint32_t now_ms) {
	if (p->esc == 1 && now_ms - p->esc_ms >= TELNETPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return TELNETPANEL_CANCEL;
	}
	return TELNETPANEL_NONE;
}

// A row's name, the selected one marked and in bold.
static void draw_name(screen_t *s, unsigned row, const char *name, bool selected, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', name);
	for (unsigned c = 0; text[c]; ++c) {
		screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
	}
}

// A text row: the text, or what stands for none, a block cursor while
// selected.
static void draw_text(const telnetpanel_t *p, screen_t *s, unsigned row, unsigned line, const char *name,
	const char *text, const char *none, uint8_t fg, uint8_t bg) {
	bool on = p->line == line;
	draw_name(s, row, name, on, fg, bg);
	screen_puts(s, VALUE_COL + 1, row, text[0] || on ? text : none, fg, bg);
	if (on) {
		screen_put(s, VALUE_COL + 1 + (unsigned)strlen(text), row, ' ', bg, fg);
	}
}

void telnetpanel_draw(const telnetpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, TELNETPANEL_ROWS, "SET-UP  Telnet (through a Zimodem)", fg, bg, border);
	char text[SCREEN_MAX_COLS + 1];

	unsigned row = top + 1;
	const settings_field_t *it = &settings_fields[MODE_FIELD];
	draw_name(s, row, it->name, p->line == TELNETPANEL_MODE, fg, bg);
	unsigned col = VALUE_COL;
	for (unsigned c = 0; c < it->n_choices; ++c) {
		snprintf(text, sizeof text, " %s ", it->labels[c]);
		bool on = it->values[c] == p->values.telnet;
		screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
		col += (unsigned)strlen(text);
	}

	char by_type[48];
	snprintf(by_type, sizeof by_type, "%s (Terminal type's)", p->type_default ? p->type_default : "VT100");
	draw_text(p, s, top + 2, TELNETPANEL_TYPE, "Terminal type", p->values.telnet_type, by_type, fg, bg);
	draw_text(p, s, top + 3, TELNETPANEL_HOST, "Host", p->values.telnet_host, "(none)", fg, bg);
	draw_name(s, top + 4, "Call", p->line == TELNETPANEL_CALL, fg, bg);
	if (p->call) {
		snprintf(text, sizeof text, "%.24s (Return hangs up: +++, ATH)", p->call);
	} else {
		snprintf(text, sizeof text, "none");
	}
	screen_puts(s, VALUE_COL + 1, top + 4, text, fg, bg);

	screen_puts(s, LEFT, top + 6, "   Return on Host: ATD\"host\" from the modem's AT prompt, raw TCP,", fg, bg);
	screen_puts(s, LEFT, top + 7, "   telnet on for the call (window size, terminal type). F11 comes here.", fg, bg);
	screen_puts(s, LEFT, top + 8,
		p->values.telnet == SETTINGS_TELNET_AUTO ? "   Auto: telnet once the host negotiates; a plain host is unchanged"
		: p->values.telnet == SETTINGS_TELNET_ON ? "   On: every 0xFF from the host starts a telnet command"
		: "   Off: the host's bytes as they come, except during a call dialed here", fg, bg);
	if (p->message) {
		screen_puts(s, LEFT + 3, top + TELNETPANEL_ROWS - 4, p->message, fg, bg);
	}
	screen_puts(s, LEFT, top + TELNETPANEL_ROWS - 3,
		" Up/Down row   Left/Right/Space choice   Type, Host: type, Backspace", fg, bg);
	screen_puts(s, LEFT, top + TELNETPANEL_ROWS - 2,
		" Return keep (Host: save and dial; Call: hang up)   Esc back", fg, bg);
}
