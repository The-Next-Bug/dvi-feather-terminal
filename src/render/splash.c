#include "splash.h"

bool splash_check(const splash_image_t *img) {
	if (img->width % 2 || img->width > RASTER_ACTIVE_WIDTH || img->height > SPLASH_HEIGHT)
		return false;
	size_t pixels = 0;
	for (size_t i = 0; i < img->size; ++i)
		pixels += (img->data[i] & 0x3fu) + 1;
	return pixels == (size_t)img->width * img->height;
}

splash_decoder_t splash_decoder_init(const splash_image_t *img, const uint8_t *data) {
	splash_decoder_t d = {
		.data = data,
		.end = data + img->size,
		.width = img->width,
		.height = img->height,
		// Even, so the image starts on a whole word of pixels.
		.x0 = (uint16_t)((RASTER_ACTIVE_WIDTH - img->width) / 4 * 2),
		.y0 = (uint16_t)((SPLASH_HEIGHT - img->height) / 2),
	};
	return d;
}

void splash_compose(uint16_t *px, splash_decoder_t *d, unsigned y, const uint16_t colours[4]) {
	unsigned x = 0;
	if (y >= d->y0 && y < d->y0 + d->height) {
		for (; x < d->x0; ++x)
			px[x] = 0;
		unsigned todo = d->width;
		while (todo) {
			if (!d->left) {
				if (d->data == d->end)
					break; // not for a checked image: the rest stays black
				d->level = *d->data >> 6;
				d->left = (*d->data++ & 0x3fu) + 1;
			}
			unsigned n = d->left < todo ? d->left : todo;
			uint16_t c = colours[d->level];
			for (unsigned i = 0; i < n; ++i)
				px[x++] = c;
			d->left -= n;
			todo -= n;
		}
	}
	for (; x < RASTER_ACTIVE_WIDTH; ++x)
		px[x] = 0;
}

splash_tint_t splash_tint(const splash_image_t *img, uint8_t fg, bool mono) {
	splash_tint_t t;
	unsigned level[3], max = 0;
	for (int p = 0; p < 3; ++p) {
		level[p] = fg >> 2 * p & 3;
		max = level[p] > max ? level[p] : max;
	}
	bool grey = level[0] == level[1] && level[1] == level[2] && !mono;
	for (int p = 0; p < 3; ++p) {
		if (grey)
			t.shift[p] = img->planes >> p & 1 ? 0 : SPLASH_PLANE_OFF;
		else
			t.shift[p] = level[p] ? (uint8_t)(max - level[p]) : SPLASH_PLANE_OFF;
	}
	return t;
}

uint8_t splash_tint_colour(const splash_tint_t *t, unsigned level) {
	unsigned c = 0;
	for (int p = 0; p < 3; ++p) {
		unsigned shift = t->shift[p];
		if (shift != SPLASH_PLANE_OFF && level > shift)
			c |= (level - shift) << 2 * p;
	}
	return (uint8_t)c;
}
