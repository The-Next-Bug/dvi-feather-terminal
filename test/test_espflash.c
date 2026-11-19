// Flashing the WiFi modem through the ESP8266 ROM loader (src/modem/
// espflash.[ch], bd xir.17.10) against a fake ROM: it enters the loader
// only if BOOT is low when RST is let go, answers SYNC several times,
// erases as the real ROM does (more than asked: espflash_erase_size asks
// for less), checks each block's checksum and sequence and refuses writes
// to sectors not erased. The image comes from a ZIMODEM.ZMI (zmi.h) fed as
// modem_glue.c feeds it.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "espflash.h"
#include "test.h"
#include "zmi.h"

#define FLASH_SIZE 0x400000u
#define SECTOR 0x1000u

// ---- the fake ROM ----

typedef struct {
	// Its pins and line, as the flasher sets them.
	bool reset_held, boot_held;
	bool in_loader;     // reset let go with BOOT low
	unsigned baud;
	// Failures to show.
	bool silent;        // never answers (not wired)
	uint32_t magic;     // READ_REG's answer
	int refuse_block;   // FLASH_DATA seq it refuses (-1: none)
	int lose_block;     // ... it does not answer (-1: none)
	bool boot_text;     // noise after each reset, as the ROM's 74880 text looks at 115200
	// Its flash.
	uint8_t *flash;
	bool erased[FLASH_SIZE / SECTOR];
	bool wrote_unerased;
	uint32_t begin_offset, begin_blocks, erase_asked;
	uint32_t next_seq;
	unsigned begins, blocks, bad_checksums, syncs;
	// The frame being read, and its replies waiting to go out.
	uint8_t frame[2048];
	size_t frame_len;
	bool in_frame, esc;
	uint8_t out[8192];
	size_t out_len, out_pos;
	// The order of pin changes: "B" BOOT low, "b" released, "R"/"r" RST.
	char pins[64];
} rom_t;

static rom_t rom;

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v) {
	for (int i = 0; i < 4; ++i) {
		p[i] = (uint8_t)(v >> (8 * i));
	}
}

static void out_byte(uint8_t c) {
	if (rom.out_len < sizeof rom.out) {
		rom.out[rom.out_len++] = c;
	}
}

static void out_slip(uint8_t c) {
	if (c == 0xc0) {
		out_byte(0xdb);
		out_byte(0xdc);
	} else if (c == 0xdb) {
		out_byte(0xdb);
		out_byte(0xdd);
	} else {
		out_byte(c);
	}
}

static void respond(uint8_t cmd, uint32_t value, uint8_t status, uint8_t error) {
	uint8_t r[10] = {1, cmd, 2, 0};
	put32(r + 4, value);
	r[8] = status;
	r[9] = error;
	out_byte(0xc0);
	for (size_t i = 0; i < sizeof r; ++i) {
		out_slip(r[i]);
	}
	out_byte(0xc0);
}

// The real ROM's erase: given n sectors it erases n + head (or 2n when
// n < head), head being the sectors to the next 64 KB block.
static void rom_erase(uint32_t offset, uint32_t size) {
	uint32_t start = offset / SECTOR, asked = (size + SECTOR - 1) / SECTOR;
	uint32_t head = 16 - start % 16;
	uint32_t sectors = asked < head ? 2 * asked : asked + head;
	for (uint32_t s = start; s < start + sectors && s < FLASH_SIZE / SECTOR; ++s) {
		rom.erased[s] = true;
		memset(rom.flash + s * SECTOR, 0xff, SECTOR);
	}
}

static void rom_command(const uint8_t *f, size_t len) {
	if (len < 8 || f[0] != 0) {
		return;
	}
	uint8_t cmd = f[1];
	size_t size = f[2] | (size_t)f[3] << 8;
	const uint8_t *data = f + 8;
	if (8 + size != len) {
		respond(cmd, 0, 1, 0xc0);
		return;
	}
	switch (cmd) {
	case 0x08: // SYNC: answered several times
		++rom.syncs;
		for (int i = 0; i < 8; ++i) {
			respond(cmd, 0x20120707, 0, 0);
		}
		break;
	case 0x0a: // READ_REG
		respond(cmd, get32(data) == ESPFLASH_ESP8266_MAGIC_REG ? rom.magic : 0, 0, 0);
		break;
	case 0x02: // FLASH_BEGIN
		++rom.begins;
		rom.erase_asked = get32(data);
		rom.begin_blocks = get32(data + 4);
		rom.begin_offset = get32(data + 12);
		rom.next_seq = 0;
		rom_erase(rom.begin_offset, rom.erase_asked);
		respond(cmd, 0, get32(data + 8) == 0x400 ? 0 : 1, 0);
		break;
	case 0x03: { // FLASH_DATA
		uint32_t n = get32(data), seq = get32(data + 4);
		const uint8_t *block = data + 16;
		uint32_t sum = 0xef;
		for (uint32_t i = 0; i < n && 16 + i < size; ++i) {
			sum ^= block[i];
		}
		if ((int)seq == rom.lose_block) {
			return;
		}
		if (sum != get32(f + 4) || n != 0x400 || 16 + n != size) {
			++rom.bad_checksums;
			respond(cmd, 0, 1, 0xc1);
			return;
		}
		if ((int)seq == rom.refuse_block || seq != rom.next_seq || seq >= rom.begin_blocks) {
			respond(cmd, 0, 1, 0xc2);
			return;
		}
		uint32_t at = rom.begin_offset + seq * 0x400;
		for (uint32_t i = 0; i < n; ++i) {
			if (!rom.erased[(at + i) / SECTOR]) {
				rom.wrote_unerased = true;
			}
			rom.flash[at + i] &= block[i]; // flash bits only clear
		}
		++rom.next_seq;
		++rom.blocks;
		respond(cmd, 0, 0, 0);
		break;
	}
	default:
		respond(cmd, 0, 1, 0x05);
	}
}

static void rom_receive(uint8_t c) {
	if (!rom.in_loader || rom.silent || rom.baud != ESPFLASH_BAUD) {
		return;
	}
	if (c == 0xc0) {
		if (rom.in_frame && rom.frame_len) {
			rom_command(rom.frame, rom.frame_len);
		}
		rom.in_frame = true;
		rom.frame_len = 0;
		rom.esc = false;
		return;
	}
	if (!rom.in_frame) {
		return;
	}
	if (rom.esc) {
		rom.esc = false;
		c = c == 0xdc ? 0xc0 : c == 0xdd ? 0xdb : c;
	} else if (c == 0xdb) {
		rom.esc = true;
		return;
	}
	if (rom.frame_len < sizeof rom.frame) {
		rom.frame[rom.frame_len++] = c;
	}
}

static void pin(char c) {
	size_t n = strlen(rom.pins);
	if (n + 1 < sizeof rom.pins) {
		rom.pins[n] = c;
		rom.pins[n + 1] = '\0';
	}
}

static void io_reset(void *ctx, bool hold) {
	(void)ctx;
	if (rom.reset_held && !hold) {
		rom.in_loader = rom.boot_held;
		rom.in_frame = false;
		if (rom.boot_text) {
			static const uint8_t noise[] = {0x00, 0xc0, 0x13, 0xfe, 0xc0, 0x55, 0x01, 0x08, 0xc0, 0x7f};
			for (size_t i = 0; i < sizeof noise; ++i) {
				out_byte(noise[i]);
			}
		}
	}
	rom.reset_held = hold;
	pin(hold ? 'R' : 'r');
}

static void io_boot(void *ctx, bool hold) {
	(void)ctx;
	if (hold != rom.boot_held) {
		pin(hold ? 'B' : 'b');
	}
	rom.boot_held = hold;
}

static void io_baud(void *ctx, unsigned baud) {
	(void)ctx;
	rom.baud = baud;
}

static void io_send(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	for (size_t i = 0; i < len; ++i) {
		rom_receive(data[i]);
	}
}

static const espflash_io_t io = {NULL, io_reset, io_boot, io_baud, io_send};

static void rom_init(void) {
	uint8_t *flash = rom.flash;
	memset(&rom, 0, sizeof rom);
	rom.flash = flash ? flash : malloc(FLASH_SIZE);
	memset(rom.flash, 0x5a, FLASH_SIZE); // the old firmware
	rom.magic = ESPFLASH_ESP8266_MAGIC;
	rom.refuse_block = rom.lose_block = -1;
}

// ---- the container, and the owner's loop ----

static uint8_t file[0x20000];
static size_t file_len;
static uint8_t image[2500], extra[3000];

typedef struct {
	uint32_t address;
	const uint8_t *data;
	uint32_t length;
} part_t;

static void build(const part_t *parts, unsigned n) {
	memset(file, 0, ZMI_HEADER_SIZE);
	memcpy(file, "ZMI1", 4);
	file[4] = ZMI_HEADER_SIZE;
	file[6] = ZMI_CHIP_ESP8266;
	memcpy(file + 8, "esp12f", 6);
	memcpy(file + 24, "4.0.3-1", 7);
	put32(file + 48, FLASH_SIZE);
	put32(file + 52, n);
	size_t at = ZMI_HEADER_SIZE;
	for (unsigned i = 0; i < n; ++i) {
		put32(file + at, parts[i].address);
		put32(file + at + 4, parts[i].length);
		memcpy(file + at + 8, parts[i].data, parts[i].length);
		at += 8 + parts[i].length;
	}
	put32(file + 56, (uint32_t)(at - ZMI_HEADER_SIZE));
	put32(file + at, crc32_update(0, file, at));
	file_len = at + 4;
}

static void make_data(void) {
	for (size_t i = 0; i < sizeof image; ++i) {
		image[i] = (uint8_t)(i * 13 + 5);
	}
	image[0] = 0xe9;
	image[3] = 0x40; // 4 MB
	image[100] = 0xc0; // SLIP's own bytes in the data
	image[101] = 0xdb;
	for (size_t i = 0; i < sizeof extra; ++i) {
		extra[i] = (uint8_t)(i ^ 0x3c);
	}
}

static espflash_t fl;
static uint32_t now;

static void on_part(void *ctx, uint32_t address, uint32_t length) {
	(void)ctx;
	espflash_part(&fl, address, length, now);
}

static void on_data(void *ctx, uint32_t address, const uint8_t *data, size_t len) {
	(void)ctx;
	(void)address;
	espflash_data(&fl, data, len, now);
}

// Flash file as modem_glue.c will: the line's bytes in, the file in pieces
// of at most espflash_room, then espflash_finish. The final state.
static espflash_state_t run(void) {
	zmi_reader_t r;
	zmi_init(&r, on_data, NULL);
	zmi_set_part_callback(&r, on_part);
	espflash_init(&fl, &io);
	now = 1000;
	espflash_start(&fl, (uint32_t)file_len, now);
	size_t at = 0;
	for (int ms = 0; ms < 120000; ++ms, ++now) {
		while (rom.out_pos < rom.out_len) {
			espflash_feed(&fl, rom.out[rom.out_pos++], now);
		}
		rom.out_len = rom.out_pos = 0;
		espflash_poll(&fl, now);
		size_t room;
		while (at < file_len && (room = espflash_room(&fl)) > 0) {
			size_t n = room < file_len - at ? room : file_len - at;
			zmi_feed(&r, file + at, n);
			at += n;
		}
		if (at == file_len && fl.state == ESPFLASH_READY) {
			if (zmi_finish(&r) != ZMI_OK) {
				espflash_abort(&fl, "the file is damaged");
			} else {
				espflash_finish(&fl, now);
			}
		}
		if (fl.state == ESPFLASH_DONE || fl.state == ESPFLASH_FAILED) {
			break;
		}
	}
	return fl.state;
}

static void test_erase_size(void) {
	// esptool's ESP8266ROM.get_erase_size.
	CHECK_EQ(espflash_erase_size(0, 409968), 348160);
	CHECK_EQ(espflash_erase_size(0, 4096), 4096);
	CHECK_EQ(espflash_erase_size(0x3000, 0x2000), 4096);
	CHECK_EQ(espflash_erase_size(0, 0x10000), 0x8000);
	CHECK_EQ(espflash_erase_size(0x10000, 0x40000), 0x30000);
}

static void test_flash(void) {
	make_data();
	rom_init();
	rom.boot_text = true;
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	CHECK_EQ(run(), ESPFLASH_DONE);
	CHECK(fl.problem == NULL);
	CHECK(strncmp(rom.pins, "BRrb", 4) == 0); // BOOT low, a reset, BOOT released after it
	CHECK(!rom.boot_held && !rom.reset_held);
	CHECK(memcmp(rom.flash, image, sizeof image) == 0);
	for (uint32_t i = sizeof image; i < 3 * 0x400; ++i) { // the last block padded with 0xFF
		CHECK_EQ(rom.flash[i], 0xff);
	}
	CHECK_EQ(rom.flash[2 * SECTOR], 0x5a); // the ROM erased sectors 0-1 for one asked: no further
	CHECK(!rom.wrote_unerased);
	CHECK_EQ(rom.begins, 1);
	CHECK_EQ(rom.blocks, 3);
	CHECK_EQ(rom.bad_checksums, 0);
	CHECK_EQ(rom.erase_asked, espflash_erase_size(0, sizeof image));
	CHECK_EQ(fl.written, sizeof image);
}

static void test_two_parts(void) {
	make_data();
	rom_init();
	part_t parts[] = {{0, image, sizeof image}, {0x23000, extra, sizeof extra}};
	build(parts, 2);
	CHECK_EQ(run(), ESPFLASH_DONE);
	CHECK(memcmp(rom.flash, image, sizeof image) == 0);
	CHECK(memcmp(rom.flash + 0x23000, extra, sizeof extra) == 0);
	CHECK(!rom.wrote_unerased);
	CHECK_EQ(rom.begins, 2);
	CHECK_EQ(fl.written, sizeof image + sizeof extra);
}

static void test_no_loader(void) {
	make_data();
	rom_init();
	rom.silent = true;
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	CHECK_EQ(run(), ESPFLASH_FAILED);
	CHECK(strstr(fl.problem, "no answer from the loader") != NULL);
	CHECK(!rom.boot_held && !rom.reset_held); // let go
	CHECK_EQ(rom.flash[0], 0x5a);             // nothing written
	CHECK(now - 1000 >= ESPFLASH_ATTEMPTS * ESPFLASH_SYNC_TRIES * ESPFLASH_SYNC_MS); // every try
}

static void test_wrong_chip(void) {
	make_data();
	rom_init();
	rom.magic = 0x00f01d83; // an ESP32
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	CHECK_EQ(run(), ESPFLASH_FAILED);
	CHECK(strstr(fl.problem, "not an ESP8266") != NULL);
	CHECK_EQ(rom.begins, 0); // refused before erasing
}

static void test_refused_block(void) {
	make_data();
	rom_init();
	rom.refuse_block = 1;
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	CHECK_EQ(run(), ESPFLASH_FAILED);
	CHECK(strstr(fl.problem, "refused a block") != NULL);
	CHECK_EQ(fl.error, 0xc2);
	CHECK(!rom.boot_held);
}

static void test_lost_reply(void) {
	make_data();
	rom_init();
	rom.lose_block = 2;
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	CHECK_EQ(run(), ESPFLASH_FAILED);
	CHECK(strstr(fl.problem, "no answer to a block") != NULL);
}

static void test_damaged_file(void) {
	make_data();
	rom_init();
	part_t parts[] = {{0, image, sizeof image}};
	build(parts, 1);
	file[file_len - 1] ^= 1; // the CRC: found only at the end, after writing
	CHECK_EQ(run(), ESPFLASH_FAILED);
	CHECK(strstr(fl.problem, "damaged") != NULL);
}

int main(void) {
	RUN(test_erase_size);
	RUN(test_flash);
	RUN(test_two_parts);
	RUN(test_no_loader);
	RUN(test_wrong_chip);
	RUN(test_refused_block);
	RUN(test_lost_reply);
	RUN(test_damaged_file);
	free(rom.flash);
	return tests_done();
}
