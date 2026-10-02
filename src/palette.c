#include "palette.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// Full-brightness colour of each hue (8-bit channels). White's colour table
// keeps its own ANSI colours; the phosphors (and mono) scale these.
static const uint8_t hues[PALETTE_HUES][3] = {
	{0xff, 0xff, 0xff}, // white (mono: grey)
	{0x33, 0xff, 0x33}, // green phosphor
	{0xff, 0xb0, 0x00}, // amber phosphor
	{0x33, 0x99, 0xff}, // blue phosphor
};

// Our ANSI colours (the RGB222 levels of before: 0, 85, 170, 255).
static const uint8_t ansi[16][3] = {
	{0, 0, 0}, {170, 0, 0}, {0, 170, 0}, {170, 170, 0},
	{0, 0, 170}, {170, 0, 170}, {0, 170, 170}, {170, 170, 170},
	{85, 85, 85}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
	{0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
};

// The ANSI ramp's levels (of 255): black; 8 at ~35%; the normal colours
// 45-63% (blue, red, magenta, green, cyan, yellow); 7 at 2/3, the old text
// level; the bright colours 75-92% in the same order; 15 full, the old bold.
static const uint8_t ansi_levels[16] = {
	0, 124, 142, 160, 115, 133, 151, 170,
	90, 199, 217, 235, 190, 208, 226, 255,
};

// Mono, 16-255: brightness mapped into this range (black stays black), so
// dark colours stay readable.
#define MONO_MIN 90

static const uint8_t cube_levels[6] = {0, 95, 135, 175, 215, 255};

void palette_xterm_rgb(unsigned i, uint8_t rgb[3]) {
	if (i < 16) {
		for (int c = 0; c < 3; ++c)
			rgb[c] = ansi[i][c];
	} else if (i < 232) {
		i -= 16;
		rgb[0] = cube_levels[i / 36];
		rgb[1] = cube_levels[i / 6 % 6];
		rgb[2] = cube_levels[i % 6];
	} else {
		rgb[0] = rgb[1] = rgb[2] = (uint8_t)(8 + 10 * (i - 232));
	}
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
	return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

// The hue at level (0-255).
static uint16_t ramp(const uint8_t hue[3], unsigned level) {
	return rgb565(hue[0] * level / 255, hue[1] * level / 255, hue[2] * level / 255);
}

// Rec. 709 luma of an 8-bit colour, 0-255.
static unsigned luma(const uint8_t rgb[3]) {
	return (54u * rgb[0] + 183u * rgb[1] + 19u * rgb[2]) / 256;
}

void palette_build(uint16_t out[PALETTE_SIZE], unsigned hue, bool mono) {
	if (hue >= PALETTE_HUES)
		hue = PALETTE_HUE_WHITE;
	const uint8_t *h = hues[hue];
	for (unsigned i = 0; i < PALETTE_SIZE; ++i) {
		uint8_t rgb[3];
		palette_xterm_rgb(i, rgb);
		if (i < 16 && (mono || hue != PALETTE_HUE_WHITE)) {
			out[i] = ramp(h, ansi_levels[i]);
		} else if (mono) {
			unsigned y = luma(rgb);
			out[i] = ramp(h, y ? MONO_MIN + y * (255 - MONO_MIN) / 255 : 0);
		} else {
			out[i] = rgb565(rgb[0], rgb[1], rgb[2]);
		}
	}
}

static unsigned dist2(const uint8_t a[3], const uint8_t b[3]) {
	unsigned d = 0;
	for (int c = 0; c < 3; ++c) {
		int x = (int)a[c] - (int)b[c];
		d += (unsigned)(x * x);
	}
	return d;
}

// The nearest of the cube's levels to v.
static unsigned cube_step(uint8_t v) {
	unsigned best = 0;
	for (unsigned s = 1; s < COUNT(cube_levels); ++s) {
		int d = (int)v - cube_levels[s], bd = (int)v - cube_levels[best];
		if (d * d < bd * bd)
			best = s;
	}
	return best;
}

uint8_t palette_nearest(uint8_t r, uint8_t g, uint8_t b) {
	const uint8_t want[3] = {r, g, b};
	unsigned cube = 16 + 36 * cube_step(r) + 6 * cube_step(g) + cube_step(b);
	unsigned avg = (r + g + b) / 3u;
	unsigned k = avg < 8 ? 0 : (avg - 8 + 5) / 10;
	unsigned grey = 232 + (k > 23 ? 23 : k);
	uint8_t c[3], gr[3];
	palette_xterm_rgb(cube, c);
	palette_xterm_rgb(grey, gr);
	return (uint8_t)(dist2(want, gr) < dist2(want, c) ? grey : cube);
}
