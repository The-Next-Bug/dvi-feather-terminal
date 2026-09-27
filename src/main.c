// DVI serial terminal for the Adafruit Feather RP2040 DVI.
//
// Milestone 1: bring up DVI and draw a static test screen (glyph table,
// colour chart, DEC graphics) plus a small scrolling region and an uptime
// counter to show that core 0 and scrolling work.

#include <stdio.h>

#include "pico/stdlib.h"

#include "font.h"
#include "screen.h"
#include "term_build.h"
#include "video.h"

#define FG RGB222(2, 2, 2)
#define BG RGB222(0, 0, 0)
#define HILITE RGB222(3, 3, 3)
#define TITLE_BG RGB222(0, 0, 2)
#define DIM RGB222(1, 1, 1)

#define SCROLL_TOP 23
#define SCROLL_BOTTOM 27

static screen_t screen;

static void draw_title(void) {
	char text[SCREEN_COLS + 1];
	screen_clear_line(&screen, 0, HILITE, TITLE_BG);
	snprintf(text, sizeof text, " DVI Feather Terminal  build %s  %s  %s",
		TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE);
	screen_puts(&screen, 0, 0, text, HILITE, TITLE_BG);
}

// All 256 glyphs, 16 per row, with hex row/column labels.
static void draw_glyph_table(unsigned col, unsigned row) {
	static const char hex[] = "0123456789ABCDEF";
	screen_puts(&screen, col, row, "Glyphs", HILITE, BG);
	for (unsigned i = 0; i < 16; ++i) {
		screen_put(&screen, col + 3 + 2 * i, row + 1, (uint8_t)hex[i], DIM, BG);
		screen_put(&screen, col, row + 2 + i, (uint8_t)hex[i], DIM, BG);
		screen_put(&screen, col + 1, row + 2 + i, '0', DIM, BG);
		for (unsigned j = 0; j < 16; ++j)
			screen_put(&screen, col + 3 + 2 * j, row + 2 + i, (uint8_t)(i * 16 + j), FG, BG);
	}
}

// All 64 RGB222 colours as 2-cell swatches, 8 per row.
static void draw_colour_chart(unsigned col, unsigned row) {
	screen_puts(&screen, col, row, "RGB222 colours", HILITE, BG);
	for (unsigned c = 0; c < 64; ++c) {
		unsigned x = col + c % 8 * 3, y = row + 1 + c / 8;
		screen_put(&screen, x, y, ' ', BG, (uint8_t)c);
		screen_put(&screen, x + 1, y, ' ', BG, (uint8_t)c);
	}
}

// A box drawn with DEC Special Graphics, holding the rest of that set.
static void draw_dec_graphics(unsigned col, unsigned row) {
	const unsigned w = 34, h = 5;
	uint8_t c = HILITE;
	screen_put(&screen, col, row, GLYPH_DEC_GRAPHICS('l'), c, BG);
	screen_put(&screen, col + w - 1, row, GLYPH_DEC_GRAPHICS('k'), c, BG);
	screen_put(&screen, col, row + h - 1, GLYPH_DEC_GRAPHICS('m'), c, BG);
	screen_put(&screen, col + w - 1, row + h - 1, GLYPH_DEC_GRAPHICS('j'), c, BG);
	for (unsigned x = 1; x < w - 1; ++x) {
		screen_put(&screen, col + x, row, GLYPH_DEC_GRAPHICS('q'), c, BG);
		screen_put(&screen, col + x, row + h - 1, GLYPH_DEC_GRAPHICS('q'), c, BG);
	}
	for (unsigned y = 1; y < h - 1; ++y) {
		screen_put(&screen, col, row + y, GLYPH_DEC_GRAPHICS('x'), c, BG);
		screen_put(&screen, col + w - 1, row + y, GLYPH_DEC_GRAPHICS('x'), c, BG);
	}
	screen_puts(&screen, col + 2, row + 1, "DEC Special Graphics 0x60-0x7E", FG, BG);
	for (unsigned i = 0; i < 31; ++i)
		screen_put(&screen, col + 2 + i, row + 2, GLYPH_DEC_GRAPHICS(0x60 + i), FG, BG);
	screen_puts(&screen, col + 2, row + 3, "Latin-1: \xe0\xe9\xee\xf5\xfc \xc6\xd8\xc5 \xdf \xa3 \xa9 \xb1 \xbd",
		FG, BG);
}

static void draw_test_screen(void) {
	screen_init(&screen, FG, BG);
	draw_title();
	draw_glyph_table(1, 2);
	draw_colour_chart(40, 2);
	draw_dec_graphics(40, 12);
	screen_puts(&screen, 1, SCROLL_TOP - 1, "Scrolling region", HILITE, BG);
	screen_puts(&screen, 1, SCREEN_ROWS - 1, "UART0 115200 8N1 TX=GP0 RX=GP1", DIM, BG);
}

int main(void) {
	video_init();
	// After video_init: the UART baud divisor depends on the new clk_peri.
	stdio_init_all();

	draw_test_screen();
	video_start(&screen);

	char text[SCREEN_COLS + 1];
	unsigned rx_count = 0;
	absolute_time_t next_tick = get_absolute_time();
	for (unsigned tick = 0;;) {
		// Serial check: echo received bytes back and count them on screen.
		int c = getchar_timeout_us(0);
		if (c != PICO_ERROR_TIMEOUT) {
			putchar_raw(c);
			snprintf(text, sizeof text, "rx %6u  last 0x%02x", ++rx_count, c & 0xff);
			screen_puts(&screen, 30, SCREEN_ROWS - 1, text, HILITE, BG);
		}

		if (absolute_time_diff_us(next_tick, get_absolute_time()) < 0)
			continue;
		next_tick = delayed_by_ms(next_tick, 500);
		++tick;

		screen_scroll_up(&screen, SCROLL_TOP, SCROLL_BOTTOM, FG, BG);
		snprintf(text, sizeof text, "scroll test line %u", tick);
		screen_puts(&screen, 1, SCROLL_BOTTOM, text, (uint8_t)(tick % 63 + 1), BG);

		unsigned s = tick / 2;
		snprintf(text, sizeof text, "uptime %02u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
		screen_puts(&screen, SCREEN_COLS - 16, SCREEN_ROWS - 1, text, DIM, BG);

		// Repeat the banner so it is seen whenever the serial port is opened.
		if (tick % 10 == 0)
			printf("DVI Feather Terminal build %s commit %s built %s, uptime %us\r\n",
				TERM_BUILD_ID, TERM_BUILD_COMMIT, TERM_BUILD_DATE, s);
	}
}
