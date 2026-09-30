#include "splash.h"

#include <string.h>

// One image row of levels; static to keep it off the (small) stack.
static uint8_t levels[RASTER_ACTIVE_WIDTH];

typedef struct {
	const uint8_t *data, *end;
	unsigned level, left; // the current run
} runs_t;

static bool next_level(runs_t *r, uint8_t *level) {
	if (!r->left) {
		if (r->data == r->end)
			return false;
		r->level = *r->data >> 6;
		r->left = (*r->data++ & 0x3fu) + 1;
	}
	--r->left;
	*level = (uint8_t)r->level;
	return true;
}

splash_tint_t splash_tint(const splash_image_t *img, uint8_t fg, bool mono) {
	splash_tint_t t;
	unsigned level[RASTER_PLANES], max = 0;
	for (int p = 0; p < RASTER_PLANES; ++p) {
		level[p] = fg >> 2 * p & 3;
		max = level[p] > max ? level[p] : max;
	}
	bool grey = level[0] == level[1] && level[1] == level[2] && !mono;
	for (int p = 0; p < RASTER_PLANES; ++p) {
		if (grey)
			t.shift[p] = img->planes >> p & 1 ? 0 : SPLASH_PLANE_OFF;
		else
			t.shift[p] = level[p] ? (uint8_t)(max - level[p]) : SPLASH_PLANE_OFF;
	}
	return t;
}

void splash_shift_map(unsigned shift, uint8_t map[256]) {
	for (unsigned b = 0; b < 256; ++b) {
		unsigned out = 0;
		for (unsigned i = 0; i < 8; i += 2) { // four 2-bit levels
			unsigned v = b >> i & 3;
			out |= (v > shift ? v - shift : 0) << i;
		}
		map[b] = (uint8_t)out;
	}
}

bool splash_unpack(splash_row_t *rows, const splash_image_t *img) {
	memset(rows, 0, SPLASH_HEIGHT * sizeof *rows);
	unsigned w = img->width, h = img->height;
	if (w % 8 || w > RASTER_ACTIVE_WIDTH || h > SPLASH_HEIGHT)
		return false;
	// Centred on whole groups: each group is inside or outside the image.
	unsigned x0 = (RASTER_ACTIVE_WIDTH - w) / 2 / 8 * 8, y0 = (SPLASH_HEIGHT - h) / 2;

	runs_t r = { img->data, img->data + img->size, 0, 0 };
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			if (!next_level(&r, &levels[x])) {
				memset(rows, 0, SPLASH_HEIGHT * sizeof *rows);
				return false;
			}
		}
		splash_row_t *row = &rows[y0 + y];
		for (unsigned x = 0; x < w; x += 8) {
			unsigned lo = 3, hi = 0;
			for (unsigned i = 0; i < 8; ++i) {
				unsigned v = levels[x + i];
				lo = v < lo ? v : lo;
				hi = v > hi ? v : hi;
			}
			// The nearer of the two levels, ties going to lo (as in raster.c);
			// logo_gen.py leaves at most two per group, so this is exact.
			unsigned bits = 0;
			for (unsigned i = 0; i < 8; ++i)
				bits |= (2 * levels[x + i] > lo + hi ? 1u : 0u) << i;
			unsigned g = (x0 + x) / 8;
			row->bits[g] = (uint8_t)bits;
			row->colour[g / 8] |= (uint32_t)(hi | lo << 2) << 4 * (g % 8);
		}
	}
	if (r.left || r.data != r.end) {
		memset(rows, 0, SPLASH_HEIGHT * sizeof *rows);
		return false;
	}
	return true;
}
