#include "clockpanel.h"

#include <stdio.h>
#include <string.h>

#include "datetime.h"

// The text starts inside the box (screen_frame).
#define LEFT 1
#define NAME_WIDTH 18
#define VALUE_COL (LEFT + 3 + NAME_WIDTH)

// The settings field of line (2 ..), settings_fields' index.
static unsigned field_of(unsigned line) {
	return SETTINGS_MAIN_FIELDS + line - 2;
}

static char *text_of(clockpanel_t *p) {
	return p->line == CLOCKPANEL_DATE ? p->date : p->time;
}

static void show_time(clockpanel_t *p, uint32_t secs) {
	datetime_t t;
	datetime_from_secs(secs, &t);
	snprintf(p->date, sizeof p->date, "%04u-%02u-%02u", t.year % 10000u, t.month % 100u, t.mday % 100u);
	snprintf(p->time, sizeof p->time, "%02u:%02u:%02u", t.hour % 100u, t.min % 100u, t.sec % 100u);
	p->shown_secs = secs;
}

void clockpanel_open(clockpanel_t *p, const settings_t *values, uint32_t now_secs, bool has_rtc) {
	memset(p, 0, sizeof *p);
	p->values = *values;
	p->has_rtc = has_rtc;
	show_time(p, now_secs);
}

// The cursor to the next (dir 1) or previous digit of the Date or Time
// text, staying put at the ends.
static void move_cursor(clockpanel_t *p, int dir) {
	const char *text = text_of(p);
	int at = (int)p->cursor;
	do {
		at += dir;
	} while (at >= 0 && text[at] && (text[at] < '0' || text[at] > '9'));
	if (at >= 0 && text[at]) {
		p->cursor = (unsigned)at;
	}
}

static void change(clockpanel_t *p, int step) {
	const settings_field_t *it = &settings_fields[field_of(p->line)];
	uint8_t *value = settings_field(&p->values, field_of(p->line));
	unsigned i = 0;
	while (i < it->n_choices && it->values[i] != *value) {
		++i;
	}
	i = i < it->n_choices ? (i + it->n_choices + (unsigned)step) % it->n_choices : 0;
	*value = it->values[i];
}

static void select_line(clockpanel_t *p, int dir) {
	p->line = (p->line + CLOCKPANEL_LINES + (unsigned)dir) % CLOCKPANEL_LINES;
	p->cursor = 0;
}

static clockpanel_action_t cursor_key(clockpanel_t *p, uint8_t final) {
	switch (final) {
	case 'A':
		select_line(p, -1);
		return CLOCKPANEL_REDRAW;
	case 'B':
		select_line(p, 1);
		return CLOCKPANEL_REDRAW;
	case 'C':
	case 'D':
		if (p->line <= CLOCKPANEL_TIME) {
			move_cursor(p, final == 'C' ? 1 : -1);
		} else {
			change(p, final == 'C' ? 1 : -1);
		}
		return CLOCKPANEL_REDRAW;
	default:
		return CLOCKPANEL_NONE;
	}
}

// The n digits at text (always digits: only digits are typed over them).
// No sscanf: newlib's costs ~20 KB of SRAM.
static unsigned digits(const char *text, unsigned n) {
	unsigned v = 0;
	for (unsigned i = 0; i < n; ++i) {
		v = v * 10 + (unsigned)(text[i] - '0');
	}
	return v;
}

// The date and time typed in, as seconds; false if they are no time.
static bool typed_time(const clockpanel_t *p, uint32_t *secs) {
	datetime_t t = {(uint16_t)digits(p->date, 4), (uint8_t)digits(p->date + 5, 2), (uint8_t)digits(p->date + 8, 2),
		(uint8_t)digits(p->time, 2), (uint8_t)digits(p->time + 3, 2), (uint8_t)digits(p->time + 6, 2), 0};
	return datetime_to_secs(&t, secs);
}

clockpanel_action_t clockpanel_input(clockpanel_t *p, uint8_t c, uint32_t now_ms) {
	bool had_message = p->message != NULL;
	p->message = NULL;
	clockpanel_action_t action = CLOCKPANEL_NONE;
	if (p->esc == 1) {
		if (c == '[' || c == 'O') {
			p->esc = 2;
			return had_message ? CLOCKPANEL_REDRAW : CLOCKPANEL_NONE;
		}
		p->esc = 0;
		return CLOCKPANEL_CANCEL; // ESC and something else: the Escape key (or Alt+key)
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
		uint32_t secs;
		if (p->edited && !typed_time(p, &secs)) {
			p->message = "That is not a date and time from 1978 to 2114";
			return CLOCKPANEL_REDRAW;
		}
		return CLOCKPANEL_APPLY;
	} else if (p->line <= CLOCKPANEL_TIME) {
		char *text = text_of(p);
		if (c >= '0' && c <= '9') {
			text[p->cursor] = (char)c;
			p->edited = true;
			move_cursor(p, 1);
			action = CLOCKPANEL_REDRAW;
		} else if (c == 0x08 || c == 0x7f) {
			move_cursor(p, -1);
			action = CLOCKPANEL_REDRAW;
		}
	} else if (c == ' ') {
		change(p, 1);
		action = CLOCKPANEL_REDRAW;
	}
	return action == CLOCKPANEL_NONE && had_message ? CLOCKPANEL_REDRAW : action;
}

clockpanel_action_t clockpanel_tick(clockpanel_t *p, uint32_t now_ms, uint32_t now_secs) {
	if (p->esc == 1 && now_ms - p->esc_ms >= CLOCKPANEL_ESC_TIMEOUT_MS) {
		p->esc = 0;
		return CLOCKPANEL_CANCEL;
	}
	if (!p->edited && now_secs != p->shown_secs) {
		show_time(p, now_secs);
		return CLOCKPANEL_REDRAW;
	}
	return CLOCKPANEL_NONE;
}

bool clockpanel_time(const clockpanel_t *p, uint32_t *secs) {
	return p->edited && typed_time(p, secs);
}

// A row's name, the selected one marked and in bold.
static void draw_name(screen_t *s, unsigned row, const char *name, bool selected, uint8_t fg, uint8_t bg) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %c %s", selected ? '>' : ' ', name);
	for (unsigned c = 0; text[c]; ++c) {
		screen_put_attr(s, LEFT + c, row, (uint8_t)text[c], fg, bg, selected ? SCREEN_ATTR_BOLD : 0);
	}
}

// Date or Time: the text, the cursor's digit reversed on the selected line.
static void draw_entry(const clockpanel_t *p, screen_t *s, unsigned row, unsigned line, const char *name,
	const char *text, uint8_t fg, uint8_t bg) {
	bool selected = p->line == line;
	draw_name(s, row, name, selected, fg, bg);
	for (unsigned c = 0; text[c]; ++c) {
		bool cursor = selected && c == p->cursor;
		screen_put(s, VALUE_COL + 1 + c, row, (uint8_t)text[c], cursor ? bg : fg, cursor ? fg : bg);
	}
}

// A clock setting: every choice, the current one reversed (as SET-UP).
static void draw_choices(const clockpanel_t *p, screen_t *s, unsigned row, unsigned line, uint8_t fg, uint8_t bg) {
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
		screen_puts(s, col, row, text, on ? bg : fg, on ? fg : bg);
		col += (unsigned)strlen(it->labels[c]) + 3;
	}
}

void clockpanel_draw(const clockpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border) {
	screen_frame(s, top, CLOCKPANEL_ROWS, p->has_rtc ? "SET-UP  Clock" : "SET-UP  Clock (no RTC: until power-off)",
		fg, bg, border);
	draw_entry(p, s, top + 1, CLOCKPANEL_DATE, "Date", p->date, fg, bg);
	draw_entry(p, s, top + 2, CLOCKPANEL_TIME, "Time", p->time, fg, bg);
	screen_puts(s, VALUE_COL + 11, top + 2, p->edited ? "  (Return sets the clock)" : "", fg, bg);
	for (unsigned line = 2; line < CLOCKPANEL_LINES; ++line) {
		draw_choices(p, s, top + 2 + line, line, fg, bg);
	}
	// What the status line will show, at the time above.
	char text[SCREEN_MAX_COLS + 1], clock[DATETIME_CLOCK_MAX + 1] = "no clock";
	if (p->values.clock != SETTINGS_CLOCK_OFF) {
		uint32_t secs = p->shown_secs;
		if (p->edited) {
			clockpanel_time(p, &secs); // as typed, if it is a time
		}
		datetime_clock_t format = {p->values.clock == SETTINGS_CLOCK_12H, p->values.clock_seconds != 0,
			p->values.clock_date};
		datetime_clock_text(clock, secs, true, &format);
	}
	draw_name(s, top + 2 + CLOCKPANEL_LINES, "Status line shows", false, fg, bg);
	snprintf(text, sizeof text, " %s", clock);
	screen_puts(s, VALUE_COL, top + 2 + CLOCKPANEL_LINES, text, fg, bg);
	if (p->message) {
		screen_puts(s, LEFT + 3, top + CLOCKPANEL_ROWS - 4, p->message, fg, bg);
	}
	screen_puts(s, LEFT, top + CLOCKPANEL_ROWS - 3,
		" Up/Down row   digits, Left/Right date and time   Left/Right/Space choice", fg, bg);
	screen_puts(s, LEFT, top + CLOCKPANEL_ROWS - 2,
		" Return set the clock (if typed) and keep the choices   Esc back", fg, bg);
}
