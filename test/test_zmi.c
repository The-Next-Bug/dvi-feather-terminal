// The WiFi modem's image container (src/modem/zmi.[ch], bd xir.17.13):
// containers built here, good and broken, fed whole and a byte at a time;
// and build/zimodem/ZIMODEM.ZMI when scripts/build-zimodem.sh has made one.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "test.h"
#include "zmi.h"

#define FLASH_4M 0x400000u

typedef struct {
	uint32_t address, length;
	const uint8_t *data;
} part_t;

typedef struct {
	uint8_t chip;
	const char *module, *version;
	uint32_t flash_size;
	uint16_t header_size;
	int32_t parts_size_delta; // added to the right parts size
} header_t;

static uint8_t file[1 << 16];
static size_t file_len;

static void put32(uint8_t *p, uint32_t v) {
	for (int i = 0; i < 4; ++i) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

// A container in file: the header, the parts, the CRC.
static void build(const header_t *h, const part_t *parts, unsigned n) {
	memset(file, 0, sizeof file);
	uint32_t hs = h->header_size ? h->header_size : ZMI_HEADER_SIZE;
	memcpy(file, "ZMI1", 4);
	file[4] = (uint8_t)hs;
	file[5] = (uint8_t)(hs >> 8);
	file[6] = h->chip;
	memcpy(file + 8, h->module, strlen(h->module) < 16 ? strlen(h->module) : 16);
	memcpy(file + 24, h->version, strlen(h->version));
	put32(file + 48, h->flash_size);
	put32(file + 52, n);
	size_t at = hs;
	for (unsigned i = 0; i < n; ++i) {
		put32(file + at, parts[i].address);
		put32(file + at + 4, parts[i].length);
		memcpy(file + at + 8, parts[i].data, parts[i].length);
		at += 8 + parts[i].length;
	}
	put32(file + 56, (uint32_t)((int32_t)(at - hs) + h->parts_size_delta));
	put32(file + at, crc32_update(0, file, at));
	file_len = at + 4;
}

// The ESP8266 image: 0xE9, a segment, DIO, 4 MB (code 4) at 40 MHz.
static uint8_t image[300];
static uint8_t extra[100];

static void make_data(void) {
	for (size_t i = 0; i < sizeof image; ++i) {
		image[i] = (uint8_t)(i * 7 + 1);
	}
	image[0] = 0xE9;
	image[1] = 1;
	image[2] = 2;
	image[3] = 0x40;
	for (size_t i = 0; i < sizeof extra; ++i) {
		extra[i] = (uint8_t)(0xA0 ^ i);
	}
}

static const header_t good = {ZMI_CHIP_ESP8266, "esp12f", "4.0.3-20261119", FLASH_4M, 0, 0};

// What the data callback saw: each byte at its address, in order.
static uint8_t seen[FLASH_4M / 64]; // the low addresses only
static uint32_t seen_hi[sizeof extra], seen_bytes, last_address;
static bool out_of_order;

static void on_data(void *ctx, uint32_t address, const uint8_t *data, size_t len) {
	(void)ctx;
	if (seen_bytes && address < last_address) {
		out_of_order = true;
	}
	for (size_t i = 0; i < len; ++i) {
		uint32_t a = address + (uint32_t)i;
		if (a < sizeof seen) {
			seen[a] = data[i];
		} else if (a >= 0x300000 && a - 0x300000 < sizeof extra) {
			seen_hi[a - 0x300000] = data[i];
		}
	}
	seen_bytes += (uint32_t)len;
	last_address = address + (uint32_t)len;
}

static void reset_seen(void) {
	memset(seen, 0, sizeof seen);
	memset(seen_hi, 0, sizeof seen_hi);
	seen_bytes = last_address = 0;
	out_of_order = false;
}

// The file fed whole (step 0) or step bytes at a time: the result.
static zmi_result_t read_file(zmi_reader_t *r, size_t step, bool callback) {
	reset_seen();
	zmi_init(r, callback ? on_data : NULL, NULL);
	size_t at = 0;
	while (at < file_len) {
		size_t n = step && step < file_len - at ? step : file_len - at;
		zmi_feed(r, file + at, n);
		at += n;
	}
	return zmi_finish(r);
}

static void test_good(void) {
	make_data();
	part_t parts[] = {{0, sizeof image, image}};
	build(&good, parts, 1);
	for (size_t step = 0; step <= 3; ++step) {
		zmi_reader_t r;
		CHECK_EQ(read_file(&r, step, true), ZMI_OK);
		CHECK(zmi_header_done(&r));
		CHECK_EQ(r.info.chip, ZMI_CHIP_ESP8266);
		CHECK(strcmp(r.info.module, "esp12f") == 0);
		CHECK(strcmp(r.info.version, "4.0.3-20261119") == 0);
		CHECK_EQ(r.info.flash_size, FLASH_4M);
		CHECK_EQ(r.info.parts, 1);
		CHECK_EQ(r.info.image_bytes, sizeof image);
		CHECK_EQ(seen_bytes, sizeof image);
		CHECK(memcmp(seen, image, sizeof image) == 0);
		CHECK_EQ(zmi_check_target(&r.info, ZMI_CHIP_ESP8266, "esp12f", FLASH_4M), ZMI_OK);
	}
	// A header a later version made longer: its extra bytes skipped.
	header_t longer = good;
	longer.header_size = 80;
	build(&longer, parts, 1);
	zmi_reader_t r;
	CHECK_EQ(read_file(&r, 5, true), ZMI_OK);
	CHECK(memcmp(seen, image, sizeof image) == 0);
}

static void test_two_parts(void) {
	make_data();
	part_t parts[] = {{0, sizeof image, image}, {0x300000, sizeof extra, extra}};
	build(&good, parts, 2);
	zmi_reader_t r;
	CHECK_EQ(read_file(&r, 7, true), ZMI_OK);
	CHECK_EQ(r.info.image_bytes, sizeof image + sizeof extra);
	CHECK(memcmp(seen, image, sizeof image) == 0);
	for (size_t i = 0; i < sizeof extra; ++i) {
		CHECK_EQ(seen_hi[i], extra[i]);
	}
	CHECK(!out_of_order);
}

static void test_damaged(void) {
	make_data();
	part_t parts[] = {{0, sizeof image, image}};
	build(&good, parts, 1);
	zmi_reader_t r;
	file[ZMI_HEADER_SIZE + 8 + 100] ^= 1;
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_CRC);
	file[ZMI_HEADER_SIZE + 8 + 100] ^= 1;
	file[44] ^= 1; // the version's padding: the header is refused before the CRC
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	file[44] ^= 1;
	file[26] ^= 1; // a version digit: a valid header, but damaged
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_CRC);
	file[26] ^= 1;
	size_t whole = file_len;
	file_len = whole - 1;
	CHECK_EQ(read_file(&r, 0, false), ZMI_TRUNCATED);
	file_len = 10;
	CHECK_EQ(read_file(&r, 0, false), ZMI_TRUNCATED);
	CHECK(!zmi_header_done(&r));
	file_len = whole + 1;
	CHECK_EQ(read_file(&r, 0, false), ZMI_TOO_LONG);
	file_len = whole;
	memcpy(file, "UF2\n", 4);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_MAGIC);
}

static void test_bad_headers(void) {
	make_data();
	part_t parts[] = {{0, sizeof image, image}};
	zmi_reader_t r;
	header_t h = good;
	h.chip = 2;
	build(&h, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	h = good;
	h.flash_size = 0x300000;
	build(&h, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	h = good;
	h.module = "sixteen-chars-xx"; // no NUL left
	build(&h, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	h = good;
	h.module = "";
	build(&h, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	h = good;
	h.header_size = 32;
	build(&h, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
	build(&good, parts, 0); // no parts
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_HEADER);
}

static void test_bad_parts(void) {
	make_data();
	zmi_reader_t r;
	part_t unaligned[] = {{0, sizeof image, image}, {0x300100, sizeof extra, extra}};
	build(&good, unaligned, 2);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
	part_t overlapping[] = {{0, 0x1000 + 1, image}, {0x1000, sizeof extra, extra}};
	static uint8_t big[0x1001];
	memcpy(big, image, sizeof image);
	overlapping[0].data = big;
	build(&good, overlapping, 2);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
	part_t backwards[] = {{0x300000, sizeof extra, extra}, {0, sizeof image, image}};
	build(&good, backwards, 2);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
	part_t past[] = {{0, sizeof image, image}, {FLASH_4M - 0x1000, 0x1001, big}};
	build(&good, past, 2);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
	part_t one[] = {{0, sizeof image, image}};
	header_t h = good;
	h.parts_size_delta = 4; // claims more than the parts are
	build(&h, one, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
	h.parts_size_delta = -4; // less: the part runs past it
	build(&h, one, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_PART);
}

static void test_bad_image(void) {
	make_data();
	zmi_reader_t r;
	part_t parts[] = {{0, sizeof image, image}};
	image[0] = 0xEA;
	build(&good, parts, 1);
	CHECK_EQ(read_file(&r, 1, true), ZMI_BAD_IMAGE);
	CHECK_EQ(seen_bytes, 0); // no data passed on from a refused image
	image[0] = 0xE9;
	image[3] = 0x30; // 2 MB
	build(&good, parts, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_IMAGE);
	image[3] = 0x40;
	part_t short_image[] = {{0, 2, image}};
	build(&good, short_image, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_IMAGE);
	part_t not_at_zero[] = {{0x1000, sizeof image, image}};
	build(&good, not_at_zero, 1);
	CHECK_EQ(read_file(&r, 0, false), ZMI_BAD_IMAGE);
}

static void test_target(void) {
	zmi_info_t info = {.chip = ZMI_CHIP_ESP8266, .module = "esp12f", .flash_size = FLASH_4M};
	CHECK_EQ(zmi_check_target(&info, ZMI_CHIP_ESP8266, "esp12f", 0x1000000), ZMI_OK);
	CHECK_EQ(zmi_check_target(&info, 2, "esp12f", FLASH_4M), ZMI_WRONG_CHIP);
	CHECK_EQ(zmi_check_target(&info, ZMI_CHIP_ESP8266, "esp01", FLASH_4M), ZMI_WRONG_MODULE);
	CHECK_EQ(zmi_check_target(&info, ZMI_CHIP_ESP8266, "esp12f", 0x100000), ZMI_TOO_BIG);
	for (int i = ZMI_OK; i <= ZMI_TOO_BIG; ++i) {
		CHECK(strcmp(zmi_result_text((zmi_result_t)i), "?") != 0);
	}
}

// scripts/build-zimodem.sh's container, if it has run.
static void test_built_container(void) {
	FILE *f = fopen(ZMI_FILE, "rb");
	if (!f) {
		printf("  (no %s: scripts/build-zimodem.sh not run)\n", ZMI_FILE);
		return;
	}
	zmi_reader_t r;
	zmi_init(&r, NULL, NULL);
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
		zmi_feed(&r, buf, n);
	}
	fclose(f);
	CHECK_EQ(zmi_finish(&r), ZMI_OK);
	CHECK(strcmp(r.info.module, "esp12f") == 0);
	CHECK(strncmp(r.info.version, "4.0.3-", 6) == 0);
	CHECK_EQ(r.info.flash_size, FLASH_4M);
	CHECK_EQ(zmi_check_target(&r.info, ZMI_CHIP_ESP8266, "esp12f", FLASH_4M), ZMI_OK);
	printf("  %s: Zimodem %s, %u bytes\n", ZMI_FILE, r.info.version, (unsigned)r.info.image_bytes);
}

int main(void) {
	RUN(test_good);
	RUN(test_two_parts);
	RUN(test_damaged);
	RUN(test_bad_headers);
	RUN(test_bad_parts);
	RUN(test_bad_image);
	RUN(test_target);
	RUN(test_built_container);
	return tests_done();
}
