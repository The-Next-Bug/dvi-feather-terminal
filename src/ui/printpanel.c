#include "printpanel.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_WIDTH 16
#define VALUE_COL (LEFT + 3 + NAME_WIDTH)

// The settings field of a line before the Target.
static unsigned field_of(unsigned line) {
	static const uint8_t fields[PRINTPANEL_TARGET] = {
		offsetof(settings_t, printer), offsetof(settings_t, printer_text), offsetof(settings_t, printer_idle),
	};
	return fields[line];
}


void printpanel_open(printpanel_t *p, const settings_t *values) {
	memset(p, 0, sizeof *p);
	p->values = *values;
}

static void change(printpanel_t *p, int step) {
	if (p->line >= PRINTPANEL_TARGET) {
		return;
	}
	const settings_field_t *it = &settings_fields[field_of(p->line)];
	uint8_t *value = settings_field(&p->values, field_of(p->line));
	unsigned i = 0;
	while (i < it->n_choices && it->values[i] != *value) {
		++i;
	}
	i = i < it->n_choices ? (i + it->n_choices + (unsigned)step) % it->n_choices : 0;
	*value = it->values[i];
}

static void select_line(printpanel_t *p, int dir) {
	p->line = (p->line + PRINTPANEL_LINES + (unsigned)dir) % PRINTPANEL_LINES;
}

static printpanel_action_t cursor_key(printpanel_t *p, uint8_t final) {
	switch (final) {
	case 'A':
		select_line(p, -1);
		return PRINTPANEL_REDRAW;
	case 'B':
		select_line(p, 1);
		return PRINTPANEL_REDRAW;
	case 'C':
	case 'D':
		change(p, final == 'C' ? 1 : -1);
		return PRINTPANEL_REDRAW;
	default:
		return PRINTPANEL_NONE;
	}
}

// A key on the Target row: printable characters added at the end (no
// spaces: no target has one), Backspace takes the last off.
static printpanel_action_t target_key(printpanel_t *p, uint8_t c) {
	char *t = p->values.printer_target;
	size_t n = strlen(t);
	if (c == 0x08 || c == 0x7f) {
		if (n) {
			t[n - 1] = '\0'; // NUL-padded still
			return PRINTPANEL_REDRAW;
		}
		return PRINTPANEL_NONE;
	}
	if (c > 0x20 && c < 0x7f) {
		if (n < SETTINGS_TARGET_MAX) {
			t[n] = (char)c;
			return PRINTPANEL_REDRAW;
		}
		p->message = "The target is at its longest";
		return PRINTPANEL_REDRAW;
	}
	return PRINTPANEL_NONE;
}

printpanel_action_t printpanel_input(printpanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	printpanel_action_t action = PRINTPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			return had_message ? PRINTPANEL_REDRAW : PRINTPANEL_NONE;
		}
		p->esc = 0;
		return PRINTPANEL_CANCEL; // ESC and something else: the Escape key (or Alt+key)
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
		if (p->values.printer != SETTINGS_PRINTER_OFF && !p->values.printer_target[0]) {
			p->message = "A printer needs a target: host:port/path";
			return PRINTPANEL_REDRAW;
		}
		return PRINTPANEL_APPLY;
	} else if (p->line == PRINTPANEL_TARGET) {
		action = target_key(p, c);
	} else if (c == ' ') {
		change(p, 1);
		action = PRINTPANEL_REDRAW;
	}
	return action == PRINTPANEL_NONE && had_message ? PRINTPANEL_REDRAW : action;
}

printpanel_action_t printpanel_tick(printpanel_t *p, uint32_t now_ms) {
	if (p->esc == 1 && now_ms - p->esc_ms >= PRINTPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return PRINTPANEL_CANCEL;
	}
	return PRINTPANEL_NONE;
}

// A row's name, the selected one marked and in bold.
static void draw_name(screen_t *s, unsigned row, const char *name, bool selected, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', name);
	for (unsigned c = 0; text[c]; ++c) {
		screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
	}
}

// A setting: every choice, the current one reversed (as SET-UP).
static void draw_choices(const printpanel_t *p, screen_t *s, unsigned row, unsigned line, uint8_t fg, uint8_t bg) {
	unsigned f = field_of(line);
	const settings_field_t *it = &settings_fields[f];
	settings_t values = p->values;
	uint8_t value = *settings_field(&values, f);
	draw_name(s, row, it->name, p->line == line, fg, bg);
	unsigned col = VALUE_COL;
	char text[SCREEN_MAX_COLS + 1];
	for (unsigned c = 0; c < it->n_choices; ++c) {
		snprintf(text, sizeof text, " %s ", it->labels[c]);
		bool on = it->values[c] == value;
		unsigned len = (unsigned)strlen(text);
		if (col + len > SCREEN_MAX_COLS) {
			break;
		}
		screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
		col += len;
	}
}

void printpanel_draw(const printpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, PRINTPANEL_ROWS, "SET-UP  Printer (Zimodem on TTY2)", fg, bg, border);
	for (unsigned line = 0; line < PRINTPANEL_TARGET; ++line) {
		draw_choices(p, s, top + 1 + line, line, fg, bg);
	}
	unsigned row = top + 1 + PRINTPANEL_TARGET;
	bool on_target = p->line == PRINTPANEL_TARGET;
	draw_name(s, row, "Target", on_target, fg, bg);
	const char *t = p->values.printer_target;
	screen_puts(s, VALUE_COL + 1, row, t[0] || on_target ? t : "(none)", fg, bg);
	if (on_target) { // a block cursor after the text
		screen_put(s, VALUE_COL + 1 + (unsigned)strlen(t), row, ' ', bg, fg);
	}
	screen_puts(s, LEFT, row + 2, "   e.g. 192.168.1.10:631/ipp/print; the modem's AT+PRINT per job", fg, bg);
	screen_puts(s, LEFT, row + 3,
		p->values.printer == SETTINGS_PRINTER_OFF ? "   Off: TTY2 is the serial port (AUX:, MP/M console 1)"
		: "   In CP/M mode TTY2 is then the printer's: LST: prints, AUX: gets nothing", fg, bg);
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, "   Last job: %s", p->last_job ? p->last_job : "none since power-on");
	screen_puts(s, LEFT, row + 4, text, fg, bg);
	if (p->message) {
		screen_puts(s, LEFT + 3, top + PRINTPANEL_ROWS - 4, p->message, fg, bg);
	}
	screen_puts(s, LEFT, top + PRINTPANEL_ROWS - 3,
		" Up/Down row   Left/Right/Space choice   Target: type, Backspace", fg, bg);
	screen_puts(s, LEFT, top + PRINTPANEL_ROWS - 2, " Return keep the choices (Return in SET-UP saves)   Esc back",
		fg, bg);
}
