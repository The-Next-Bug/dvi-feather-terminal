#ifndef NVFLASH_H
#define NVFLASH_H

// The last NVFLASH_SECTORS sectors of the board's flash, for settings kept
// across power cycles. Written with the DVI clock running (400 MHz): the
// flash serial clock is kept within spec at any clk_sys, see nvflash.c.
//
// Writing blocks core 0 for the sector erase (typically 30-50 ms, up to
// 400 ms); core 1 and the interrupt handlers carry on, as the whole program
// runs from SRAM (COPY_TO_RAM) and nothing reads flash meanwhile.

#include <stdbool.h>
#include <stdint.h>

#define NVFLASH_SECTORS 2
#define NVFLASH_PAGE_SIZE 256

// At boot, while the boot-time flash setup is valid (before video_init
// raises clk_sys).
void nvflash_init(void);

// After the system clock changes: slow the flash read clock down to suit it.
void nvflash_retime(void);

// Sector i's contents (the first page is enough for a settings record).
const uint8_t *nvflash_sector(unsigned i);

// Erase sector i and program page (in RAM: the ROM reads it while the flash
// is out of XIP mode) to its start. Returns whether it reads back correctly.
bool nvflash_write(unsigned i, const uint8_t page[NVFLASH_PAGE_SIZE]);

#endif
