#ifndef SDFORMAT_H
#define SDFORMAT_H

// Formatting the SD card (the monitor's F): a FAT file system suited to
// the card's size, after the SD Association's layout (FAT12/16 up to
// 2 GB, FAT32 above, clusters on the card's erase blocks), in one MBR
// partition. Cards over 32 GB get FAT32 rather than their exFAT, which
// FatFs would need long file names for.

#include <stddef.h>
#include <stdint.h>

#include "ff.h"

typedef struct {
	MKFS_PARM parm;
	const char *name; // e.g. "FAT32, 32 KB clusters"
} sdformat_t;

// The format for a card of `sectors` 512-byte blocks.
void sdformat_choose(uint32_t sectors, sdformat_t *f);

// Format volume path ("" the SD card, "1:" a stick) as f says, with work
// (at least 512 bytes; more is faster) as the buffer. The volume is
// unmounted first; mount it again afterwards.
FRESULT sdformat_run(const char *path, const sdformat_t *f, void *work, size_t len);

#endif
