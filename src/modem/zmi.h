#ifndef ZMI_H
#define ZMI_H

// The WiFi modem's firmware image container, ZIMODEM.ZMI (bd xir.17.13):
// what the terminal will flash into the module (xir.17.10), with what it
// is for, so a wrong file is refused before anything is erased. Written by
// src/modem/zmi_gen.py (scripts/build-zimodem.sh, the release ZIP's
// modem/); read here, a chunk at a time, as it comes off the card or a
// stick. Portable C, host-tested (test_zmi).
//
// Little-endian:
//   0  "ZMI1"             magic
//   4  u16 header size    ZMI_HEADER_SIZE, or more: a later version's extra
//                         fields, skipped (covered by the CRC)
//   6  u8  chip           ZMI_CHIP_*
//   7  u8  0
//   8  char[16] module    the module it is for (BOARD_MODEM_NAME: "esp12f"),
//                         NUL-padded
//  24  char[24] version   Zimodem's version, as ATI4 reports it, NUL-padded
//  48  u32 flash size     bytes of flash the image is built for
//  52  u32 parts          how many parts follow (1 to ZMI_MAX_PARTS)
//  56  u32 parts size     bytes from the header's end to the CRC: each part's
//                         8-byte header and its data
//  60  u32 0
// then each part: u32 flash address (a multiple of 4 KB, ascending, not
// overlapping, inside the flash size), u32 length (> 0), the data; then
// u32 CRC-32 (crc32.h) of every byte before it.
//
// An ESP8266 container has a part at address 0, an ESP image: 0xE9 first,
// and the flash size in its header (byte 3, high nibble) is the
// container's.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZMI_HEADER_SIZE 64
#define ZMI_HEADER_MAX 256
#define ZMI_MODULE_MAX 15
#define ZMI_VERSION_MAX 23
#define ZMI_MAX_PARTS 16
#define ZMI_SECTOR 4096

#define ZMI_CHIP_ESP8266 1

typedef enum {
	ZMI_OK,           // fine so far (zmi_feed), or whole and correct (zmi_finish)
	ZMI_TRUNCATED,    // the file ends early
	ZMI_BAD_MAGIC,    // not a container
	ZMI_BAD_HEADER,   // a field out of range: an unknown chip, a flash size, the part count
	ZMI_BAD_PART,     // a part out of order, unaligned, empty, past the flash or the parts size
	ZMI_BAD_IMAGE,    // no ESP image at address 0, or its flash size differs
	ZMI_BAD_CRC,      // damaged
	ZMI_TOO_LONG,     // bytes after the CRC
	ZMI_WRONG_CHIP,   // zmi_check_target: for another chip
	ZMI_WRONG_MODULE, // ... another module
	ZMI_TOO_BIG,      // ... more flash than the module has
} zmi_result_t;

typedef struct {
	uint8_t chip;
	char module[ZMI_MODULE_MAX + 1];
	char version[ZMI_VERSION_MAX + 1];
	uint32_t flash_size;
	uint32_t parts;
	uint32_t parts_size;
	uint32_t image_bytes; // the parts' data, so far (all of it after zmi_finish)
} zmi_info_t;

// The parts' data as it comes, in order: address is where data goes.
// Called before the CRC is known: check the whole file first (no callback),
// then read it again to use it.
typedef void (*zmi_data_t)(void *ctx, uint32_t address, const uint8_t *data, size_t len);
// A part begins: its address and length, before its data.
typedef void (*zmi_part_t)(void *ctx, uint32_t address, uint32_t length);

typedef struct {
	zmi_info_t info; // valid once the header is in (zmi_header_done)
	zmi_data_t data;
	zmi_part_t part;
	void *ctx;
	zmi_result_t result; // the first error, kept
	uint8_t state;
	uint8_t buf[ZMI_HEADER_SIZE]; // the header, then a part's header, then the CRC
	uint32_t have;      // bytes of buf (or of the extra header) so far
	uint32_t header_size;
	uint32_t parts_seen;
	uint32_t parts_used; // bytes of the parts size so far
	uint32_t part_start; // the current part's address
	uint32_t address;   // where its next byte goes
	uint32_t left;      // its bytes still to come
	uint32_t next_free; // the least address the next part may start at
	uint8_t esp[4];     // the start of the part at address 0
	uint8_t esp_have;
	bool has_image;
	uint32_t crc;
} zmi_reader_t;

void zmi_init(zmi_reader_t *r, zmi_data_t data, void *ctx);

// Also tell part (NULL: nothing) where each part begins (after zmi_init).
void zmi_set_part_callback(zmi_reader_t *r, zmi_part_t part);

// The next bytes of the file. ZMI_OK while it is fine so far; the first
// error otherwise (later calls return it too).
zmi_result_t zmi_feed(zmi_reader_t *r, const uint8_t *data, size_t len);

// The file has ended: ZMI_OK only if it was whole, every check passed and
// the CRC matches.
zmi_result_t zmi_finish(zmi_reader_t *r);

// Whether the header has been read (r->info is valid).
bool zmi_header_done(const zmi_reader_t *r);

// Whether the container is for this module: its chip, its name and at
// most flash_size bytes of flash.
zmi_result_t zmi_check_target(const zmi_info_t *info, uint8_t chip, const char *module, uint32_t flash_size);

// A short reason for the user ("damaged (CRC)", "for another module", ...).
const char *zmi_result_text(zmi_result_t result);

#endif
