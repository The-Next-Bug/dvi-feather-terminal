#include "uf2.h"

#include <string.h>

#include "crc32.h"

#define MAGIC_START0 0x0a324655u
#define MAGIC_START1 0x9e5d5157u
#define MAGIC_END 0x0ab16f30u
#define FLAG_NOT_MAIN_FLASH 0x00000001u
#define FLAG_FAMILY_ID 0x00002000u

static uint32_t word(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uf2_block_kind_t uf2_block_check(const uint8_t blk[UF2_BLOCK_SIZE], uint32_t family, uint32_t *addr,
	uint32_t *block_no, uint32_t *num_blocks) {
	if (word(blk) != MAGIC_START0 || word(blk + 4) != MAGIC_START1 || word(blk + 508) != MAGIC_END) {
		return UF2_BLOCK_BAD;
	}
	uint32_t flags = word(blk + 8);
	if (flags & FLAG_NOT_MAIN_FLASH || !(flags & FLAG_FAMILY_ID) || word(blk + 28) != family) {
		return UF2_BLOCK_SKIP;
	}
	*addr = word(blk + 12);
	*block_no = word(blk + 20);
	*num_blocks = word(blk + 24);
	return UF2_BLOCK_OURS;
}

void uf2_check_start(uf2_check_t *c, uint32_t family, uint32_t limit) {
	memset(c, 0, sizeof *c);
	c->family = family;
	c->limit = limit;
}

// Continue the CRC over n erased bytes.
static uint32_t crc_erased(uint32_t crc, uint32_t n) {
	static const uint8_t ff[64] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};
	while (n) {
		uint32_t k = n < sizeof ff ? n : sizeof ff;
		crc = crc32_update(crc, ff, k);
		n -= k;
	}
	return crc;
}

static uf2_result_t fail(uf2_check_t *c, uf2_result_t r) {
	if (c->error == UF2_OK) {
		c->error = r;
	}
	return c->error;
}

uf2_result_t uf2_check_block(uf2_check_t *c, const uint8_t blk[UF2_BLOCK_SIZE], uint32_t *offset) {
	*offset = UINT32_MAX;
	if (c->error != UF2_OK) {
		return c->error;
	}
	uint32_t addr, block_no, num_blocks;
	switch (uf2_block_check(blk, c->family, &addr, &block_no, &num_blocks)) {
	case UF2_BLOCK_BAD:
		return fail(c, UF2_BAD_MAGIC);
	case UF2_BLOCK_SKIP:
		return UF2_OK;
	case UF2_BLOCK_OURS:
		break;
	}
	if (c->blocks == 0) {
		c->num_blocks = num_blocks;
	}
	if (num_blocks != c->num_blocks || block_no != c->blocks || block_no >= num_blocks) {
		return fail(c, UF2_BAD_SEQUENCE);
	}
	if (word(blk + 16) != UF2_PAYLOAD_SIZE) {
		return fail(c, UF2_BAD_PAYLOAD);
	}
	uint32_t off = addr - UF2_FLASH_BASE;
	if (addr < UF2_FLASH_BASE || off % UF2_PAYLOAD_SIZE || off >= c->limit
		|| c->limit - off < UF2_PAYLOAD_SIZE || (c->blocks && off < c->end)) {
		return fail(c, UF2_BAD_ADDRESS);
	}

	const uint8_t *data = uf2_payload(blk);
	c->crc = crc32_update(crc_erased(c->crc, off - c->end), data, UF2_PAYLOAD_SIZE);
	c->end = off + UF2_PAYLOAD_SIZE;
	++c->blocks;
	if (off == 0) {
		c->start = true;
	}
	if (off < UF2_SECTOR_SIZE) {
		for (unsigned i = 0; i < UF2_PAYLOAD_SIZE; i += 4) {
			if (word(data + i) == UF2_PICOBIN_MARKER) {
				c->picobin = true;
			}
		}
	}
	*offset = off;
	return UF2_OK;
}

uf2_result_t uf2_check_end(uf2_check_t *c, bool partial, uint32_t *len, uint32_t *crc) {
	if (c->error != UF2_OK) {
		return c->error;
	}
	if (!c->blocks) {
		return fail(c, partial ? UF2_TRUNCATED : UF2_NO_BLOCKS);
	}
	if (partial || c->blocks != c->num_blocks) {
		return fail(c, UF2_TRUNCATED);
	}
	if (!c->start) {
		return fail(c, UF2_NO_START);
	}
	if (!c->picobin) {
		return fail(c, UF2_NOT_BOOTABLE);
	}
	*len = (c->end + UF2_SECTOR_SIZE - 1) / UF2_SECTOR_SIZE * UF2_SECTOR_SIZE;
	if (*len > c->limit) {
		return fail(c, UF2_BAD_ADDRESS);
	}
	*crc = crc_erased(c->crc, *len - c->end);
	return UF2_OK;
}

const char *uf2_result_text(uf2_result_t r) {
	switch (r) {
	case UF2_OK:
		return "OK";
	case UF2_BAD_MAGIC:
		return "not a UF2 file";
	case UF2_TRUNCATED:
		return "file incomplete";
	case UF2_NO_BLOCKS:
		return "not an RP2350 firmware";
	case UF2_BAD_SEQUENCE:
		return "blocks out of order";
	case UF2_BAD_PAYLOAD:
		return "unexpected block size";
	case UF2_BAD_ADDRESS:
		return "too big or bad address";
	case UF2_NO_START:
		return "no start of flash";
	case UF2_NOT_BOOTABLE:
		return "no RP2350 boot block";
	}
	return "?";
}
