#include "nvflash.h"

#include <assert.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// 03h reads (XIP after nvflash_retime) are rated to 50 MHz on the usual QSPI
// parts.
#define READ_SCK_MAX_HZ 50000000u

// Below the last sector (nvflash.h). The old layout's first sector is now
// the second, so a record saved there is still found.
static_assert(NVFLASH_SECTOR_SIZE == FLASH_SECTOR_SIZE, "");

extern char __flash_binary_end;
static bool usable; // the sectors are clear of the program image

// clk_sys / max_hz rounded up.
static unsigned divisor(uint32_t max_hz) {
	uint32_t sys = clock_get_hz(clk_sys);
	unsigned div = (unsigned)((sys + max_hz - 1) / max_hz);
	return div < 2 ? 2 : div;
}

// The RP2350 ROM's erase/program routines put the QMI into 03h mode at its
// own clk_sys / 12 first (33 MHz at 400 MHz: fine), so the SDK's
// flash_range_* are used as they are. Afterwards the SDK re-runs the XIP
// setup the boot ROM saved, whose divisor was chosen for <= 150 MHz (as low
// as 3: 133 MHz at 400 MHz), so the read mode is set again for this clock.

void nvflash_init(void) {
	usable = (uintptr_t)&__flash_binary_end - XIP_BASE <= NVFLASH_OFFSET;
}

void __no_inline_not_in_flash_func(nvflash_retime)(void) {
	rom_flash_select_xip_read_mode(BOOTROM_XIP_MODE_03H_SERIAL, (uint8_t)divisor(READ_SCK_MAX_HZ));
}

static void write_sector(uint32_t offset, const uint8_t *page) {
	flash_range_erase(offset, FLASH_SECTOR_SIZE);
	flash_range_program(offset, page, FLASH_PAGE_SIZE);
	nvflash_retime();
}

const uint8_t *nvflash_read(uint32_t offset) {
	// Uncached, so a read-back sees the flash, not the XIP cache.
	return (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + offset);
}

const uint8_t *nvflash_sector(unsigned i) {
	return nvflash_read(NVFLASH_OFFSET + i * FLASH_SECTOR_SIZE);
}

void nvflash_erase(uint32_t offset, uint32_t len) {
	uint32_t irq = save_and_disable_interrupts();
	flash_range_erase(offset, len);
	nvflash_retime();
	restore_interrupts(irq);
}

void nvflash_program(uint32_t offset, const uint8_t *data, uint32_t len) {
	uint32_t irq = save_and_disable_interrupts();
	flash_range_program(offset, data, len);
	nvflash_retime();
	restore_interrupts(irq);
}

bool nvflash_write(unsigned i, const uint8_t page[NVFLASH_PAGE_SIZE]) {
	if (!usable || i >= NVFLASH_SECTORS)
		return false;
	write_sector(NVFLASH_OFFSET + i * FLASH_SECTOR_SIZE, page);
	return memcmp(nvflash_sector(i), page, NVFLASH_PAGE_SIZE) == 0;
}
