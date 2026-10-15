#include "nvflash.h"

#include <assert.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"

#include "board.h"

// 03h reads (XIP after nvflash_retime) are rated to 50 MHz on the usual QSPI
// parts.
#define READ_SCK_MAX_HZ 50000000u
// EBh quad reads (bd cpx.2) are rated to 104-133 MHz, but past ~66 MHz the
// QMI may need its RX delay tuned: start at the 03h clock, which already
// moves 4x the bits per clock and so speeds up the cold code in flash.
#define QUAD_SCK_MAX_HZ 50000000u
// Where nvflash_retime compares an 03h read with a quad one: the image's
// first bytes (the vector table), never all 0xFF or all 0.
#define CHECK_OFFSET 0u
#define CHECK_WORDS 16u

// Below the last sector (nvflash.h). The old layout's first sector is now
// the second, so a record saved there is still found.
static_assert(NVFLASH_SECTOR_SIZE == FLASH_SECTOR_SIZE, "");

extern char __flash_binary_end;
static bool usable; // the sectors are clear of the program image
static bool quad;   // XIP reads in EBh quad mode, else 03h
static uint32_t read_hz;

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

// Winbond-style parts (and GigaDevice's) answer EBh only with QE, bit 1 of
// status register 2, set. Some ship with it set, some not, and the ROM does
// not set it, so set it here: a volatile write (50h, then 31h), which wears
// nothing and is gone at power-off. Parts that keep QE elsewhere or lack
// these commands ignore them; nvflash_retime's check then keeps 03h.
static void set_quad_enable(void) {
	uint8_t sr2[2] = {0x35, 0};
	flash_do_cmd(sr2, sr2, sizeof sr2);
	if (sr2[1] & 0x02) {
		return;
	}
	const uint8_t volatile_enable[1] = {0x50};
	uint8_t rx[2];
	flash_do_cmd(volatile_enable, rx, 1);
	const uint8_t write[2] = {0x31, (uint8_t)(sr2[1] | 0x02)}; // the other bits as they were
	flash_do_cmd(write, rx, sizeof write);
}

void nvflash_init(void) {
	usable = (uintptr_t)&__flash_binary_end - XIP_BASE <= NVFLASH_OFFSET;
	if (BOARD_FLASH_QUAD) {
		set_quad_enable();
	}
}

bool nvflash_quad(void) {
	return quad;
}

uint32_t nvflash_read_hz(void) {
	return read_hz;
}

// The ROM's read mode setup may configure the QMI's second chip select too:
// a PSRAM there (extram.h) keeps its own timing and commands.
//
// Quad reads (BOARD_FLASH_QUAD) are taken only if they read what 03h read:
// a board without all four data lines wired, or a part without QE set,
// reads something else, and stays at 03h. All of this runs from SRAM with
// no flash reads but the uncached check ones, so a failed switch cannot
// hang.
void __no_inline_not_in_flash_func(nvflash_retime)(void) {
	uint32_t timing = qmi_hw->m[1].timing, rfmt = qmi_hw->m[1].rfmt, rcmd = qmi_hw->m[1].rcmd;
	uint32_t wfmt = qmi_hw->m[1].wfmt, wcmd = qmi_hw->m[1].wcmd;
	unsigned div = divisor(READ_SCK_MAX_HZ);
	rom_flash_select_xip_read_mode(BOOTROM_XIP_MODE_03H_SERIAL, (uint8_t)div);
	quad = false;
	if (BOARD_FLASH_QUAD) {
		const volatile uint32_t *check = (const volatile uint32_t *)(XIP_NOCACHE_NOALLOC_BASE + CHECK_OFFSET);
		uint32_t want[CHECK_WORDS];
		for (unsigned i = 0; i < CHECK_WORDS; ++i) {
			want[i] = check[i];
		}
		unsigned qdiv = divisor(QUAD_SCK_MAX_HZ);
		rom_flash_select_xip_read_mode(BOOTROM_XIP_MODE_EBH_QUAD, (uint8_t)qdiv);
		quad = true;
		for (unsigned i = 0; i < CHECK_WORDS; ++i) {
			quad = quad && check[i] == want[i];
		}
		if (quad) {
			div = qdiv;
		} else {
			// Back to 03h. A status read takes the flash out of any
			// continuous read mode it got into (the SDK's flash_do_cmd
			// sends the ROM's exit sequence and puts a PSRAM on CS1
			// back as it was).
			uint8_t sr[2] = {0x05, 0};
			flash_do_cmd(sr, sr, sizeof sr);
			rom_flash_select_xip_read_mode(BOOTROM_XIP_MODE_03H_SERIAL, (uint8_t)div);
		}
	}
	read_hz = clock_get_hz(clk_sys) / div;
	qmi_hw->m[1].timing = timing;
	qmi_hw->m[1].rfmt = rfmt;
	qmi_hw->m[1].rcmd = rcmd;
	qmi_hw->m[1].wfmt = wfmt;
	qmi_hw->m[1].wcmd = wcmd;
}

// Interrupts off throughout, as in nvflash_erase/program: XIP is off, and
// some handlers' code is in flash (the USB host's, ld/, bd qur.24).
static void write_sector(uint32_t offset, const uint8_t *page) {
	uint32_t irq = save_and_disable_interrupts();
	flash_range_erase(offset, FLASH_SECTOR_SIZE);
	flash_range_program(offset, page, FLASH_PAGE_SIZE);
	nvflash_retime();
	restore_interrupts(irq);
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
	if (!usable || i >= NVFLASH_SECTORS) {
		return false;
	}
	write_sector(NVFLASH_OFFSET + i * FLASH_SECTOR_SIZE, page);
	return memcmp(nvflash_sector(i), page, NVFLASH_PAGE_SIZE) == 0;
}
