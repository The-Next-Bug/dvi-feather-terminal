#include "demo.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "palette.h"

// Palette indices.
#define BLACK PALETTE_BLACK
#define GREY PALETTE_TEXT
#define DIM PALETTE_DIM
#define WHITE PALETTE_BOLD_TEXT
#define TITLE_BG PALETTE_BLUE

static const char *const names[8] = {
	"Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White",
};

// ANSI colour n (0-7: bit 0 red, bit 1 green, bit 2 blue), normal or bright.
static uint8_t normal(unsigned n) {
	return (uint8_t)n;
}

static uint8_t bright(unsigned n) {
	return (uint8_t)(n + 8);
}

// A hue wheel for the rainbow rows: red, yellow, green, cyan, blue, magenta.
static uint8_t rainbow(unsigned i, int is_bright) {
	static const uint8_t hues[6] = { 1, 3, 2, 6, 4, 5 };
	return is_bright ? bright(hues[i % 6]) : normal(hues[i % 6]);
}

static void fill_row(screen_t *s, unsigned row, const char *text, unsigned offset, int bright_hues) {
	size_t len = strlen(text);
	for (unsigned c = 0; c < s->cols; ++c)
		screen_put(s, c, row, (uint8_t)text[(c + offset) % len],
			rainbow(c + offset, bright_hues), BLACK);
}

static void box(screen_t *s, unsigned col, unsigned row, unsigned w, unsigned h, int dbl, uint8_t fg) {
	uint8_t tl = dbl ? GLYPH_DOUBLE_DR : GLYPH_DEC_GRAPHICS('l');
	uint8_t tr = dbl ? GLYPH_DOUBLE_DL : GLYPH_DEC_GRAPHICS('k');
	uint8_t bl = dbl ? GLYPH_DOUBLE_UR : GLYPH_DEC_GRAPHICS('m');
	uint8_t br = dbl ? GLYPH_DOUBLE_UL : GLYPH_DEC_GRAPHICS('j');
	uint8_t hz = dbl ? GLYPH_DOUBLE_H : GLYPH_DEC_GRAPHICS('q');
	uint8_t vt = dbl ? GLYPH_DOUBLE_V : GLYPH_DEC_GRAPHICS('x');
	screen_put(s, col, row, tl, fg, BLACK);
	screen_put(s, col + w - 1, row, tr, fg, BLACK);
	screen_put(s, col, row + h - 1, bl, fg, BLACK);
	screen_put(s, col + w - 1, row + h - 1, br, fg, BLACK);
	for (unsigned x = 1; x + 1 < w; ++x) {
		screen_put(s, col + x, row, hz, fg, BLACK);
		screen_put(s, col + x, row + h - 1, hz, fg, BLACK);
	}
	for (unsigned y = 1; y + 1 < h; ++y) {
		screen_put(s, col, row + y, vt, fg, BLACK);
		screen_put(s, col + w - 1, row + y, vt, fg, BLACK);
	}
}

// "Label   Black Red Green ..." with each name drawn by style().
static void colour_row(screen_t *s, unsigned row, const char *label, int style) {
	screen_puts(s, 1, row, label, GREY, BLACK);
	unsigned col = 10, step = s->cols > 80 ? 14 : 9;
	for (unsigned n = 0; n < 8; ++n, col += step) {
		uint8_t fg, bg;
		switch (style) {
		case 0: fg = normal(n); bg = n == 0 ? DIM : BLACK; break;
		case 1: fg = bright(n); bg = BLACK; break;
		case 2: fg = BLACK; bg = normal(n); break;
		default: fg = BLACK; bg = bright(n); break;
		}
		char name[16];
		snprintf(name, sizeof name, " %-7s ", names[n]);
		screen_puts(s, col, row, name, fg, bg);
	}
}

void demo_draw(screen_t *s, unsigned cols, const char *font, const char *status) {
	screen_init(s, cols, GREY, BLACK);
	const unsigned w = s->cols;
	char text[SCREEN_MAX_COLS + 1];

	// Title bar.
	screen_clear_line(s, 0, WHITE, TITLE_BG);
	snprintf(text, sizeof text, " DVI Feather Terminal - %u x 24 demo", w);
	screen_puts(s, 0, 0, text, WHITE, TITLE_BG);
	screen_puts(s, w - 12, 0, "800x600p60", WHITE, TITLE_BG);

	// Column ruler: tens, then units coloured per decade.
	for (unsigned c = 0; c < w; ++c) {
		unsigned n = c + 1;
		if (n % 10 == 0)
			screen_put(s, c, 2, (uint8_t)('0' + n / 10 % 10), WHITE, BLACK);
		screen_put(s, c, 3, (uint8_t)('0' + n % 10), n / 10 % 2 ? bright(6) : bright(3), BLACK);
	}

	colour_row(s, 5, "Normal", 0);
	colour_row(s, 6, "Bright", 1);
	colour_row(s, 7, "Reverse", 2);
	colour_row(s, 8, "Rev+Brt", 3);

	fill_row(s, 10, "The quick brown fox jumps over the lazy dog. ", 0, 0);
	fill_row(s, 11, "Pack my box with five dozen liquor jugs! ", 3, 1);
	fill_row(s, 12, "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ", 0, 1);

	// Line drawing and Latin-1 in two boxes side by side.
	unsigned half = w / 2;
	box(s, 0, 14, half, 6, 0, bright(6));
	box(s, half, 14, w - half, 6, 1, bright(3));
	screen_puts(s, 2, 15, "DEC Special Graphics", WHITE, BLACK);
	for (unsigned i = 0; i < 31; ++i)
		screen_put(s, 2 + i, 16, GLYPH_DEC_GRAPHICS(0x60 + i), GREY, BLACK);
	screen_puts(s, 2, 17, "Latin-1: \xe0\xe9\xee\xf5\xfc \xc6\xd8\xc5 \xdf \xa3 \xa9 \xb1 \xbd \xbf",
		GREY, BLACK);
	snprintf(text, sizeof text, "%u columns x 24 rows", w);
	screen_puts(s, 2, 18, text, bright(2), BLACK);

	screen_puts(s, half + 2, 15, "Shades and blocks", WHITE, BLACK);
	for (unsigned i = 0; i < 9; ++i)
		screen_put(s, half + 2 + 2 * i, 16, (uint8_t)(GLYPH_SHADE_LIGHT + i), bright(5), BLACK);
	screen_puts(s, half + 2, 17, "Font: ", GREY, BLACK);
	screen_puts(s, half + 8, 17, font, bright(3), BLACK);

	// The 240 colours above the ANSI ones (cube and grey ramp) as
	// background swatches, as many as fit, evenly spaced.
	screen_puts(s, 1, 21, "xterm", GREY, BLACK);
	unsigned swatches = w - 8;
	for (unsigned c = 0; c < swatches; ++c)
		screen_put(s, 7 + c, 21, ' ', BLACK, (uint8_t)(16 + c * 240 / swatches));

	screen_puts(s, 1, 23, "Switches between 80 and 132 columns every 5 s; any key ends the demo.",
		DIM, BLACK);

	screen_clear_line(s, SCREEN_ROWS - 1, WHITE, TITLE_BG);
	screen_puts(s, 0, SCREEN_ROWS - 1, status, WHITE, TITLE_BG);
}
