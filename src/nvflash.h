#ifndef NVFLASH_H
#define NVFLASH_H

// NVFLASH_SECTORS sectors near the end of the board's flash, for settings
// kept across power cycles and reflashing. The very last sector is left
// alone: the SDK's UF2 files start with an RP2350-E10 workaround block for
// 0x10ffff00, which wraps round to the last page of a smaller flash and so
// erases that sector on every drag-and-drop flash. Written with the DVI clock running (400 MHz): the
// flash serial clock is kept within spec at any clk_sys, see nvflash.c.
//
// Writing blocks core 0 for the sector erase (typically 30-50 ms, up to
// 400 ms); core 1 and the interrupt handlers carry on, as the whole program
// runs from SRAM (COPY_TO_RAM) and nothing reads flash meanwhile.

#include <stdbool.h>
#include <stdint.h>

#define NVFLASH_SECTORS 2
#define NVFLASH_PAGE_SIZE 256
#define NVFLASH_SECTOR_SIZE 4096u
// Where the settings sectors start, from the start of flash.
#define NVFLASH_OFFSET (PICO_FLASH_SIZE_BYTES - (NVFLASH_SECTORS + 1) * NVFLASH_SECTOR_SIZE)

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

// Raw access for the firmware update (fwupdate.c), at offsets from the
// start of flash. Interrupts on this core are off during each erase and
// program, as the flash is out of XIP mode then and a handler reading const
// data would read garbage.

// Erase len bytes (whole sectors) at offset.
void nvflash_erase(uint32_t offset, uint32_t len);
// Program len bytes (whole pages) at offset from data (in RAM).
void nvflash_program(uint32_t offset, const uint8_t *data, uint32_t len);
// The flash at offset, read uncached (sees what was just written).
const uint8_t *nvflash_read(uint32_t offset);

#endif
