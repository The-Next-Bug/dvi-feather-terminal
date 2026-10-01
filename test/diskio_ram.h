#ifndef DISKIO_RAM_H
#define DISKIO_RAM_H

#include <stddef.h>

// A blank RAM volume of 512-byte sectors for FatFs drive 0.
void diskio_ram_init(size_t sectors);
void diskio_ram_free(void);

// The most sectors one disk_write was given since the last call.
unsigned diskio_ram_max_write(void);

#endif
