#ifndef SDCARD_H
#define SDCARD_H

// microSD card on SPI for FatFs (drive 0's disk I/O, through diskio.c): SD v1 (SDSC) and v2 (SDSC/SDHC/SDXC) cards in SPI mode,
// polled, 512-byte blocks (written several at a time).
//
// Pins: the board header's BOARD_SD_* (boards/; on the Pico 2 with the DVI
// Sock SPI0: GP4 MISO (card DO), GP5 CS, GP6 SCK, GP7 MOSI (card DI));
// 3V3 and GND. The adapter has no card-detect line: a card is found by
// initializing it (FatFs does on f_mount), and a removed card shows up as
// I/O errors until it is mounted again.

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "diskio.h"

typedef enum {
	SDCARD_NONE,  // not initialized, or no card answered
	SDCARD_SDSC,  // byte addressed, up to 2 GB
	SDCARD_SDHC,  // block addressed (SDHC and SDXC)
} sdcard_type_t;

// What the last initialization found, and its size in 512-byte blocks.
sdcard_type_t sdcard_type(void);
uint32_t sdcard_blocks(void);

// FatFs's disk_* for drive 0.
DSTATUS sdcard_disk_status(void);
DSTATUS sdcard_disk_initialize(void);
DRESULT sdcard_disk_read(BYTE *buff, LBA_t sector, UINT count);
DRESULT sdcard_disk_write(const BYTE *buff, LBA_t sector, UINT count);
DRESULT sdcard_disk_ioctl(BYTE cmd, void *buff);

#endif
