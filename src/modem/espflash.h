#ifndef ESPFLASH_H
#define ESPFLASH_H

// Flashing the WiFi modem's ESP8266 through its ROM loader (bd xir.17.10),
// over the terminal UART: BOOT (GPIO0) held low across a reset starts the
// loader, which takes SLIP-framed commands (Espressif's serial protocol,
// as esptool speaks it without its stub): SYNC, READ_REG (the chip's
// identity), FLASH_BEGIN (erase) and FLASH_DATA (1 KB blocks, each with a
// checksum the ROM checks), then a reset with BOOT high runs the new
// image. The ROM cannot read the flash back or checksum it (esptool's stub
// can); the per-block checksums and the version the new firmware reports
// afterwards (the start-up's ATI4) are the check. Portable C, host-tested
// against a fake ROM (test_espflash); apps/terminal/modem_glue.c drives it
// from a ZIMODEM.ZMI (zmi.h) on the card or a stick.
//
// Poll-driven, as modem.c: the owner feeds it the line's bytes and the
// image. The image comes as parts (espflash_part, from zmi's part
// callback) and their data (espflash_data, at most espflash_room bytes at
// a time: feed the file in pieces that small, so a part's bytes never
// outrun the block being written).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESPFLASH_BAUD 115200        // the ROM's loader (it finds the rate from SYNC; it cannot change it)
#define ESPFLASH_BLOCK 0x400        // FLASH_DATA's size in the ROM (esptool's FLASH_WRITE_SIZE)
#define ESPFLASH_SECTOR 0x1000
#define ESPFLASH_RESET_MS 50        // RST held low
#define ESPFLASH_BOOT_MS 100        // after RST: the ROM samples GPIO0, then BOOT is released
#define ESPFLASH_SYNC_MS 200        // SYNC again this often
#define ESPFLASH_SYNC_TRIES 25      // ... this many times, then a reset and again
#define ESPFLASH_ATTEMPTS 3         // resets into the loader before giving up
#define ESPFLASH_SYNC_SETTLE_MS 100 // the ROM answers SYNC several times: let them go by
#define ESPFLASH_REPLY_MS 3000      // READ_REG
#define ESPFLASH_DATA_MS 5000       // a block written
#define ESPFLASH_ERASE_MS_PER_MB 30000 // FLASH_BEGIN erases (esptool's ERASE_REGION_TIMEOUT_PER_MB)
#define ESPFLASH_ERASE_MIN_MS 5000

#define ESPFLASH_ESP8266_MAGIC_REG 0x40001000u // esptool's CHIP_DETECT_MAGIC_REG_ADDR
#define ESPFLASH_ESP8266_MAGIC 0xfff0c101u

typedef struct {
	void *ctx;
	void (*hold_reset)(void *ctx, bool hold); // RST: true = held low
	void (*hold_boot)(void *ctx, bool hold);  // GPIO0: true = held low
	void (*set_baud)(void *ctx, unsigned baud);
	void (*send)(void *ctx, const uint8_t *data, size_t len); // queued to the line
} espflash_io_t;

typedef enum {
	ESPFLASH_IDLE,
	ESPFLASH_CONNECTING, // into the loader: reset, SYNC, the chip's identity
	ESPFLASH_READY,      // in the loader: espflash_part next (or espflash_finish)
	ESPFLASH_WRITING,    // a part: erasing, then blocks
	ESPFLASH_DONE,       // written, the module reset into it
	ESPFLASH_FAILED,     // problem says why; the module is left in the loader
} espflash_state_t;

typedef struct {
	espflash_io_t io;
	espflash_state_t state;
	uint8_t step;      // espflash.c
	uint8_t attempt;   // resets into the loader so far
	uint8_t tries;     // SYNCs since the reset
	uint32_t since;    // when the step began
	uint32_t timeout;  // for the reply awaited
	uint8_t waiting;   // the command whose reply is awaited, 0: none
	// The reply being read (SLIP).
	uint8_t rx[64];
	uint8_t rx_len;
	bool rx_in, rx_esc, rx_over;
	// The part being written.
	uint32_t part_address, part_left; // bytes still to come from the owner
	uint32_t seq, blocks;
	uint8_t block[ESPFLASH_BLOCK];
	uint32_t fill;     // bytes in block
	bool block_ready;  // full, or the part's last
	// Progress, for the panel.
	uint32_t written;  // bytes the ROM has taken
	uint32_t total;    // the image's, as the owner set it (espflash_start)
	const char *problem;
	uint8_t error;     // the ROM's error code for a refused command, 0: none
} espflash_t;

void espflash_init(espflash_t *f, const espflash_io_t *io);

// Reset the module into its loader and connect (ESPFLASH_CONNECTING, then
// ESPFLASH_READY). total: the image's bytes, for the progress.
void espflash_start(espflash_t *f, uint32_t total, uint32_t now_ms);

// A byte from the line.
void espflash_feed(espflash_t *f, uint8_t c, uint32_t now_ms);

// Call regularly: replies, timeouts, the next block.
void espflash_poll(espflash_t *f, uint32_t now_ms);

// A part of the image begins (only when ESPFLASH_READY): erase its
// sectors, then take its data.
void espflash_part(espflash_t *f, uint32_t address, uint32_t length, uint32_t now_ms);

// How many bytes of the image espflash_data takes now (0: busy). Feed the
// file at most this many bytes at a time (in ESPFLASH_READY too: the next
// part's header and the start of its data).
size_t espflash_room(const espflash_t *f);

// The current part's next bytes (at most espflash_room).
void espflash_data(espflash_t *f, const uint8_t *data, size_t len, uint32_t now_ms);

// All of the image is written (ESPFLASH_READY): reset the module into it,
// BOOT high (ESPFLASH_DONE).
void espflash_finish(espflash_t *f, uint32_t now_ms);

// Stop: the module reset with BOOT high, whatever it holds (ESPFLASH_FAILED
// unless done).
void espflash_abort(espflash_t *f, const char *problem);

// Where it is, for the panel ("connecting", "erasing", "writing", ...).
const char *espflash_step_name(const espflash_t *f);

// The ESP8266 ROM's erase size for FLASH_BEGIN: its erase routine has a
// bug esptool works around (ESP8266ROM.get_erase_size), erasing more than
// asked; this asks for less so that the right sectors go.
uint32_t espflash_erase_size(uint32_t offset, uint32_t size);

#endif
