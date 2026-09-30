#include "nvflash.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#if PICO_RP2040
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/xip_ctrl.h"
#endif

// Flash serial clock limits. 03h reads (XIP after nvflash_retime) are rated
// to 50 MHz on the usual QSPI parts; the RP2040's erase/program commands run
// at half that for margin, as its SSI has no RX sample delay set.
#define READ_SCK_MAX_HZ 50000000u
#define COMMAND_SCK_MAX_HZ 25000000u
#define BLOCK_ERASE_CMD 0xd8

#define NVFLASH_OFFSET (PICO_FLASH_SIZE_BYTES - NVFLASH_SECTORS * FLASH_SECTOR_SIZE)

extern char __flash_binary_end;
static bool usable; // the sectors are clear of the program image

// clk_sys / max_hz rounded up; the RP2040 SSI divisor must be even.
static unsigned divisor(uint32_t max_hz, bool even) {
	uint32_t sys = clock_get_hz(clk_sys);
	unsigned div = (unsigned)((sys + max_hz - 1) / max_hz);
	if (even)
		div += div & 1;
	return div < 2 ? 2 : div;
}

#if PICO_RP2040
// The RP2040 ROM's flash_exit_xip sets the SSI to divide clk_sys by 6
// (67 MHz at 400 MHz, beyond what reads without an RX sample delay manage:
// see bd dmt.10), and nothing else in the ROM touches the divisor, so it is
// changed between exit_xip and the erase/program calls. Afterwards, boot2
// (a RAM copy, as the SDK does) puts the flash back into XIP mode at its own
// clk_sys / 4, which nvflash_retime slows down again.
static uint32_t boot2_copy[64];

static void __no_inline_not_in_flash_func(ssi_set_divisor)(unsigned div) {
	ssi_hw->ssienr = 0;
	ssi_hw->baudr = div;
	ssi_hw->ssienr = 1;
}

static void __no_inline_not_in_flash_func(erase_program)(uint32_t offset, const uint8_t *page,
	unsigned command_div, unsigned read_div) {
	uint32_t pads[count_of(pads_qspi_hw->io)];
	for (size_t i = 0; i < count_of(pads); ++i)
		pads[i] = pads_qspi_hw->io[i];
	uint32_t xip_ctrl = xip_ctrl_hw->ctrl;
	__compiler_memory_barrier();

	rom_connect_internal_flash();
	rom_flash_exit_xip();
	ssi_set_divisor(command_div);
	rom_flash_range_erase(offset, FLASH_SECTOR_SIZE, FLASH_BLOCK_SIZE, BLOCK_ERASE_CMD);
	rom_flash_range_program(offset, page, FLASH_PAGE_SIZE);
	rom_flash_flush_cache(); // also releases the chip select
	((void (*)(void))((uintptr_t)boot2_copy + 1))();
	ssi_set_divisor(read_div);

	for (size_t i = 0; i < count_of(pads); ++i)
		pads_qspi_hw->io[i] = pads[i];
	xip_ctrl_hw->ctrl = xip_ctrl;
}

void nvflash_init(void) {
	memcpy(boot2_copy, (const void *)XIP_BASE, sizeof boot2_copy);
	usable = (uintptr_t)&__flash_binary_end - XIP_BASE <= NVFLASH_OFFSET;
}

void nvflash_retime(void) {
	ssi_set_divisor(divisor(READ_SCK_MAX_HZ, true));
}

static void write_sector(uint32_t offset, const uint8_t *page) {
	erase_program(offset, page, divisor(COMMAND_SCK_MAX_HZ, true), divisor(READ_SCK_MAX_HZ, true));
}
#else
// The RP2350 ROM's erase/program routines put the QMI into 03h mode at its
// own clk_sys / 12 first (33 MHz at 400 MHz: fine), so the SDK's
// flash_range_* are used as they are. Afterwards the SDK re-runs the XIP
// setup the boot ROM saved, whose divisor was chosen for <= 150 MHz (as low
// as 3: 133 MHz at 400 MHz), so the read mode is set again for this clock.

void nvflash_init(void) {
	usable = (uintptr_t)&__flash_binary_end - XIP_BASE <= NVFLASH_OFFSET;
}

void __no_inline_not_in_flash_func(nvflash_retime)(void) {
	rom_flash_select_xip_read_mode(BOOTROM_XIP_MODE_03H_SERIAL, (uint8_t)divisor(READ_SCK_MAX_HZ, false));
}

static void write_sector(uint32_t offset, const uint8_t *page) {
	flash_range_erase(offset, FLASH_SECTOR_SIZE);
	flash_range_program(offset, page, FLASH_PAGE_SIZE);
	nvflash_retime();
}
#endif

const uint8_t *nvflash_sector(unsigned i) {
	// Uncached, so a read-back sees the flash, not the XIP cache.
	return (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + NVFLASH_OFFSET + i * FLASH_SECTOR_SIZE);
}

bool nvflash_write(unsigned i, const uint8_t page[NVFLASH_PAGE_SIZE]) {
	if (!usable || i >= NVFLASH_SECTORS)
		return false;
	write_sector(NVFLASH_OFFSET + i * FLASH_SECTOR_SIZE, page);
	return memcmp(nvflash_sector(i), page, NVFLASH_PAGE_SIZE) == 0;
}
