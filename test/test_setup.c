#include <stddef.h>
#include <string.h>

#include "setup.h"
#include "clockpanel.h"
#include "datetime.h"
#include "font.h"
#include "palette.h"
#include "test.h"

#define FG RGB222(3, 3, 3)
#define BG RGB222(0, 0, 2)
#define BORDER RGB222(0, 3, 0)

static setup_t u;

static void open_with(unsigned columns) {
	settings_t s;
	settings_defaults(&s);
	s.columns = (uint8_t)columns;
	setup_open(&u, &s);
}

static setup_action_t send(const char *keys, uint32_t now) {
	setup_action_t last = SETUP_NONE;
	for (const char *p = keys; *p; ++p) {
		setup_action_t a = setup_input(&u, (uint8_t)*p, now);
		if (a != SETUP_NONE) {
			last = a;
		}
	}
	return last;
}

static void test_space_and_arrows_change_columns(void) {
	open_with(80);
	CHECK_EQ(send(" ", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send(" ", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW); // Right
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send("\x1bOD", 0), SETUP_REDRAW); // Left, DECCKM form
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(send("\x1b[D", 0), SETUP_REDRAW); // wraps round
	CHECK_EQ(u.values.columns, 132);
}

// Down moves to the next setting, whose choices Space then cycles.
static void test_down_selects_next_setting(void) {
	open_with(80);
	u.values.backspace = 0x7f;
	CHECK_EQ(send("\x1b[B", 0), SETUP_REDRAW);
	CHECK_EQ(u.item, 1);
	send(" ", 0);
	CHECK_EQ(u.values.backspace, 0x08);
	CHECK_EQ(u.values.columns, 80);
	send("\x1b[B ", 0); // Local echo on
	CHECK_EQ(u.values.local_echo, 1);
	for (unsigned i = 2; i < SETUP_ITEMS; ++i) {
		send("\x1b[B", 0);
	}
	CHECK_EQ(u.item, 0); // round to Columns
	send("\x1b[A", 0);
	CHECK_EQ(u.item, SETUP_ITEMS - 1);
}

static void test_return_saves(void) {
	open_with(80);
	send(" ", 0);
	CHECK_EQ(send("\r", 0), SETUP_SAVE);
	CHECK_EQ(u.values.columns, 132);
	CHECK_EQ(send("\n", 0), SETUP_SAVE);
}

static void test_lone_escape_cancels_after_timeout(void) {
	open_with(80);
	CHECK_EQ(send("\x1b", 1000), SETUP_NONE);
	CHECK_EQ(setup_tick(&u, 1000 + SETUP_ESC_TIMEOUT_MS - 1), SETUP_NONE);
	CHECK_EQ(setup_tick(&u, 1000 + SETUP_ESC_TIMEOUT_MS), SETUP_CANCEL);
	CHECK_EQ(setup_tick(&u, 5000), SETUP_NONE); // only once
}

static void test_escape_then_other_key_cancels(void) {
	open_with(80);
	CHECK_EQ(send("\x1bx", 0), SETUP_CANCEL);
}

static void test_other_sequences_are_ignored(void) {
	open_with(80);
	CHECK_EQ(send("\x1b[2~", 0), SETUP_NONE); // Insert
	CHECK_EQ(send("a", 0), SETUP_NONE);
	CHECK_EQ(u.values.columns, 80);
	CHECK_EQ(setup_tick(&u, 10000), SETUP_NONE);
}

static void test_message_cleared_by_next_key(void) {
	open_with(80);
	u.message = "Saving to flash failed";
	CHECK_EQ(send("a", 0), SETUP_REDRAW);
	CHECK(u.message == NULL);
}

static bool row_contains(const screen_t *s, unsigned row, const char *text) {
	char line[SCREEN_MAX_COLS + 1];
	for (unsigned c = 0; c < s->cols; ++c) {
		line[c] = (char)screen_glyph(s, c, row);
	}
	line[s->cols] = 0;
	return strstr(line, text) != NULL;
}

static void test_draw_marks_current_choice(void) {
	static screen_t s;
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	open_with(132);
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS; // above the status line
	setup_draw(&u, &s, top, "build x", FG, BG, BORDER);
	CHECK(row_contains(&s, top, "SET-UP"));
	CHECK(row_contains(&s, top, "build x"));
	CHECK(row_contains(&s, top + 1, "> Columns"));
	CHECK(row_contains(&s, top + 2, "  Backspace sends"));
	CHECK(row_contains(&s, top + 3, "  Local echo"));
	CHECK(row_contains(&s, top + 4, "< 115200 >")); // long lists: current only
	CHECK(row_contains(&s, top + 5, "< 8N1 >"));
	CHECK(row_contains(&s, top + 7, "  Colors"));
	CHECK(row_contains(&s, top + 7, "white   green   amber   blue")); // short lists: all
	CHECK(row_contains(&s, top + SETUP_ROWS - 3, "Return save"));
	// " 132 " is drawn reversed, " 80 " normal.
	char line[81];
	for (unsigned c = 0; c < 80; ++c) {
		line[c] = (char)screen_glyph(&s, c, top + 1);
	}
	line[80] = 0;
	unsigned at80 = (unsigned)(strstr(line, " 80 ") - line) + 1;
	unsigned at132 = (unsigned)(strstr(line, " 132 ") - line) + 1;
	uint8_t fg, bg;
	screen_colors(&s, at80, top + 1, &fg, &bg);
	CHECK_EQ(fg, FG);
	screen_colors(&s, at132, top + 1, &fg, &bg);
	CHECK_EQ(fg, BG);
	CHECK_EQ(bg, FG);
}

// Under Colors and Palette, two rows preview the choice: default text,
// bold, status line, and each ANSI color as text (first row) and as a
// background (second row, same column).
static void test_colors_preview(void) {
	static screen_t s;
	screen_init(&s, 80, RGB222(2, 2, 2), 0);
	open_with(80);
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS;
	const unsigned preview = top + 9; // under Palette (item 7, row top + 8)
	setup_draw(&u, &s, top, "", FG, BG, BORDER);
	CHECK(row_contains(&s, top + 7, "Colors"));
	CHECK(row_contains(&s, top + 8, "Palette"));
	CHECK(row_contains(&s, preview, " Text Bold  Status "));
	CHECK(row_contains(&s, preview, "0 1 2 3 4 5 6 7 8 9 A B C D E F"));
	CHECK(row_contains(&s, preview + 1, "0 1 2 3 4 5 6 7 8 9 A B C D E F"));
	CHECK(row_contains(&s, top + SETUP_ROWS - 3, "Return save"));

	unsigned text_col = 0, digits = 0;
	for (unsigned c = 0; c < 80; ++c) {
		if (!text_col && screen_glyph(&s, c, preview) == 'T') {
			text_col = c;
		}
		if (!digits && screen_glyph(&s, c, preview) == '0') {
			digits = c;
		}
	}
	// Palette indices: the palette on screen shows what they look like.
	uint8_t fg, bg;
	screen_colors(&s, text_col, preview, &fg, &bg);
	CHECK_EQ(fg, PALETTE_TEXT);
	CHECK_EQ(bg, PALETTE_BACKGROUND);
	screen_colors(&s, text_col + 5, preview, &fg, &bg); // "Bold"
	CHECK_EQ(fg, PALETTE_BOLD_TEXT);
	CHECK(screen_attr(&s, text_col + 5, preview) & SCREEN_ATTR_BOLD);
	unsigned status_col = text_col + 12; // " Status "
	screen_colors(&s, status_col, preview, &fg, &bg);
	CHECK(fg == PALETTE_BOLD_TEXT && bg == PALETTE_BLUE); // white, color
	for (unsigned i = 0; i < 16; ++i) {
		unsigned col = digits + 2 * i;
		screen_colors(&s, col, preview, &fg, &bg);
		CHECK_EQ(fg, i); // as text
		CHECK_EQ(bg, PALETTE_BACKGROUND);
		screen_colors(&s, col, preview + 1, &fg, &bg);
		CHECK_EQ(bg, i); // as background
		CHECK(fg != bg);
		screen_colors(&s, col + 1, preview + 1, &fg, &bg);
		CHECK_EQ(bg, PALETTE_BACKGROUND); // spaced out with the default background
	}

	// Colors -> blue and Palette -> mono: the samples keep their indices
	// (the owner shows the edited palette); the status sample follows.
	for (unsigned i = 0; i < 6; ++i) {
		send("\x1b[B", 0);
	}
	send("\x1b[D", 0); // white -> blue (wraps)
	send("\x1b[B ", 0); // Palette: mono
	CHECK_EQ(u.values.colors, 3);
	CHECK_EQ(u.values.palette, 1);
	setup_draw(&u, &s, top, "", FG, BG, BORDER);
	screen_colors(&s, digits + 2, preview, &fg, &bg); // red as text
	CHECK(fg == 1 && bg == PALETTE_BACKGROUND);
	screen_colors(&s, status_col, preview, &fg, &bg);
	CHECK(fg == PALETTE_BACKGROUND && bg == PALETTE_TEXT);
}

static void test_machine_menu_key(void) {
	open_with(80);
	CHECK_EQ(send("m", 0), SETUP_NONE); // terminal mode: not offered
	u.machine_menu = true;
	CHECK_EQ(send("M", 0), SETUP_MACHINE);
	CHECK_EQ(send("m", 0), SETUP_MACHINE);
	CHECK_EQ(send("D", 0), SETUP_DRIVES);

	screen_t s;
	screen_init(&s, 80, FG, BG);
	setup_draw(&u, &s, SCREEN_ROWS - 1 - SETUP_ROWS, "", FG, BG, BORDER);
	char help[81] = "";
	for (unsigned c = 0; c < 80; ++c) {
		help[c] = (char)screen_glyph(&s, c, SCREEN_ROWS - 3);
	}
	CHECK(strstr(help, "R reboot  C clock  D drives  M monitor") != NULL);
}

// T shows the demo screen, in either mode.
static void test_demo_key(void) {
	open_with(80);
	CHECK_EQ(send("t", 0), SETUP_DEMO);
	CHECK_EQ(send("T", 0), SETUP_DEMO);
	screen_t s;
	screen_init(&s, 80, FG, BG);
	setup_draw(&u, &s, SCREEN_ROWS - 1 - SETUP_ROWS, "", FG, BG, BORDER);
	CHECK(row_contains(&s, SCREEN_ROWS - 3, "T demo"));
}

// R reboots, in either mode; the help line says so and still fits 80
// columns.
static void test_reboot_key(void) {
	for (int machine = 0; machine <= 1; ++machine) {
		open_with(80);
		u.machine_menu = machine;
		CHECK_EQ(send("r", 0), SETUP_REBOOT);
		CHECK_EQ(send("R", 0), SETUP_REBOOT);
		screen_t s;
		screen_init(&s, 80, FG, BG);
		setup_draw(&u, &s, SCREEN_ROWS - 1 - SETUP_ROWS, "", FG, BG, BORDER);
		CHECK(row_contains(&s, SCREEN_ROWS - 3, "R reboot"));
		CHECK(row_contains(&s, SCREEN_ROWS - 4, "Return save"));
		CHECK(row_contains(&s, SCREEN_ROWS - 4, "Esc cancel"));
		// Inside the box: the right-hand line is still there.
		CHECK_EQ(screen_glyph(&s, 79, SCREEN_ROWS - 3), GLYPH_DEC_GRAPHICS('x'));
	}
	// Alt+R (ESC r) is still the Escape key, not a reboot.
	open_with(80);
	CHECK_EQ(send("\x1br", 0), SETUP_CANCEL);
}

// U opens the firmware update, in either mode; the help line says so.
static void test_firmware_key(void) {
	for (int machine = 0; machine <= 1; ++machine) {
		open_with(80);
		u.machine_menu = machine;
		CHECK_EQ(send("u", 0), SETUP_FIRMWARE);
		CHECK_EQ(send("U", 0), SETUP_FIRMWARE);
		static screen_t s;
		screen_init(&s, 80, FG, BG);
		setup_draw(&u, &s, SCREEN_ROWS - 1 - SETUP_ROWS, "", FG, BG, BORDER);
		CHECK(row_contains(&s, SCREEN_ROWS - 3, "U firmware update"));
	}
}

// C opens the Clock panel, in either mode; the help line says so.
static void test_clock_key(void) {
	for (int machine = 0; machine <= 1; ++machine) {
		open_with(80);
		u.machine_menu = machine;
		CHECK_EQ(send("c", 0), SETUP_CLOCK);
		CHECK_EQ(send("C", 0), SETUP_CLOCK);
		static screen_t s;
		screen_init(&s, 80, FG, BG);
		setup_draw(&u, &s, SCREEN_ROWS - 1 - SETUP_ROWS, "", FG, BG, BORDER);
		CHECK(row_contains(&s, SCREEN_ROWS - 3, "C clock"));
		// SET-UP's own rows stop before the clock's settings.
		CHECK(!row_contains(&s, SCREEN_ROWS - 1 - SETUP_ROWS + 1 + SETUP_ITEMS + 2, "Status line clock"));
	}
}

// Bell is SET-UP's last row; without a bell it shows off, fixed, and is
// skipped.
static void test_bell_row(void) {
	static screen_t s;
	screen_init(&s, 80, FG, BG);
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS;
	open_with(80);
	send("\x1b[A", 0); // round to the last row
	CHECK_EQ(u.item, SETUP_ITEMS - 1);
	CHECK_EQ(u.values.bell, SETTINGS_BELL_MEDIUM);
	send(" ", 0);
	CHECK_EQ(u.values.bell, SETTINGS_BELL_HIGH);
	setup_draw(&u, &s, top, "", FG, BG, BORDER);
	const unsigned bell_row = top + SETUP_ITEMS + 2; // under the two preview rows
	CHECK(row_contains(&s, bell_row, "> Bell") && row_contains(&s, bell_row, "off   low   medium   high"));

	open_with(80);
	u.no_bell = true;
	send("\x1b[A", 0);
	CHECK_EQ(u.item, SETUP_ITEMS - 2); // Bell skipped
	u.item = SETUP_ITEMS - 1;
	send(" ", 0);
	CHECK_EQ(u.values.bell, SETTINGS_BELL_MEDIUM); // unchanged
	setup_draw(&u, &s, top, "", FG, BG, BORDER);
	CHECK(row_contains(&s, bell_row, " off  (no bell on this board)"));
}

// ---- the Clock panel (clockpanel.h, bd 1ls.6.4) ----

static clockpanel_t cp;

static clockpanel_action_t clock_send(const char *keys) {
	clockpanel_action_t last = CLOCKPANEL_NONE;
	for (const char *k = keys; *k; ++k) {
		clockpanel_action_t a = clockpanel_input(&cp, (uint8_t)*k, 0);
		if (a != CLOCKPANEL_NONE) {
			last = a;
		}
	}
	return last;
}

static uint32_t secs_at(unsigned y, unsigned mo, unsigned d, unsigned h, unsigned mi, unsigned sec) {
	datetime_t t = {(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)sec, 0};
	uint32_t secs = 0;
	CHECK(datetime_to_secs(&t, &secs));
	return secs;
}

static void clock_open_at(uint32_t secs) {
	settings_t v;
	settings_defaults(&v);
	clockpanel_open(&cp, &v, secs, true);
}

// Date and Time follow the clock until typed in; Return then sets nothing.
static void test_clock_panel_follows(void) {
	uint32_t now = secs_at(2026, 10, 22, 13, 45, 30);
	clock_open_at(now);
	CHECK(strcmp(cp.date, "2026-10-22") == 0);
	CHECK(strcmp(cp.time, "13:45:30") == 0);
	CHECK_EQ(clockpanel_tick(&cp, 0, now), CLOCKPANEL_NONE);
	CHECK_EQ(clockpanel_tick(&cp, 0, now + 31), CLOCKPANEL_REDRAW);
	CHECK(strcmp(cp.time, "13:46:01") == 0);
	CHECK_EQ(clock_send("\r"), CLOCKPANEL_APPLY);
	uint32_t secs;
	CHECK(!clockpanel_time(&cp, &secs));

	static screen_t s;
	screen_init(&s, 80, FG, BG);
	const unsigned top = SCREEN_ROWS - 1 - CLOCKPANEL_ROWS;
	clockpanel_draw(&cp, &s, top, FG, BG, BORDER);
	CHECK(row_contains(&s, top, "SET-UP  Clock"));
	CHECK(row_contains(&s, top + 1, "> Date") && row_contains(&s, top + 1, "2026-10-22"));
	CHECK(row_contains(&s, top + 2, "Time") && row_contains(&s, top + 2, "13:46:01"));
	CHECK(row_contains(&s, top + 4, "Status line clock") && row_contains(&s, top + 4, "24 h   12 h   off"));
	CHECK(row_contains(&s, top + 7, "Status line shows  13:46")); // the default format
	// Without an RTC the title says it lasts until power-off.
	settings_t v;
	settings_defaults(&v);
	clockpanel_open(&cp, &v, now, false);
	clockpanel_draw(&cp, &s, top, FG, BG, BORDER);
	CHECK(row_contains(&s, top, "no RTC"));
}

// Digits overwrite the one under the cursor, skipping the separators;
// Return sets the clock to them.
static void test_clock_panel_typing(void) {
	clock_open_at(secs_at(2026, 10, 22, 13, 45, 30));
	CHECK_EQ(clock_send("20251231"), CLOCKPANEL_REDRAW);
	CHECK(strcmp(cp.date, "2025-12-31") == 0);
	CHECK(cp.edited);
	CHECK_EQ(clockpanel_tick(&cp, 0, secs_at(2026, 10, 22, 13, 50, 0)), CLOCKPANEL_NONE); // stays as typed
	CHECK(strcmp(cp.date, "2025-12-31") == 0);
	// Down to Time; Right twice skips the colon.
	CHECK_EQ(clock_send("\x1b[B23"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.cursor, 3u);
	CHECK_EQ(clock_send("59"), CLOCKPANEL_REDRAW);
	CHECK_EQ(clock_send("\x7f\x7f"), CLOCKPANEL_REDRAW); // Backspace twice: back on 59's 5
	CHECK_EQ(clock_send("5958"), CLOCKPANEL_REDRAW);
	CHECK(strcmp(cp.time, "23:59:58") == 0);
	CHECK_EQ(clock_send("7"), CLOCKPANEL_REDRAW); // at the end: the last digit again
	CHECK(strcmp(cp.time, "23:59:57") == 0);
	CHECK_EQ(clock_send("\x1b[D\x1b[D\x1b[D"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.cursor, 3u);
	CHECK_EQ(clock_send("\r"), CLOCKPANEL_APPLY);
	uint32_t secs;
	CHECK(clockpanel_time(&cp, &secs));
	CHECK_EQ(secs, secs_at(2025, 12, 31, 23, 59, 57));

	// Not a date: Return stays, with a message.
	clock_open_at(secs_at(2026, 10, 22, 13, 45, 30));
	CHECK_EQ(clock_send("20260230\r"), CLOCKPANEL_REDRAW);
	CHECK(cp.message != NULL);
	CHECK(!clockpanel_time(&cp, &secs));
	CHECK_EQ(clock_send("\x1b[D2"), CLOCKPANEL_REDRAW); // the 3 of 30 made a 2
	CHECK(strcmp(cp.date, "2026-02-20") == 0);
	CHECK(cp.message == NULL);
	CHECK_EQ(clock_send("\r"), CLOCKPANEL_APPLY);
}

// The status line rows step through the clock settings' choices, and the
// sample shows what they give.
static void test_clock_panel_format(void) {
	uint32_t now = secs_at(2026, 10, 22, 13, 45, 30);
	clock_open_at(now);
	CHECK_EQ(clock_send("\x1b[B\x1b[B\x1b[C"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.values.clock, SETTINGS_CLOCK_12H);
	CHECK_EQ(clock_send("\x1b[B "), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.values.clock_seconds, 1);
	CHECK_EQ(clock_send("\x1b[B  "), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.values.clock_date, SETTINGS_DATE_SHORT);
	static screen_t s;
	screen_init(&s, 80, FG, BG);
	const unsigned top = SCREEN_ROWS - 1 - CLOCKPANEL_ROWS;
	clockpanel_draw(&cp, &s, top, FG, BG, BORDER);
	CHECK(row_contains(&s, top + 7, "Thu 22 Oct  1:45:30 PM"));
	// Down from the last row wraps to Date; Up from Date to the last row.
	CHECK_EQ(clock_send("\x1b[B"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.line, (unsigned)CLOCKPANEL_DATE);
	CHECK_EQ(clock_send("\x1b[A\x1b[D"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.values.clock_date, SETTINGS_DATE_ISO);
	// Off: the sample says so; Space on Date does nothing.
	CHECK_EQ(clock_send("\x1b[A\x1b[A "), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.values.clock, SETTINGS_CLOCK_OFF);
	clockpanel_draw(&cp, &s, top, FG, BG, BORDER);
	CHECK(row_contains(&s, top + 7, "no clock"));
	CHECK_EQ(clock_send("\x1b[A\x1b[A"), CLOCKPANEL_REDRAW);
	CHECK_EQ(cp.line, (unsigned)CLOCKPANEL_DATE);
	CHECK_EQ(clock_send(" "), CLOCKPANEL_NONE);
	CHECK(!cp.edited);
}

static void test_clock_panel_escape(void) {
	clock_open_at(1000);
	CHECK_EQ(clockpanel_input(&cp, 0x1b, 50), CLOCKPANEL_NONE);
	CHECK_EQ(clockpanel_tick(&cp, 100, 1000), CLOCKPANEL_NONE);
	CHECK_EQ(clockpanel_tick(&cp, 150, 1000), CLOCKPANEL_CANCEL);
	clock_open_at(1000);
	CHECK_EQ(clock_send("\x1bx"), CLOCKPANEL_CANCEL);
}

static void test_mode_setting(void) {
	open_with(80);
	CHECK_EQ(u.values.mode, SETTINGS_MODE_TERMINAL);
	for (unsigned i = 0; i < offsetof(settings_t, mode); ++i) {
		send("\x1b[B", 0);
	}
	CHECK_EQ(send(" ", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.mode, SETTINGS_MODE_CPM);
}

static void test_status_line_setting(void) {
	open_with(80);
	CHECK_EQ(u.values.status_line, 1);
	for (unsigned i = 0; i < offsetof(settings_t, status_line); ++i) {
		send("\x1b[B", 0);
	}
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.status_line, 0);
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.status_line, SETTINGS_STATUS_DEBUG);
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.status_line, SETTINGS_STATUS_DEBUG_USB);
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.status_line, 1);
}

static void test_blank_after_setting(void) {
	open_with(80);
	CHECK_EQ(u.values.blank_after, 0);
	for (unsigned i = 0; i < offsetof(settings_t, blank_after); ++i) {
		send("\x1b[B", 0);
	}
	CHECK_EQ(send("\x1b[C", 0), SETUP_REDRAW);
	CHECK_EQ(u.values.blank_after, 1);
	send("\x1b[D\x1b[D", 0); // wraps
	CHECK_EQ(u.values.blank_after, 60);
	// More choices than fit inline: just the current one, as "< 60 min >".
	static screen_t scr;
	screen_init(&scr, 80, FG, BG);
	setup_draw(&u, &scr, 0, "", FG, BG, BORDER);
	unsigned r = 1 + offsetof(settings_t, blank_after) + 2; // the preview rows come before it
	CHECK(row_contains(&scr, r, "> Blank after"));
	CHECK(row_contains(&scr, r, "< 60 min >"));
}

// The ADM-3A is 80 columns only: Columns shows 80 and what fixes it, is
// skipped by Up/Down and cannot change, and its own value is kept (dmt.39).
static void test_columns_fixed_by_adm3a(void) {
	static screen_t s;
	screen_init(&s, 80, FG, BG);
	settings_t v;
	settings_defaults(&v);
	v.columns = 132;
	v.term_type = SETTINGS_TERM_ADM3A;
	setup_open(&u, &v);
	CHECK_EQ(u.item, 1); // not on Columns
	const unsigned type = offsetof(settings_t, term_type);
	for (unsigned i = 0; i < 2 * SETUP_ITEMS; ++i) {
		send("\x1b[B", 0);
		CHECK(u.item != 0);
	}
	for (unsigned i = 0; i < 2 * SETUP_ITEMS; ++i) {
		send("\x1b[A", 0);
		CHECK(u.item != 0);
	}
	u.item = 0; // even if selected, it does not change
	send(" \x1b[C", 0);
	CHECK_EQ(u.values.columns, 132);
	setup_draw(&u, &s, 0, "", FG, BG, BORDER);
	CHECK(row_contains(&s, 1, "> Columns"));
	CHECK(row_contains(&s, 1, " 80  (ADM-3A)")); // where 132 would be
	CHECK(!row_contains(&s, 1, "132"));
	char line[81];
	for (unsigned c = 0; c < 80; ++c) {
		line[c] = (char)screen_glyph(&s, c, 1);
	}
	line[80] = 0;
	unsigned at80 = (unsigned)(strstr(line, " 80 ") - line) + 1;
	uint8_t fg, bg;
	screen_colors(&s, at80, 1, &fg, &bg);
	CHECK(fg == BG && bg == FG); // the value in use, reversed

	// Live: Terminal type back to VT100 frees Columns with its 132.
	u.item = type;
	send("\x1b[C", 0); // ADM-3A -> VT100 (wraps)
	CHECK_EQ(u.values.term_type, SETTINGS_TERM_VT100);
	setup_draw(&u, &s, 0, "", FG, BG, BORDER);
	CHECK(row_contains(&s, 1, " 80   132 "));
	CHECK(!row_contains(&s, 1, "ADM-3A"));
	u.item = 1;
	send("\x1b[A", 0);
	CHECK_EQ(u.item, 0);
	send(" ", 0);
	CHECK_EQ(u.values.columns, 80);
	// And back to ADM-3A: fixed again.
	u.item = type;
	send("\x1b[D", 0);
	CHECK_EQ(u.values.term_type, SETTINGS_TERM_ADM3A);
	send("\x1b[B", 0); // from Terminal type to Bell (the last row)
	CHECK_EQ(u.item, SETUP_ITEMS - 1);
	send("\x1b[B", 0); // round past Columns
	CHECK_EQ(u.item, 1);
}

// The panel is a box in the border color; the selected setting's name is
// bold and its current choice reversed, so mono shows them too.
static void test_box_and_selection(void) {
	static screen_t s;
	screen_init(&s, 80, FG, BG);
	open_with(80);
	send("\x1b[B", 0); // Backspace sends
	const unsigned top = SCREEN_ROWS - 1 - SETUP_ROWS, bottom = top + SETUP_ROWS - 1;
	setup_draw(&u, &s, top, "build x", FG, BG, BORDER);
	CHECK_EQ(screen_glyph(&s, 0, top), GLYPH_DEC_GRAPHICS('l'));
	CHECK_EQ(screen_glyph(&s, 79, top), GLYPH_DEC_GRAPHICS('k'));
	CHECK_EQ(screen_glyph(&s, 0, bottom), GLYPH_DEC_GRAPHICS('m'));
	CHECK_EQ(screen_glyph(&s, 79, bottom), GLYPH_DEC_GRAPHICS('j'));
	CHECK_EQ(screen_glyph(&s, 40, bottom), GLYPH_DEC_GRAPHICS('q'));
	for (unsigned r = top + 1; r < bottom; ++r) {
		CHECK_EQ(screen_glyph(&s, 0, r), GLYPH_DEC_GRAPHICS('x'));
		CHECK_EQ(screen_glyph(&s, 79, r), GLYPH_DEC_GRAPHICS('x'));
	}
	uint8_t fg, bg;
	screen_colors(&s, 0, top + 3, &fg, &bg);
	CHECK(fg == BORDER && bg == BG);
	CHECK(row_contains(&s, top, " SET-UP  build x "));
	screen_colors(&s, 3, top, &fg, &bg); // the title: text colors
	CHECK(fg == FG && bg == BG);
	// "> Backspace sends" in bold; "Columns" not.
	CHECK(row_contains(&s, top + 2, "> Backspace sends"));
	CHECK(screen_attr(&s, 4, top + 2) & SCREEN_ATTR_BOLD);
	CHECK(!(screen_attr(&s, 4, top + 1) & SCREEN_ATTR_BOLD));
	// A long title stops short of the corner.
	char title[200];
	memset(title, 'x', sizeof title - 1);
	title[sizeof title - 1] = 0;
	setup_draw(&u, &s, top, title, FG, BG, BORDER);
	CHECK_EQ(screen_glyph(&s, 79, top), GLYPH_DEC_GRAPHICS('k'));
	CHECK_EQ(screen_glyph(&s, 78, top), ' ');
}

int main(void) {
	RUN(test_space_and_arrows_change_columns);
	RUN(test_down_selects_next_setting);
	RUN(test_return_saves);
	RUN(test_lone_escape_cancels_after_timeout);
	RUN(test_escape_then_other_key_cancels);
	RUN(test_other_sequences_are_ignored);
	RUN(test_message_cleared_by_next_key);
	RUN(test_draw_marks_current_choice);
	RUN(test_box_and_selection);
	RUN(test_columns_fixed_by_adm3a);
	RUN(test_colors_preview);
	RUN(test_machine_menu_key);
	RUN(test_demo_key);
	RUN(test_reboot_key);
	RUN(test_firmware_key);
	RUN(test_bell_row);
	RUN(test_clock_key);
	RUN(test_clock_panel_follows);
	RUN(test_clock_panel_typing);
	RUN(test_clock_panel_format);
	RUN(test_clock_panel_escape);
	RUN(test_mode_setting);
	RUN(test_status_line_setting);
	RUN(test_blank_after_setting);
	return tests_done();
}
