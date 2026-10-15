#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "test.h"
#include "uf2.h"

#define FAMILY_ABSOLUTE 0xe48bff57u
#define FAMILY_RP2040 0xe48bff56u
#define LIMIT (2u * 1024 * 1024)
#define MAX_BLOCKS 64

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void block(uint8_t *blk, uint32_t flags, uint32_t addr, uint32_t no, uint32_t n, uint32_t family) {
	memset(blk, 0, UF2_BLOCK_SIZE);
	put32(blk, 0x0a324655);
	put32(blk + 4, 0x9e5d5157);
	put32(blk + 8, flags);
	put32(blk + 12, addr);
	put32(blk + 16, UF2_PAYLOAD_SIZE);
	put32(blk + 20, no);
	put32(blk + 24, n);
	put32(blk + 28, family);
	for (unsigned i = 0; i < UF2_PAYLOAD_SIZE; ++i) {
		blk[32 + i] = (uint8_t)(addr / UF2_PAYLOAD_SIZE * 7 + i);
	}
	put32(blk + 508, 0x0ab16f30);
}

// A file as picotool writes one: the E10 block, then n of ours from the
// start of flash (a PICOBIN marker in the first), with a gap of `gap`
// pages before the last block.
static uint8_t file[MAX_BLOCKS + 1][UF2_BLOCK_SIZE];
static uint8_t image[MAX_BLOCKS * 2 * UF2_PAYLOAD_SIZE + UF2_SECTOR_SIZE]; // as flashed

static unsigned make_file(unsigned n, unsigned gap) {
	memset(image, 0xff, sizeof image);
	block(file[0], 0xa000, 0x10ffff00, 0, 2, FAMILY_ABSOLUTE);
	for (unsigned i = 0; i < n; ++i) {
		uint32_t off = (i + (i == n - 1 ? gap : 0)) * UF2_PAYLOAD_SIZE;
		block(file[i + 1], 0x2000, UF2_FLASH_BASE + off, i, n, UF2_FAMILY_RP2350_ARM_S);
		if (i == 0) {
			put32(file[1] + 32 + 0x10, UF2_PICOBIN_MARKER);
		}
		memcpy(image + off, uf2_payload(file[i + 1]), UF2_PAYLOAD_SIZE);
	}
	return n + 1;
}

// Check blocks[0 .. count) with the last `partial` bytes cut off.
static uf2_result_t check(unsigned count, bool partial, uint32_t *len, uint32_t *crc) {
	uf2_check_t c;
	uint32_t offset;
	uf2_check_start(&c, UF2_FAMILY_RP2350_ARM_S, LIMIT);
	for (unsigned i = 0; i < count; ++i) {
		if (uf2_check_block(&c, file[i], &offset) != UF2_OK) {
			break;
		}
	}
	return uf2_check_end(&c, partial, len, crc);
}

static void test_good_file(void) {
	unsigned count = make_file(20, 0);
	uint32_t len = 0, crc = 0;
	CHECK_EQ(check(count, false, &len, &crc), UF2_OK);
	CHECK_EQ(len, 2 * UF2_SECTOR_SIZE); // 20 pages, rounded up
	CHECK_EQ(crc, crc32_update(0, image, len));
}

static void test_offsets_and_skips(void) {
	unsigned count = make_file(3, 0);
	uf2_check_t c;
	uint32_t offset = 0;
	uf2_check_start(&c, UF2_FAMILY_RP2350_ARM_S, LIMIT);
	CHECK_EQ(uf2_check_block(&c, file[0], &offset), UF2_OK);
	CHECK_EQ(offset, UINT32_MAX); // the E10 block is skipped
	for (unsigned i = 1; i < count; ++i) {
		CHECK_EQ(uf2_check_block(&c, file[i], &offset), UF2_OK);
		CHECK_EQ(offset, (i - 1) * UF2_PAYLOAD_SIZE);
	}
	// Not for the main flash: skipped too.
	uint8_t other[UF2_BLOCK_SIZE];
	block(other, 0x2001, UF2_FLASH_BASE, 0, 1, UF2_FAMILY_RP2350_ARM_S);
	CHECK_EQ(uf2_check_block(&c, other, &offset), UF2_OK);
	CHECK_EQ(offset, UINT32_MAX);
}

static void test_gap_reads_as_erased(void) {
	unsigned count = make_file(5, 40); // the last block 44 pages in
	uint32_t len = 0, crc = 0;
	CHECK_EQ(check(count, false, &len, &crc), UF2_OK);
	CHECK_EQ(len, 3 * UF2_SECTOR_SIZE);
	CHECK_EQ(crc, crc32_update(0, image, len));
}

static void test_truncated(void) {
	unsigned count = make_file(20, 0);
	uint32_t len, crc;
	CHECK_EQ(check(count - 1, false, &len, &crc), UF2_TRUNCATED);
	CHECK_EQ(check(count, true, &len, &crc), UF2_TRUNCATED);
	CHECK_EQ(check(0, true, &len, &crc), UF2_TRUNCATED);
}

static void test_wrong_family(void) {
	unsigned count = make_file(10, 0);
	for (unsigned i = 1; i < count; ++i) {
		put32(file[i] + 28, FAMILY_RP2040);
	}
	uint32_t len, crc;
	CHECK_EQ(check(count, false, &len, &crc), UF2_NO_BLOCKS);
	CHECK_EQ(check(0, false, &len, &crc), UF2_NO_BLOCKS);
}

static void test_bad_magic(void) {
	uint32_t len, crc;
	unsigned count = make_file(10, 0);
	file[4][0] ^= 1;
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_MAGIC);
	count = make_file(10, 0);
	file[4][508] ^= 1;
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_MAGIC);
}

static void test_bad_addresses(void) {
	uint32_t len, crc;
	unsigned count = make_file(10, 0);
	put32(file[5] + 12, UF2_FLASH_BASE + 4 * UF2_PAYLOAD_SIZE + 4); // misaligned
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_ADDRESS);
	count = make_file(10, 0);
	put32(file[10] + 12, UF2_FLASH_BASE + LIMIT); // past the limit
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_ADDRESS);
	count = make_file(10, 0);
	put32(file[10] + 12, 0x20000000); // SRAM
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_ADDRESS);
	count = make_file(10, 0);
	put32(file[10] + 12, 0x0fffff00); // below flash
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_ADDRESS);
	count = make_file(10, 0);
	put32(file[5] + 12, UF2_FLASH_BASE); // not ascending
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_ADDRESS);
}

static void test_bad_sequence(void) {
	uint32_t len, crc;
	unsigned count = make_file(10, 0);
	memcpy(file[5], file[4], UF2_BLOCK_SIZE); // a block twice, the next missing
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_SEQUENCE);
	count = make_file(10, 0);
	put32(file[5] + 24, 11); // numBlocks changes
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_SEQUENCE);
	count = make_file(10, 0);
	put32(file[1] + 20, 1); // starts at 1
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_SEQUENCE);
	count = make_file(10, 0);
	put32(file[3] + 16, 128);
	CHECK_EQ(check(count, false, &len, &crc), UF2_BAD_PAYLOAD);
}

static void test_not_bootable(void) {
	uint32_t len, crc;
	unsigned count = make_file(20, 0);
	put32(file[1] + 32 + 0x10, 0);
	CHECK_EQ(check(count, false, &len, &crc), UF2_NOT_BOOTABLE);
	// No block at the start of flash.
	count = make_file(20, 0);
	for (unsigned i = 1; i < count; ++i) {
		put32(file[i] + 12, UF2_FLASH_BASE + i * UF2_PAYLOAD_SIZE);
	}
	CHECK_EQ(check(count, false, &len, &crc), UF2_NO_START);
}

// The firmware the build made, when there is one (scripts/build.sh).
static void test_real_firmware(void) {
	FILE *f = fopen(FIRMWARE_UF2, "rb");
	if (!f) {
		printf("     (no %s: skipped)\n", FIRMWARE_UF2);
		return;
	}
	uf2_check_t c;
	uint8_t blk[UF2_BLOCK_SIZE];
	uint32_t offset, len = 0, crc = 0, ours = 0;
	size_t got;
	uf2_check_start(&c, UF2_FAMILY_RP2350_ARM_S, LIMIT);
	while ((got = fread(blk, 1, sizeof blk, f)) == sizeof blk) {
		CHECK_EQ(uf2_check_block(&c, blk, &offset), UF2_OK);
		ours += offset != UINT32_MAX;
	}
	fclose(f);
	CHECK_EQ(uf2_check_end(&c, got != 0, &len, &crc), UF2_OK);
	CHECK(ours > 100);
	CHECK(len >= ours * UF2_PAYLOAD_SIZE && len % UF2_SECTOR_SIZE == 0);
}

int main(void) {
	RUN(test_good_file);
	RUN(test_offsets_and_skips);
	RUN(test_gap_reads_as_erased);
	RUN(test_truncated);
	RUN(test_wrong_family);
	RUN(test_bad_magic);
	RUN(test_bad_addresses);
	RUN(test_bad_sequence);
	RUN(test_not_bootable);
	RUN(test_real_firmware);
	return tests_done();
}
