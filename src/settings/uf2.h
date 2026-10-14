#ifndef UF2_H
#define UF2_H

// UF2 firmware files (https://github.com/microsoft/uf2): checks a whole file
// a 512-byte block at a time before the firmware update (fwupdate.c) writes
// any of it, and works out the flash image it holds. Portable C,
// host-tested.
//
// Blocks of other families, or not for the main flash, are skipped: the
// SDK's RP2350 files start with an RP2350-E10 workaround block (family
// ABSOLUTE, at 0x10ffff00) with its own block numbers. Ours must:
// - have a 256-byte payload at a 256-byte aligned address within
//   [UF2_FLASH_BASE, UF2_FLASH_BASE + limit);
// - come in order: block numbers 0 .. n-1 with the same n throughout, and
//   addresses ascending (as picotool writes them), so the image's CRC can
//   be taken as the blocks arrive;
// - include the image's first page, and a PICOBIN block (UF2_PICOBIN_MARKER)
//   in its first 4 KB, which the RP2350 boot ROM needs to boot it.

#include <stdbool.h>
#include <stdint.h>

#define UF2_BLOCK_SIZE 512
#define UF2_PAYLOAD_SIZE 256
#define UF2_FLASH_BASE 0x10000000u
#define UF2_SECTOR_SIZE 4096u
#define UF2_FAMILY_RP2350_ARM_S 0xe48bff59u
#define UF2_PICOBIN_MARKER 0xffffded3u

typedef enum {
	UF2_OK,
	UF2_BAD_MAGIC,      // not a UF2 block
	UF2_TRUNCATED,      // the file ends inside a block or before the last one
	UF2_NO_BLOCKS,      // none of this family: another chip's firmware
	UF2_BAD_SEQUENCE,   // block numbers out of order or inconsistent
	UF2_BAD_PAYLOAD,    // payload size not 256
	UF2_BAD_ADDRESS,    // misaligned, outside the flash allowed, or not ascending
	UF2_NO_START,       // nothing at the start of flash
	UF2_NOT_BOOTABLE,   // no PICOBIN block in the first 4 KB
} uf2_result_t;

typedef enum {
	UF2_BLOCK_OURS,
	UF2_BLOCK_SKIP,
	UF2_BLOCK_BAD,
} uf2_block_kind_t;

// One block on its own: whether it is a valid UF2 block, and one of ours
// (family, main flash). For ours, *addr, *block_no and *num_blocks are set.
uf2_block_kind_t uf2_block_check(const uint8_t blk[UF2_BLOCK_SIZE], uint32_t family, uint32_t *addr,
	uint32_t *block_no, uint32_t *num_blocks);

// The payload of a block.
static inline const uint8_t *uf2_payload(const uint8_t blk[UF2_BLOCK_SIZE]) {
	return blk + 32;
}

typedef struct {
	uint32_t family;
	uint32_t limit;      // bytes of flash from UF2_FLASH_BASE the image may use
	uint32_t blocks;     // ours so far
	uint32_t num_blocks; // ours expected (from the first)
	uint32_t end;        // offset after the last payload so far
	uint32_t crc;        // of the image up to end, gaps as 0xff
	bool start, picobin;
	uf2_result_t error;  // the first error found
} uf2_check_t;

void uf2_check_start(uf2_check_t *c, uint32_t family, uint32_t limit);

// The next block of the file. Returns the error so far (UF2_OK: carry on);
// for one of ours, *offset is set to its payload's offset in flash (from
// UF2_FLASH_BASE), else to UINT32_MAX.
uf2_result_t uf2_check_block(uf2_check_t *c, const uint8_t blk[UF2_BLOCK_SIZE], uint32_t *offset);

// The file ended (partial: with bytes left over that make no whole block).
// On UF2_OK, *len is the image's length rounded up to a sector and *crc the
// CRC-32 of those len bytes as flashed (gaps and the tail 0xff).
uf2_result_t uf2_check_end(uf2_check_t *c, bool partial, uint32_t *len, uint32_t *crc);

// A few words for the user, e.g. "not an RP2350 firmware".
const char *uf2_result_text(uf2_result_t r);

#endif
