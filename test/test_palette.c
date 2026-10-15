#include <stdbool.h>

#include "palette.h"
#include "test.h"

static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
	return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

// The hue's channels at level (of 255), as palette.c's ramp makes them.
static uint16_t ramp(unsigned r, unsigned g, unsigned b, unsigned level) {
	return rgb565(r * level / 255, g * level / 255, b * level / 255);
}

static unsigned green_of(uint16_t px) {
	return px >> 5 & 63;
}

static void test_xterm_rgb(void) {
	uint8_t c[3];
	palette_xterm_rgb(16, c);
	CHECK(c[0] == 0 && c[1] == 0 && c[2] == 0);
	palette_xterm_rgb(21, c); // 0, 0, 5
	CHECK(c[0] == 0 && c[1] == 0 && c[2] == 255);
	palette_xterm_rgb(67, c); // 1, 2, 3
	CHECK(c[0] == 95 && c[1] == 135 && c[2] == 175);
	palette_xterm_rgb(231, c);
	CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255);
	palette_xterm_rgb(232, c);
	CHECK(c[0] == 8 && c[1] == 8 && c[2] == 8);
	palette_xterm_rgb(255, c);
	CHECK(c[0] == 238 && c[1] == 238 && c[2] == 238);
}

// White in color: our ANSI colors (the old RGB222 levels), then xterm's.
static void test_white_color(void) {
	uint16_t t[PALETTE_SIZE];
	palette_build(t, PALETTE_HUE_WHITE, false);
	CHECK_EQ(t[PALETTE_BLACK], 0);
	CHECK_EQ(t[1], rgb565(170, 0, 0));
	CHECK_EQ(t[PALETTE_BLUE], rgb565(0, 0, 170));
	CHECK_EQ(t[PALETTE_TEXT], rgb565(170, 170, 170));
	CHECK_EQ(t[PALETTE_DIM], rgb565(85, 85, 85));
	CHECK_EQ(t[11], rgb565(255, 255, 0));
	CHECK_EQ(t[PALETTE_BOLD_TEXT], 0xffff);
	CHECK_EQ(t[16], 0);
	CHECK_EQ(t[196], rgb565(255, 0, 0));
	CHECK_EQ(t[67], rgb565(95, 135, 175));
	CHECK_EQ(t[244], rgb565(128, 128, 128));
	// Out-of-range hues are white.
	uint16_t u[PALETTE_SIZE];
	palette_build(u, 99, false);
	for (unsigned i = 0; i < PALETTE_SIZE; ++i) {
		CHECK_EQ(u[i], t[i]);
	}
}

// A phosphor in color: the ANSI colors are a ramp in its hue, by band;
// the rest are xterm's colors.
static void test_phosphor_color(void) {
	uint16_t t[PALETTE_SIZE], white[PALETTE_SIZE];
	palette_build(t, PALETTE_HUE_GREEN, false);
	palette_build(white, PALETTE_HUE_WHITE, false);
	CHECK_EQ(t[PALETTE_BLACK], 0);
	CHECK_EQ(t[PALETTE_TEXT], ramp(0x33, 0xff, 0x33, 170));
	CHECK_EQ(t[PALETTE_BOLD_TEXT], rgb565(0x33, 0xff, 0x33));
	// Bands: 0 < 8 < normal colors (blue dimmest, yellow brightest) < 7 <
	// bright colors (12 dimmest, 11 brightest) < 15.
	static const unsigned order[16] = {0, 8, 4, 1, 5, 2, 6, 3, 7, 12, 9, 13, 10, 14, 11, 15};
	for (unsigned i = 1; i < 16; ++i) {
		CHECK(green_of(t[order[i]]) > green_of(t[order[i - 1]]));
	}
	// Nothing but black is darker than a third.
	CHECK(green_of(t[PALETTE_BLUE]) > 63 / 3);
	for (unsigned i = 16; i < PALETTE_SIZE; ++i) {
		CHECK_EQ(t[i], white[i]);
	}

	palette_build(t, PALETTE_HUE_AMBER, false);
	CHECK_EQ(t[PALETTE_BOLD_TEXT], rgb565(0xff, 0xb0, 0x00));
	CHECK_EQ(t[196], rgb565(255, 0, 0));
}

// Mono: every entry is a level of the hue; the ANSI ones as in the phosphor
// color palettes.
static void test_mono(void) {
	uint16_t t[PALETTE_SIZE], phosphor[PALETTE_SIZE];
	palette_build(t, PALETTE_HUE_WHITE, true);
	for (unsigned i = 0; i < PALETTE_SIZE; ++i) {
		unsigned r = t[i] >> 11, g = t[i] >> 5 & 63, b = t[i] & 31;
		CHECK(r == b && g >> 1 == r); // gray
	}
	CHECK_EQ(t[PALETTE_TEXT], ramp(255, 255, 255, 170));
	CHECK_EQ(t[PALETTE_BOLD_TEXT], 0xffff);
	CHECK_EQ(t[16], 0);         // black
	CHECK_EQ(t[231], 0xffff);   // white
	CHECK(green_of(t[17]) > 63 / 3); // dark blue: lifted to the floor
	CHECK(green_of(t[46]) > green_of(t[21])); // green brighter than blue

	palette_build(t, PALETTE_HUE_BLUE, true);
	palette_build(phosphor, PALETTE_HUE_BLUE, false);
	for (unsigned i = 0; i < 16; ++i) {
		CHECK_EQ(t[i], phosphor[i]);
	}
	CHECK_EQ(t[231], rgb565(0x33, 0x99, 0xff));
}

static void test_nearest(void) {
	CHECK_EQ(palette_nearest(0, 0, 0), 16);
	CHECK_EQ(palette_nearest(255, 255, 255), 231);
	CHECK_EQ(palette_nearest(255, 0, 0), 196);
	CHECK_EQ(palette_nearest(95, 135, 175), 67);
	CHECK_EQ(palette_nearest(100, 130, 180), 67);
	CHECK_EQ(palette_nearest(128, 128, 128), 244);
	CHECK_EQ(palette_nearest(10, 10, 10), 232);
	CHECK_EQ(palette_nearest(240, 240, 240), 255);
	// Every xterm color above the ANSI ones maps to itself.
	for (unsigned i = 16; i < PALETTE_SIZE; ++i) {
		uint8_t c[3];
		palette_xterm_rgb(i, c);
		uint8_t n = palette_nearest(c[0], c[1], c[2]);
		uint8_t back[3];
		palette_xterm_rgb(n, back);
		CHECK(back[0] == c[0] && back[1] == c[1] && back[2] == c[2]);
	}
}

int main(void) {
	RUN(test_xterm_rgb);
	RUN(test_white_color);
	RUN(test_phosphor_color);
	RUN(test_mono);
	RUN(test_nearest);
	return tests_done();
}
