#include <string.h>

#include "splash.h"
#include "test.h"

static splash_row_t rows[SPLASH_HEIGHT];

// The level pair of group g on a row: set (hi) and clear (lo).
static unsigned group_hi(const splash_row_t *row, unsigned g) {
	return row->colour[g / 8] >> 4 * (g % 8) & 3;
}

static unsigned group_lo(const splash_row_t *row, unsigned g) {
	return row->colour[g / 8] >> (4 * (g % 8) + 2) & 3;
}

static bool row_black(const splash_row_t *row) {
	for (unsigned i = 0; i < sizeof row->bits; ++i)
		if (row->bits[i])
			return false;
	for (unsigned i = 0; i < RASTER_COLOUR_WORDS; ++i)
		if (row->colour[i])
			return false;
	return true;
}

// A 16x2 image: row 0 is levels 0 0 0 3 3 3 3 3 | 2 x 8; row 1 all 1.
static const uint8_t small_data[] = {
	0 << 6 | 2, 3 << 6 | 4, 2 << 6 | 7,
	1 << 6 | 15,
};
static const splash_image_t small = { 16, 2, 0x2, small_data, sizeof small_data };

static void test_small_image_centred(void) {
	CHECK(splash_unpack(rows, &small));
	unsigned x0 = (RASTER_ACTIVE_WIDTH - 16) / 2, y0 = (SPLASH_HEIGHT - 2) / 2;
	unsigned g = x0 / 8;
	CHECK_EQ(x0 % 8, 0);
	CHECK(row_black(&rows[y0 - 1]));
	CHECK(row_black(&rows[y0 + 2]));
	const splash_row_t *r0 = &rows[y0], *r1 = &rows[y0 + 1];
	// Two levels: pixels 3-7 take the set level.
	CHECK_EQ(r0->bits[g], 0xf8);
	CHECK_EQ(group_hi(r0, g), 3);
	CHECK_EQ(group_lo(r0, g), 0);
	// One level: set and clear are the same, no bits.
	CHECK_EQ(r0->bits[g + 1], 0);
	CHECK_EQ(group_hi(r0, g + 1), 2);
	CHECK_EQ(group_lo(r0, g + 1), 2);
	CHECK_EQ(group_hi(r1, g), 1);
	CHECK_EQ(group_lo(r1, g + 1), 1);
	// Outside the image: black.
	CHECK_EQ(r0->bits[g - 1], 0);
	CHECK_EQ(group_hi(r0, g - 1), 0);
	CHECK_EQ(group_hi(r0, g + 2), 0);
	CHECK_EQ(r0->bits[RASTER_BYTES - 1], 0);
}

static void test_bad_images_leave_black(void) {
	splash_image_t img = small;
	img.size = sizeof small_data - 1; // too little data
	CHECK(!splash_unpack(rows, &img));
	CHECK(row_black(&rows[SPLASH_HEIGHT / 2]));

	static const uint8_t extra[] = { 0 << 6 | 2, 3 << 6 | 4, 2 << 6 | 7, 1 << 6 | 15, 0 };
	img.data = extra;
	img.size = sizeof extra; // data left over
	CHECK(!splash_unpack(rows, &img));
	CHECK(row_black(&rows[SPLASH_HEIGHT / 2]));

	img = small;
	img.width = 12; // not whole groups
	CHECK(!splash_unpack(rows, &img));
	img = small;
	img.height = SPLASH_HEIGHT + 1;
	CHECK(!splash_unpack(rows, &img));
}

// The logo built from LOGO_IMAGE unpacks and has something to show.
static void test_built_in_logo(void) {
	CHECK(splash_logo.planes != 0);
	CHECK(splash_unpack(rows, &splash_logo));
	unsigned lit = 0;
	for (unsigned y = 0; y < SPLASH_HEIGHT; ++y)
		for (unsigned i = 0; i < RASTER_COLOUR_WORDS; ++i)
			lit += rows[y].colour[i] != 0;
	CHECK(lit > SPLASH_HEIGHT);
}

int main(void) {
	RUN(test_small_image_centred);
	RUN(test_bad_images_leave_black);
	RUN(test_built_in_logo);
	return tests_done();
}
