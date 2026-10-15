#include "fwupdate.h"

#include <assert.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "crc32.h"
#include "ff.h"
#include "nvflash.h"
#include "scratch.h"
#include "uf2.h"

#define MAX_IMAGE (NVFLASH_OFFSET - FWUPDATE_STAGING)
static_assert(MAX_IMAGE <= FWUPDATE_STAGING, "the image stays below the staging area");
static_assert(FWUPDATE_STAGING % FLASH_SECTOR_SIZE == 0, "");
static_assert(FLASH_SECTOR_SIZE <= SCRATCH_SIZE && UF2_BLOCK_SIZE <= SCRATCH_SIZE, "fits the scratch buffer");

extern char __flash_binary_end;

// The UF2 block being read, and the commit's copy of a sector (the ROM
// programs from RAM), are in the scratch buffer (scratch.h).
static FIL file;
static uint32_t staged_len;

// Read path through, checking it; with stage, also write our blocks to the
// staging area (erased as the ascending addresses reach each sector) and
// verify it. NULL, or why not.
static const char *scan(const char *path, bool stage, fwupdate_progress_t progress, void *ctx,
	uint32_t *size, uint32_t *len) {
	// The running image must not reach the staging area (a big build).
	if ((uintptr_t)&__flash_binary_end - XIP_BASE > FWUPDATE_STAGING) {
		return "the running firmware is too big to update this way";
	}
	uint8_t *block = scratch_acquire();
	if (!block) {
		return "busy: a disk is being written";
	}
	if (f_open(&file, path, FA_READ) != FR_OK) {
		scratch_release(block);
		return "cannot open the file";
	}
	*size = (uint32_t)f_size(&file);

	uf2_check_t check;
	uf2_check_start(&check, UF2_FAMILY_RP2350_ARM_S, MAX_IMAGE);
	uint32_t erased = 0, done = 0;
	bool partial = false;
	const char *why = NULL;
	while (true) {
		UINT got;
		if (f_read(&file, block, UF2_BLOCK_SIZE, &got) != FR_OK) {
			why = "card read error";
			break;
		}
		if (got < UF2_BLOCK_SIZE) {
			partial = got != 0;
			break;
		}
		uint32_t offset;
		if (uf2_check_block(&check, block, &offset) != UF2_OK) {
			break;
		}
		if (stage && offset != UINT32_MAX) {
			for (; erased <= offset; erased += FLASH_SECTOR_SIZE) {
				nvflash_erase(FWUPDATE_STAGING + erased, FLASH_SECTOR_SIZE);
			}
			nvflash_program(FWUPDATE_STAGING + offset, uf2_payload(block), UF2_PAYLOAD_SIZE);
		}
		done += got;
		if (progress) {
			progress(ctx, done, *size);
		}
	}
	f_close(&file);
	scratch_release(block);
	if (why) {
		return why;
	}

	uint32_t crc;
	uf2_result_t r = uf2_check_end(&check, partial, len, &crc);
	if (r != UF2_OK) {
		return uf2_result_text(r);
	}
	if (stage) {
		for (; erased < *len; erased += FLASH_SECTOR_SIZE) {
			nvflash_erase(FWUPDATE_STAGING + erased, FLASH_SECTOR_SIZE);
		}
		if (crc32_update(0, nvflash_read(FWUPDATE_STAGING), *len) != crc) {
			return "flash verify failed";
		}
	}
	return NULL;
}

bool fwupdate_check(const char *path, char *info, size_t n) {
	uint32_t size = 0, len = 0;
	const char *why = scan(path, false, NULL, NULL, &size, &len);
	if (why) {
		snprintf(info, n, "%s", why);
	} else {
		snprintf(info, n, "%lu KB file, %lu KB firmware (running: %lu KB)", (unsigned long)(size / 1024),
			(unsigned long)(len / 1024),
			(unsigned long)(((uintptr_t)&__flash_binary_end - XIP_BASE + 1023) / 1024));
	}
	return !why;
}

bool fwupdate_stage(const char *path, fwupdate_progress_t progress, void *ctx, char *info, size_t n) {
	uint32_t size = 0, len = 0;
	staged_len = 0;
	const char *why = scan(path, true, progress, ctx, &size, &len);
	if (why) {
		snprintf(info, n, "%s", why);
		return false;
	}
	staged_len = len;
	return true;
}

// Once the first sector is erased, the old image's const data is gone (or
// the new image's): only RAM, registers and the staging area from here on.
static void __no_inline_not_in_flash_func(fwupdate_commit_image)(uint32_t len, uint8_t *sector) {
	for (uint32_t offset = 0; offset < len; offset += FLASH_SECTOR_SIZE) {
		const uint32_t *from = (const uint32_t *)nvflash_read(FWUPDATE_STAGING + offset);
		const uint32_t *now = (const uint32_t *)nvflash_read(offset);
		uint32_t *to = (uint32_t *)sector;
		bool same = true;
		for (unsigned i = 0; i < FLASH_SECTOR_SIZE / 4; ++i) {
			to[i] = from[i];
			same = same && now[i] == to[i];
		}
		if (same) { // as it is already: spare the flash
			continue;
		}
		nvflash_erase(offset, FLASH_SECTOR_SIZE);
		nvflash_program(offset, sector, FLASH_SECTOR_SIZE);
	}
	watchdog_reboot(0, 0, 0);
	while (true) {
		tight_loop_contents();
	}
}

void fwupdate_commit(void) {
	uint8_t *sector = staged_len ? scratch_acquire() : NULL;
	if (!sector) {
		return;
	}
	(void)save_and_disable_interrupts(); // for good: the board restarts
	fwupdate_commit_image(staged_len, sector);
}
