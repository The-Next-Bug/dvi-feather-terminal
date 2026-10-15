#include <string.h>

#include "splash.h"
#include "test.h"

static uint16_t px[RASTER_ACTIVE_WIDTH];

// Level colors for the tests: level l as the pixel 0x10 + l (black is 0).
static const uint16_t colors[4] = { 0x10, 0x11, 0x12, 0x13 };

static bool row_black(void) {
	for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
		if (px[x]) {
			return false;
		}
	}
	return true;
}

// A 16x2 image: row 0 is levels 0 0 0 3 3 3 3 3 | 2 x 8; row 1 all 1. The
// 2-run crosses into row 1 (runs do not stop at row ends).
static const uint8_t small_data[] = {
	0 << 6 | 2, 3 << 6 | 4, 2 << 6 | 7,
	1 << 6 | 15,
};
static const splash_image_t small = { 16, 2, 0x2, small_data, sizeof small_data };

static void test_small_image_centered(void) {
	CHECK(splash_check(&small));
	splash_decoder_t d = splash_decoder_init(&small, small_data);
	unsigned x0 = (RASTER_ACTIVE_WIDTH - 16) / 2, y0 = (SPLASH_HEIGHT - 2) / 2;
	for (unsigned y = 0; y < SPLASH_HEIGHT; ++y) {
		memset(px, 0xa5, sizeof px); // every pixel must be written
		splash_compose(px, &d, y, colors);
		if (y == y0) {
			for (unsigned i = 0; i < 16; ++i) {
				CHECK_EQ(px[x0 + i], i < 3 ? 0x10 : i < 8 ? 0x13 : 0x12);
			}
			CHECK_EQ(px[x0 - 1], 0);
			CHECK_EQ(px[x0 + 16], 0);
		} else if (y == y0 + 1) {
			for (unsigned i = 0; i < 16; ++i) {
				CHECK_EQ(px[x0 + i], 0x11);
			}
		} else {
			CHECK(row_black());
		}
	}
	CHECK(d.data == d.end && d.left == 0);
}

// An odd-width margin rounds the start down to an even pixel.
static void test_start_even(void) {
	static const uint8_t data[] = { 3 << 6 | 1 };
	const splash_image_t img = { 2, 1, 0x2, data, sizeof data };
	splash_decoder_t d = splash_decoder_init(&img, data);
	CHECK_EQ(d.x0 % 2, 0);
	CHECK_EQ(d.x0, (RASTER_ACTIVE_WIDTH - 2) / 2 / 2 * 2);
}

static void test_bad_images_rejected(void) {
	splash_image_t img = small;
	img.size = sizeof small_data - 1; // too little data
	CHECK(!splash_check(&img));
	static const uint8_t extra[] = { 0 << 6 | 2, 3 << 6 | 4, 2 << 6 | 7, 1 << 6 | 15, 0 };
	img.data = extra;
	img.size = sizeof extra; // data left over
	CHECK(!splash_check(&img));
	img = small;
	img.width = 15; // odd
	CHECK(!splash_check(&img));
	img = small;
	img.height = SPLASH_HEIGHT + 1;
	CHECK(!splash_check(&img));

	// Short data while decoding (an unchecked image) leaves the rest black
	// instead of reading past the end.
	img = small;
	img.size = 2;
	splash_decoder_t d = splash_decoder_init(&img, small_data);
	unsigned y0 = (SPLASH_HEIGHT - 2) / 2, x0 = (RASTER_ACTIVE_WIDTH - 16) / 2;
	for (unsigned y = 0; y <= y0; ++y) {
		splash_compose(px, &d, y, colors);
	}
	CHECK_EQ(px[x0 + 7], 0x13);
	CHECK_EQ(px[x0 + 8], 0);
}

// The logo built from LOGO_IMAGE checks out, decodes to the end of its data
// and has something to show.
static void test_built_in_logo(void) {
	CHECK(splash_logo.planes != 0);
	CHECK(splash_check(&splash_logo));
	splash_decoder_t d = splash_decoder_init(&splash_logo, splash_logo.data);
	unsigned lit = 0;
	for (unsigned y = 0; y < SPLASH_HEIGHT; ++y) {
		splash_compose(px, &d, y, colors);
		for (unsigned x = 0; x < RASTER_ACTIVE_WIDTH; ++x) {
			lit += px[x] > 0x10;
		}
	}
	CHECK(d.data == d.end && d.left == 0);
	CHECK(lit > SPLASH_HEIGHT);
}

// The tint follows the text color's hue: its brightest plane at the
// logo's levels, the others lowered in proportion; gray keeps the logo's
// own planes.
static void test_tint(void) {
	const splash_image_t img = {.planes = 0x2}; // green only
	splash_tint_t t = splash_tint(&img, RGB222(2, 2, 2), false); // white
	CHECK(t.shift[0] == SPLASH_PLANE_OFF && t.shift[1] == 0 && t.shift[2] == SPLASH_PLANE_OFF);
	t = splash_tint(&img, RGB222(2, 2, 2), true); // white, mono: gray
	CHECK(t.shift[0] == 0 && t.shift[1] == 0 && t.shift[2] == 0);
	t = splash_tint(&img, RGB222(0, 2, 0), false); // green
	CHECK(t.shift[0] == SPLASH_PLANE_OFF && t.shift[1] == 0 && t.shift[2] == SPLASH_PLANE_OFF);
	t = splash_tint(&img, RGB222(2, 1, 0), true); // amber (mono or not): red full, green one down
	CHECK(t.shift[0] == SPLASH_PLANE_OFF && t.shift[1] == 1 && t.shift[2] == 0);
	t = splash_tint(&img, RGB222(0, 1, 3), false); // blue: blue full, green two down
	CHECK(t.shift[0] == 0 && t.shift[1] == 2 && t.shift[2] == SPLASH_PLANE_OFF);
}

// Each level's color: per plane, off or the level lowered by the shift.
static void test_tint_color(void) {
	const splash_tint_t green = { { SPLASH_PLANE_OFF, 0, SPLASH_PLANE_OFF } };
	for (unsigned l = 0; l < 4; ++l) {
		CHECK_EQ(splash_tint_color(&green, l), RGB222(0, l, 0));
	}
	const splash_tint_t amber = { { SPLASH_PLANE_OFF, 1, 0 } };
	CHECK_EQ(splash_tint_color(&amber, 3), RGB222(3, 2, 0));
	CHECK_EQ(splash_tint_color(&amber, 1), RGB222(1, 0, 0));
	CHECK_EQ(splash_tint_color(&amber, 0), 0);
	const splash_tint_t gray = { { 0, 0, 0 } };
	CHECK_EQ(splash_tint_color(&gray, 2), RGB222(2, 2, 2));
}

int main(void) {
	RUN(test_small_image_centered);
	RUN(test_start_even);
	RUN(test_bad_images_rejected);
	RUN(test_built_in_logo);
	RUN(test_tint);
	RUN(test_tint_color);
	return tests_done();
}
