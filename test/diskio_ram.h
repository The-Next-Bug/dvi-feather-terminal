#ifndef DISKIO_RAM_H
#define DISKIO_RAM_H

#include <stddef.h>

// Blank RAM volumes of 512-byte sectors for FatFs: drive 0 (the SD card's
// place) and drive 1 (a USB stick's).
void diskio_ram_init(size_t sectors);
void diskio_ram_free(void);
void diskio_ram_init_drive(unsigned pdrv, size_t sectors);
void diskio_ram_free_drive(unsigned pdrv);

// The most sectors one disk_write was given since the last call.
unsigned diskio_ram_max_write(void);

#endif
