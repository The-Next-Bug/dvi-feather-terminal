// The user guide's generated parts (scripts/docs.sh, bd 1ay): screenshots
// rendered by the firmware's own portable code (term, screen, raster,
// palette, SET-UP and its panels, the demo) to the pixels the DVI output
// sends, and the settings tables from settings_fields[], so neither drifts
// from the firmware.
//
// Usage: docgen OUTDIR
// Writes OUTDIR/<name>.ppm (binary PPM, 24-bit; docs.sh makes PNGs) and
// OUTDIR/settings-main.md, settings-panel.md (Markdown tables).
//
// The status line is status.c's (RP2350 code), rebuilt here in its format.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clockpanel.h"
#include "datetime.h"
#include "demo.h"
#include "palette.h"
#include "raster.h"
#include "screen.h"
#include "settings.h"
#include "setup.h"
#include "term.h"

#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define SETUP_TOP (TERM_ROWS - SETUP_ROWS)
#define BUILD "v0.11.0"

static const char *out_dir;
static screen_t screen;
static term_t term;
static settings_t settings;
static settings_colors_t colors;

// 28 Oct 2026, 14:05: the status line's clock and the Clock panel's.
static uint32_t sample_time(void) {
	datetime_t t = {.year = 2026, .month = 10, .mday = 28, .hour = 14, .min = 5, .sec = 0};
	uint32_t secs = 0;
	datetime_to_secs(&t, &secs);
	return secs;
}

static FILE *open_out(const char *name) {
	char path[512];
	snprintf(path, sizeof path, "%s/%s", out_dir, name);
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(1);
	}
	return f;
}

// The screen as the DVI output shows it, in settings' palette, at the
// raster's width: every scanline composed as core 1 composes it.
static void write_ppm(const char *name, bool cursor) {
	static uint16_t table[PALETTE_SIZE];
	static uint32_t words[PALETTE_SIZE];
	static raster_line_t line;
	static uint32_t px[RASTER_LINE_WORDS];
	palette_build(table, settings.colors, settings.palette);
	for (unsigned c = 0; c < PALETTE_SIZE; ++c) {
		words[c] = table[c] | (uint32_t)table[c] << 16;
	}
	const unsigned width = raster_width();
	FILE *f = open_out(name);
	fprintf(f, "P6\n%u %u\n255\n", width, RASTER_ACTIVE_HEIGHT);
	raster_layout_t layout = raster_layout(screen.cols);
	for (unsigned row = 0; row < SCREEN_ROWS; ++row) {
		unsigned flags = screen.reverse_rows >> row & 1 ? RASTER_REVERSE : 0;
		raster_render_line(&line, screen_line(&screen, row), layout, flags);
		int cursor_col = cursor && screen.cursor_visible && row == screen.cursor_row ? screen.cursor_col : -1;
		for (unsigned sy = 0; sy < RASTER_ROW_HEIGHT; ++sy) {
			raster_compose(px, &line, layout, raster_stored_row(layout, sy), cursor_col, words);
			for (unsigned x = 0; x < width; ++x) {
				unsigned p = px[x / 2] >> 16 * (x & 1) & 0xffff;
				unsigned r = p >> 11 & 0x1f, g = p >> 5 & 0x3f, b = p & 0x1f;
				fputc((int)(r << 3 | r >> 2), f);
				fputc((int)(g << 2 | g >> 4), f);
				fputc((int)(b << 3 | b >> 2), f);
			}
		}
	}
	fclose(f);
}

// The terminal's status line in status.c's format, with the clock.
static void draw_status(const char *text) {
	screen_clear_line(&screen, STATUS_ROW, colors.status_fg, colors.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colors.status_fg, colors.status_bg);
	char clock[DATETIME_CLOCK_MAX + 1];
	datetime_clock_t fmt = {.twelve_hour = false, .seconds = false, .date = DATETIME_DATE_NONE};
	unsigned n = datetime_clock_text(clock, sample_time(), true, &fmt);
	screen_puts(&screen, screen.cols - n - 1, STATUS_ROW, clock, colors.status_fg, colors.status_bg);
}

static void terminal_status(void) {
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " VT100 %ux24 | %s | 115200 8N1 | kbd | key ESC [A", screen.cols, BUILD);
	draw_status(text);
}

// A terminal at cols with a short shell session.
static void start_terminal(unsigned cols) {
	colors = settings_colors(&settings);
	screen_init(&screen, cols, PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&term, &screen, 0, TERM_ROWS);
	term_puts(&term, "DVI Feather Terminal build " BUILD "\r\n\r\n");
	term_puts(&term, "\x1b[1;32mpi@host\x1b[0m:\x1b[1;34m~/cpm\x1b[0m$ ls -l\r\n");
	term_puts(&term, "total 24\r\n");
	term_puts(&term, "drwxr-xr-x 2 pi pi 4096 Oct 28 13:58 \x1b[1;34mdisks\x1b[0m\r\n");
	term_puts(&term, "-rw-r--r-- 1 pi pi 8192 Oct 28 13:59 notes.txt\r\n");
	term_puts(&term, "-rwxr-xr-x 1 pi pi 1932 Oct 28 14:01 \x1b[1;32mrun.sh\x1b[0m\r\n\r\n");
	term_puts(&term, "\x1b[1;32mpi@host\x1b[0m:\x1b[1;34m~/cpm\x1b[0m$ ");
	terminal_status();
}

// Each setting's row: name, choices (the default marked), notes from the
// guide's page. page: SETTINGS_PAGE_*.
static void settings_table(const char *name, unsigned page) {
	settings_t d;
	settings_defaults(&d);
	FILE *f = open_out(name);
	fprintf(f, "| Setting | Choices (default in bold) |\n|---|---|\n");
	for (unsigned i = 0; i < SETTINGS_FIELDS; ++i) {
		const settings_field_t *field = &settings_fields[i];
		if (field->page != page) {
			continue;
		}
		fprintf(f, "| %s | ", field->name);
		uint8_t def = *settings_field(&d, i);
		for (unsigned v = 0; v < field->n_choices; ++v) {
			const char *fmt = field->values[v] == def ? "**%s**" : "%s";
			fprintf(f, v ? ", " : "");
			fprintf(f, fmt, field->labels[v]);
		}
		fprintf(f, " |\n");
	}
	fclose(f);
}

// SET-UP's main rows in their order (the table in the order they appear).
static void setup_rows_table(void) {
	settings_t d;
	settings_defaults(&d);
	FILE *f = open_out("settings-main.md");
	fprintf(f, "| Setting | Choices (default in bold) |\n|---|---|\n");
	for (unsigned row = 0; row < SETTINGS_MAIN_FIELDS; ++row) {
		unsigned i = settings_main_field(row);
		const settings_field_t *field = &settings_fields[i];
		fprintf(f, "| %s | ", field->name);
		uint8_t def = *settings_field(&d, i);
		for (unsigned v = 0; v < field->n_choices; ++v) {
			fprintf(f, v ? ", " : "");
			fprintf(f, field->values[v] == def ? "**%s**" : "%s", field->labels[v]);
		}
		fprintf(f, " |\n");
	}
	fclose(f);
}

static void shot_demo(unsigned width, unsigned cols, const char *name) {
	raster_set_width(width);
	char mode[24];
	snprintf(mode, sizeof mode, "%ux600p60", width);
	char status[SCREEN_MAX_COLS + 1];
	snprintf(status, sizeof status, " demo | %u x 24 | build %s | any key: back to SET-UP", cols, BUILD);
	demo_draw(&screen, cols, mode, raster_font_name(raster_layout(cols)), status);
	write_ppm(name, false);
}

static void shot_setup(void) {
	raster_set_width(RASTER_WIDTH_1024);
	start_terminal(80);
	setup_t u;
	setup_open(&u, &settings);
	u.machine_menu = true;
	while (settings_main_field(u.item) != offsetof(settings_t, display)) {
		setup_input(&u, 0x1b, 0); // Down: ESC [ B
		setup_input(&u, '[', 0);
		setup_input(&u, 'B', 0);
	}
	setup_draw(&u, &screen, SETUP_TOP, "DVI Terminal build " BUILD, PALETTE_TEXT, PALETTE_BACKGROUND,
		colors.status_bg);
	write_ppm("setup.ppm", false);
}

static void shot_clockpanel(void) {
	raster_set_width(RASTER_WIDTH_1024);
	start_terminal(80);
	clockpanel_t p;
	clockpanel_open(&p, &settings, sample_time(), true, true);
	for (unsigned r = 0; r < SETUP_ROWS; ++r) {
		screen_clear_line(&screen, SETUP_TOP + r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	clockpanel_draw(&p, &screen, SETUP_TOP + SETUP_ROWS - CLOCKPANEL_ROWS, PALETTE_TEXT, PALETTE_BACKGROUND,
		colors.status_bg);
	write_ppm("clockpanel.ppm", false);
}

static void shot_terminal(const char *name, unsigned hue, bool mono) {
	raster_set_width(RASTER_WIDTH_1024);
	settings.colors = (uint8_t)hue;
	settings.palette = mono;
	start_terminal(80);
	write_ppm(name, true);
	settings_defaults(&settings);
}

// main.c's display_prompt, as the first boot in a new Display mode shows it.
static void shot_display_prompt(void) {
	raster_set_width(RASTER_WIDTH_800);
	colors = settings_colors(&settings);
	screen_init(&screen, 80, PALETTE_TEXT, PALETTE_BACKGROUND);
	const char *lines[] = {"Display: 800x600", "", "Press any key to keep it.", "Back to 1024x600 in 12 s"};
	const unsigned row = TERM_ROWS / 2 - 2;
	for (unsigned i = 0; i < 4; ++i) {
		screen_puts(&screen, (80 - (unsigned)strlen(lines[i])) / 2, row + i, lines[i],
			i ? PALETTE_TEXT : PALETTE_BOLD_TEXT, PALETTE_BACKGROUND);
	}
	write_ppm("display-prompt.ppm", false);
}

// The 256 glyph codes as a 16 x 16 table, at cols (the layout's font).
static void shot_charset(unsigned width, unsigned cols, const char *name) {
	raster_set_width(width);
	colors = settings_colors(&settings);
	screen_init(&screen, cols, PALETTE_TEXT, PALETTE_BACKGROUND);
	static const char hex[] = "0123456789ABCDEF";
	const unsigned left = 6, step = cols > 80 ? 4 : 3, top = 2;
	char title[SCREEN_MAX_COLS + 1];
	snprintf(title, sizeof title, "Glyph codes: %s", raster_font_name(raster_layout(cols)));
	screen_puts(&screen, left, 0, title, PALETTE_BOLD_TEXT, PALETTE_BACKGROUND);
	for (unsigned c = 0; c < 16; ++c) {
		screen_put(&screen, left + 2 + c * step, top, (uint8_t)hex[c], PALETTE_DIM, PALETTE_BACKGROUND);
	}
	for (unsigned r = 0; r < 16; ++r) {
		screen_put(&screen, left - 1, top + 1 + r, (uint8_t)hex[r], PALETTE_DIM, PALETTE_BACKGROUND);
		screen_put(&screen, left, top + 1 + r, 'x', PALETTE_DIM, PALETTE_BACKGROUND);
		for (unsigned c = 0; c < 16; ++c) {
			screen_put(&screen, left + 2 + c * step, top + 1 + r, (uint8_t)(r * 16 + c), PALETTE_TEXT,
				PALETTE_BACKGROUND);
		}
	}
	write_ppm(name, false);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: docgen OUTDIR\n");
		return 2;
	}
	out_dir = argv[1];
	settings_defaults(&settings);
	setup_rows_table();
	settings_table("settings-panel.md", SETTINGS_PAGE_CLOCK);
	shot_demo(RASTER_WIDTH_1024, 80, "demo-1024-80.ppm");
	shot_demo(RASTER_WIDTH_1024, 132, "demo-1024-132.ppm");
	shot_demo(RASTER_WIDTH_800, 80, "demo-800-80.ppm");
	shot_demo(RASTER_WIDTH_800, 132, "demo-800-132.ppm");
	shot_terminal("terminal.ppm", 0, false);
	shot_terminal("terminal-green.ppm", 1, true);
	shot_terminal("terminal-amber.ppm", 2, true);
	shot_setup();
	shot_clockpanel();
	shot_display_prompt();
	shot_charset(RASTER_WIDTH_1024, 80, "charset-80.ppm");
	shot_charset(RASTER_WIDTH_1024, 132, "charset-132.ppm");
	return 0;
}
