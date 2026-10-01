#include <string.h>

#include "crc32.h"
#include "diskimg.h"
#include "lz.h"
#include "test.h"

static uint8_t window[LZ_WINDOW];

typedef struct {
	uint8_t out[300000];
	size_t len;
	size_t max_chunk;
	size_t stop_after; // 0: never stop
} collect_t;

static collect_t got;

static bool collect(void *ctx, const uint8_t *data, size_t len) {
	collect_t *c = ctx;
	if (len > c->max_chunk)
		c->max_chunk = len;
	if (c->len + len > sizeof c->out)
		return false;
	memcpy(c->out + c->len, data, len);
	c->len += len;
	return !c->stop_after || c->len < c->stop_after;
}

static bool decode(const uint8_t *src, size_t len, size_t out_size) {
	memset(&got, 0, sizeof got);
	return lz_decode(src, len, out_size, window, collect, &got);
}

static void test_literals_only(void) {
	const uint8_t src[] = {0x50, 'h', 'e', 'l', 'l', 'o'};
	CHECK(decode(src, sizeof src, 5));
	CHECK_EQ(got.len, 5);
	CHECK(memcmp(got.out, "hello", 5) == 0);
}

static void test_overlapping_match(void) {
	// "ab", then a 10-byte match at offset 2, then one literal.
	const uint8_t src[] = {0x26, 'a', 'b', 2, 0, 0x10, 'c'};
	CHECK(decode(src, sizeof src, 13));
	CHECK(memcmp(got.out, "ababababababc", 13) == 0);
}

static void test_long_lengths(void) {
	// 20 literals (15 + 5) and a run of 300 (4 + 15 + 255 + 26) at offset 1.
	uint8_t src[64];
	size_t n = 0;
	src[n++] = 0xff;
	src[n++] = 5;
	for (int i = 0; i < 20; ++i)
		src[n++] = (uint8_t)('A' + i);
	src[n++] = 1;
	src[n++] = 0;
	src[n++] = 255;
	src[n++] = 26;
	src[n++] = 0x00; // final sequence: no literals
	CHECK(decode(src, n, 320));
	CHECK_EQ(got.len, 320);
	CHECK_EQ(got.out[19], 'T');
	CHECK_EQ(got.out[20], 'T');
	CHECK_EQ(got.out[319], 'T');
}

static void test_corrupt_data(void) {
	const uint8_t truncated[] = {0x50, 'h', 'e'};
	CHECK(!decode(truncated, sizeof truncated, 5));
	const uint8_t offset_zero[] = {0x10, 'a', 0, 0, 0x00};
	CHECK(!decode(offset_zero, sizeof offset_zero, 6));
	const uint8_t before_start[] = {0x10, 'a', 2, 0, 0x00};
	CHECK(!decode(before_start, sizeof before_start, 6));
	const uint8_t src[] = {0x50, 'h', 'e', 'l', 'l', 'o'};
	CHECK(!decode(src, sizeof src, 4)); // more output than expected
	CHECK(!decode(src, sizeof src, 6)); // less
}

static void test_embedded_images(void) {
	CHECK(diskimg_count >= 1);
	for (unsigned i = 0; i < diskimg_count; ++i) {
		const diskimg_t *img = &diskimg_list[i];
		CHECK(decode(img->data, img->data_len, img->size));
		CHECK_EQ(got.len, img->size);
		CHECK_EQ(crc32_update(0, got.out, got.len), img->crc);
		CHECK(got.max_chunk <= LZ_WINDOW / 2);
		CHECK(diskimg_find(img->name) == img);
	}
	CHECK(diskimg_find("cpm22") == diskimg_find("CPM22"));
	CHECK(diskimg_find("NOSUCH") == NULL);
}

static void test_sink_stops(void) {
	const diskimg_t *img = &diskimg_list[0];
	memset(&got, 0, sizeof got);
	got.stop_after = LZ_WINDOW;
	CHECK(!lz_decode(img->data, img->data_len, img->size, window, collect, &got));
}

int main(void) {
	RUN(test_literals_only);
	RUN(test_overlapping_match);
	RUN(test_long_lengths);
	RUN(test_corrupt_data);
	RUN(test_embedded_images);
	RUN(test_sink_stops);
	return tests_done();
}
