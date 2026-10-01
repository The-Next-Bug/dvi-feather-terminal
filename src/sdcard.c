// microSD over SPI for FatFs (sdcard.h). The SD Physical Layer Simplified
// Specification's SPI mode: CMD0 into SPI mode, CMD8 for v2 cards, ACMD41
// until ready, CMD58 for the addressing, then CMD17/CMD24 per block.

#include "sdcard.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "ff.h"
#include "diskio.h"

#define SD_SPI spi0
#define INIT_HZ 400000     // the spec's limit until initialised
#define RUN_HZ 25000000    // default-speed cards
#define BLOCK 512

#define CMD0 0    // GO_IDLE_STATE
#define CMD8 8    // SEND_IF_COND
#define CMD9 9    // SEND_CSD
#define CMD16 16  // SET_BLOCKLEN
#define CMD17 17  // READ_SINGLE_BLOCK
#define CMD24 24  // WRITE_BLOCK
#define CMD55 55  // APP_CMD
#define CMD58 58  // READ_OCR
#define ACMD41 41 // SD_SEND_OP_COND

#define R1_IDLE 0x01
#define R1_ILLEGAL 0x04
#define TOKEN_START 0xfe

static sdcard_type_t type;
static uint32_t blocks;
static bool pins_ready;

sdcard_type_t sdcard_type(void) {
	return type;
}

uint32_t sdcard_blocks(void) {
	return blocks;
}

static uint8_t xfer(uint8_t out) {
	uint8_t in;
	spi_write_read_blocking(SD_SPI, &out, &in, 1);
	return in;
}

// Wait until the card stops holding DO low (busy), up to ms.
static bool wait_ready(uint32_t ms) {
	absolute_time_t until = make_timeout_time_ms(ms);
	do {
		if (xfer(0xff) == 0xff)
			return true;
	} while (!time_reached(until));
	return false;
}

static void deselect(void) {
	gpio_put(SDCARD_CS_PIN, 1);
	xfer(0xff); // the card releases DO on the next clock
}

static bool select_card(void) {
	gpio_put(SDCARD_CS_PIN, 0);
	xfer(0xff);
	if (wait_ready(500))
		return true;
	deselect();
	return false;
}

// Send a command (selecting the card) and return its R1 response, 0xff on
// a timeout. The card stays selected for the rest of the transaction.
static uint8_t command(uint8_t cmd, uint32_t arg) {
	if (cmd == ACMD41) {
		uint8_t r = command(CMD55, 0);
		if (r > R1_IDLE)
			return r;
	}
	deselect();
	if (!select_card())
		return 0xff;
	// Only CMD0 and CMD8 are checked for a CRC in SPI mode.
	uint8_t crc = cmd == CMD0 ? 0x95 : cmd == CMD8 ? 0x87 : 0x01;
	uint8_t frame[6] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8),
		(uint8_t)arg, crc};
	spi_write_blocking(SD_SPI, frame, sizeof frame);
	uint8_t r = 0xff;
	for (int i = 0; i < 10 && (r & 0x80); ++i)
		r = xfer(0xff);
	return r;
}

static bool read_data(uint8_t *buf, unsigned len, uint32_t ms) {
	absolute_time_t until = make_timeout_time_ms(ms);
	uint8_t token;
	while ((token = xfer(0xff)) == 0xff)
		if (time_reached(until))
			return false;
	if (token != TOKEN_START)
		return false;
	spi_read_blocking(SD_SPI, 0xff, buf, len);
	xfer(0xff); // CRC, unchecked
	xfer(0xff);
	return true;
}

static void pins_init(void) {
	if (pins_ready)
		return;
	spi_init(SD_SPI, INIT_HZ);
	spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(SDCARD_MISO_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SDCARD_SCK_PIN, GPIO_FUNC_SPI);
	gpio_set_function(SDCARD_MOSI_PIN, GPIO_FUNC_SPI);
	gpio_pull_up(SDCARD_MISO_PIN); // DO floats until the card drives it
	gpio_init(SDCARD_CS_PIN);
	gpio_set_dir(SDCARD_CS_PIN, GPIO_OUT);
	gpio_put(SDCARD_CS_PIN, 1);
	pins_ready = true;
}

// Bits hi..lo (inclusive) of a 128-bit register, as the spec numbers them.
static uint32_t bits(const uint8_t reg[16], unsigned hi, unsigned lo) {
	uint32_t v = 0;
	for (unsigned b = hi + 1; b-- > lo;)
		v = v << 1 | (reg[15 - b / 8] >> (b % 8) & 1);
	return v;
}

static uint32_t read_capacity(void) {
	uint8_t csd[16];
	uint32_t n = 0;
	if (command(CMD9, 0) == 0 && read_data(csd, sizeof csd, 100)) {
		if (bits(csd, 127, 126) == 1) { // CSD 2.0: SDHC/SDXC
			n = (bits(csd, 69, 48) + 1) * 1024;
		} else {
			// (C_SIZE + 1) << (C_SIZE_MULT + 2) blocks of 2^READ_BL_LEN
			// bytes (READ_BL_LEN >= 9), counted in 512-byte blocks.
			unsigned shift = bits(csd, 49, 47) + 2 + bits(csd, 83, 80) - 9;
			n = (bits(csd, 73, 62) + 1) << shift;
		}
	}
	deselect();
	return n;
}

static sdcard_type_t card_init(void) {
	pins_init();
	spi_set_baudrate(SD_SPI, INIT_HZ);
	// At least 74 clocks with CS high put the card in its native mode,
	// ready for CMD0 with CS low to switch it to SPI mode.
	gpio_put(SDCARD_CS_PIN, 1);
	for (int i = 0; i < 10; ++i)
		xfer(0xff);

	uint8_t r = 0xff;
	for (int i = 0; i < 10 && r != R1_IDLE; ++i)
		r = command(CMD0, 0);
	if (r != R1_IDLE) {
		deselect();
		return SDCARD_NONE;
	}

	bool v2 = false;
	if (command(CMD8, 0x1aa) == R1_IDLE) {
		uint8_t r7[4];
		spi_read_blocking(SD_SPI, 0xff, r7, sizeof r7);
		if (r7[2] != 0x01 || r7[3] != 0xaa) { // 2.7-3.6 V, check pattern
			deselect();
			return SDCARD_NONE;
		}
		v2 = true;
	}

	absolute_time_t until = make_timeout_time_ms(1000);
	while ((r = command(ACMD41, v2 ? 1u << 30 : 0)) == R1_IDLE && !time_reached(until)) {
	}
	if (r != 0) {
		deselect();
		return SDCARD_NONE;
	}

	sdcard_type_t t = SDCARD_SDSC;
	if (v2 && command(CMD58, 0) == 0) {
		uint8_t ocr[4];
		spi_read_blocking(SD_SPI, 0xff, ocr, sizeof ocr);
		if (ocr[0] & 0x40) // CCS
			t = SDCARD_SDHC;
	}
	if (t == SDCARD_SDSC && command(CMD16, BLOCK) != 0) {
		deselect();
		return SDCARD_NONE;
	}
	deselect();
	spi_set_baudrate(SD_SPI, RUN_HZ);
	return t;
}

static uint32_t address(LBA_t sector) {
	return type == SDCARD_SDHC ? (uint32_t)sector : (uint32_t)sector * BLOCK;
}

// ---- FatFs disk I/O (diskio.h), drive 0 only ----

DSTATUS disk_status(BYTE pdrv) {
	return pdrv == 0 && type != SDCARD_NONE ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	if (pdrv != 0)
		return STA_NOINIT;
	type = card_init();
	blocks = type != SDCARD_NONE ? read_capacity() : 0;
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || type == SDCARD_NONE)
		return RES_NOTRDY;
	for (; count; --count, ++sector, buff += BLOCK) {
		bool ok = command(CMD17, address(sector)) == 0 && read_data(buff, BLOCK, 200);
		deselect();
		if (!ok)
			return RES_ERROR;
	}
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || type == SDCARD_NONE)
		return RES_NOTRDY;
	for (; count; --count, ++sector, buff += BLOCK) {
		bool ok = false;
		if (command(CMD24, address(sector)) == 0) {
			xfer(0xff);
			xfer(TOKEN_START);
			spi_write_blocking(SD_SPI, buff, BLOCK);
			xfer(0xff); // CRC, unchecked
			xfer(0xff);
			// Data response xxx0sss1: sss = 010 accepted; then busy.
			ok = (xfer(0xff) & 0x1f) == 0x05 && wait_ready(500);
		}
		deselect();
		if (!ok)
			return RES_ERROR;
	}
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv != 0 || type == SDCARD_NONE)
		return RES_NOTRDY;
	switch (cmd) {
	case CTRL_SYNC: {
		// Writes finish before disk_write returns; just wait out the card.
		bool ok = select_card();
		deselect();
		return ok ? RES_OK : RES_ERROR;
	}
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = blocks;
		return blocks ? RES_OK : RES_ERROR;
	case GET_SECTOR_SIZE:
		*(WORD *)buff = BLOCK;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1; // unknown
		return RES_OK;
	default:
		return RES_PARERR;
	}
}
