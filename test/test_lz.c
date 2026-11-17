#include <string.h>

#include "crc32.h"
#include "diskimg.h"
#include "lz.h"
#include "test.h"

static uint8_t work[LZ_WORK] __attribute__((aligned(4)));

typedef struct {
	uint8_t out[300000]; // the first bytes
	size_t len;
	uint32_t crc; // of all of it
	size_t max_chunk;
	size_t stop_after; // 0: never stop
} collect_t;

static collect_t got;

static bool collect(void *ctx, const uint8_t *data, size_t len) {
	collect_t *c = ctx;
	if (len > c->max_chunk) {
		c->max_chunk = len;
	}
	if (c->len < sizeof c->out) { // the 8 MB disks are only counted
		memcpy(c->out + c->len, data, len < sizeof c->out - c->len ? len : sizeof c->out - c->len);
	}
	c->crc = crc32_update(c->crc, data, len);
	c->len += len;
	return !c->stop_after || c->len < c->stop_after;
}

static bool decode(const uint8_t *src, size_t len, size_t out_size) {
	memset(&got, 0, sizeof got);
	return lz_decode(src, len, out_size, work, collect, &got);
}

// Python's zlib, raw deflate (wbits -11), fixed Huffman codes.
static const uint8_t hello[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00};

static void test_fixed_literals(void) {
	CHECK(decode(hello, sizeof hello, 5));
	CHECK_EQ(got.len, 5);
	CHECK(memcmp(got.out, "hello", 5) == 0);
}

static void test_overlapping_match(void) {
	// "ab", then a 10-byte match at distance 2, then "c".
	const uint8_t src[] = {0x4b, 0x4c, 0x4a, 0x84, 0xc3, 0x64, 0x00};
	CHECK(decode(src, sizeof src, 13));
	CHECK(memcmp(got.out, "ababababababc", 13) == 0);
}

static void test_stored_block(void) {
	const uint8_t src[] = {0x01, 0x07, 0x00, 0xf8, 0xff, 's', 't', 'o', 'r', 'e', 'd', '!'};
	CHECK(decode(src, sizeof src, 7));
	CHECK(memcmp(got.out, "stored!", 7) == 0);
	uint8_t bad[sizeof src];
	memcpy(bad, src, sizeof src);
	bad[3] = 0xf9; // NLEN not LEN's complement
	CHECK(!decode(bad, sizeof bad, 7));
}

// A fixed-Huffman block written by hand: Huffman codes go most significant
// bit first, everything else least significant bit first.
typedef struct {
	uint8_t buf[4096];
	size_t bit;
} writer_t;

static void put_bits(writer_t *w, unsigned v, unsigned n) {
	for (unsigned i = 0; i < n; ++i, ++w->bit) {
		if (v >> i & 1) {
			w->buf[w->bit / 8] |= (uint8_t)(1u << w->bit % 8);
		}
	}
}

static void put_code(writer_t *w, unsigned code, unsigned n) {
	while (n--) {
		put_bits(w, code >> n & 1, 1);
	}
}

static void put_literal(writer_t *w, uint8_t c) {
	if (c < 144) {
		put_code(w, 0x30 + c, 8);
	} else {
		put_code(w, 0x190 + c - 144, 9);
	}
}

static void test_distance_beyond_dictionary(void) {
	// 3000 literals, then length 3 at distance 2049 (code 22, 10 extra
	// bits 0): fine for zlib's 32 KB, but past this 2 KB dictionary.
	static writer_t w;
	for (unsigned d = 2048; d <= 2049; ++d) {
		memset(&w, 0, sizeof w);
		put_bits(&w, 1, 1); // final
		put_bits(&w, 1, 2); // fixed
		for (unsigned i = 0; i < 3000; ++i) {
			put_literal(&w, (uint8_t)(i * 7));
		}
		put_code(&w, 1, 7);  // 257: length 3
		put_code(&w, d == 2048 ? 21 : 22, 5); // 21: 1537 + 9 bits; 22: 2049 + 10 bits
		put_bits(&w, d == 2048 ? 511 : 0, d == 2048 ? 9 : 10);
		put_code(&w, 0, 7); // end of block
		bool ok = decode(w.buf, (w.bit + 7) / 8, 3003);
		if (d == 2048) {
			CHECK(ok); // the whole dictionary back
			CHECK_EQ(got.out[3000], (uint8_t)(952 * 7));
			CHECK_EQ(got.out[3002], (uint8_t)(954 * 7));
		} else {
			CHECK(!ok);
		}
	}
}

static void test_corrupt_data(void) {
	CHECK(!decode(hello, 3, 5));             // truncated
	CHECK(!decode(hello, sizeof hello, 4));  // more output than expected
	CHECK(!decode(hello, sizeof hello, 6));  // less
	const uint8_t reserved[] = {0x07, 0x00}; // block type 3
	CHECK(!decode(reserved, sizeof reserved, 1));
	static writer_t w; // a match first: length 3, distance 1, nothing before it
	memset(&w, 0, sizeof w);
	put_bits(&w, 1, 1);
	put_bits(&w, 1, 2);
	put_code(&w, 1, 7);
	put_code(&w, 0, 5);
	put_code(&w, 0, 7);
	CHECK(!decode(w.buf, (w.bit + 7) / 8, 3));
	static const uint8_t zeros[64];
	CHECK(!decode(zeros, sizeof zeros, 1)); // stored, LEN 0 NLEN 0
}

static void test_embedded_images(void) {
	CHECK(diskimg_count >= 1);
	for (unsigned i = 0; i < diskimg_count; ++i) {
		const diskimg_t *img = &diskimg_list[i];
		CHECK(decode(img->data, img->data_len, img->size)); // dynamic blocks, zlib's level 9
		CHECK_EQ(got.len, img->size);
		CHECK_EQ(got.crc, img->crc);
		CHECK(got.max_chunk <= LZ_DICT / 2);
		CHECK(diskimg_find(img->name) == img);
	}
	CHECK(diskimg_find("cpm22") == diskimg_find("CPM22"));
	CHECK(diskimg_find("NOSUCH") == NULL);
}

static void test_sink_stops(void) {
	const diskimg_t *img = &diskimg_list[0];
	memset(&got, 0, sizeof got);
	got.stop_after = LZ_WORK;
	CHECK(!lz_decode(img->data, img->data_len, img->size, work, collect, &got));
}

int main(void) {
	RUN(test_fixed_literals);
	RUN(test_overlapping_match);
	RUN(test_stored_block);
	RUN(test_distance_beyond_dictionary);
	RUN(test_corrupt_data);
	RUN(test_embedded_images);
	RUN(test_sink_stops);
	return tests_done();
}
