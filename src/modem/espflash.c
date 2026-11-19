#include "espflash.h"

#include <string.h>

// The loader's commands (esptool's ESPLoader).
#define CMD_FLASH_BEGIN 0x02
#define CMD_FLASH_DATA 0x03
#define CMD_SYNC 0x08
#define CMD_READ_REG 0x0a

#define SLIP_END 0xc0
#define SLIP_ESC 0xdb
#define SLIP_ESC_END 0xdc
#define SLIP_ESC_ESC 0xdd

#define CHECKSUM_SEED 0xef

enum {
	ST_RESET,   // BOOT and RST low
	ST_BOOT,    // RST released, BOOT still low while the ROM looks
	ST_SYNC,    // SYNC sent, a reply awaited
	ST_SETTLE,  // the ROM's further SYNC replies go by
	ST_DETECT,  // READ_REG of the chip's magic number
	ST_IDLE,    // in the loader, between parts
	ST_BEGIN,   // FLASH_BEGIN: the part's sectors being erased
	ST_DATA,    // a block awaited from the owner
	ST_WRITE,   // FLASH_DATA sent
	ST_STOPPED, // done or failed
};

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void to_step(espflash_t *f, uint8_t step, uint32_t now) {
	f->step = step;
	f->since = now;
}

// Bytes SLIP-escaped onto the line, a few at a time.
static void send_escaped(espflash_t *f, const uint8_t *data, size_t len) {
	uint8_t out[64];
	size_t n = 0;
	for (size_t i = 0; i < len; ++i) {
		if (n + 2 > sizeof out) {
			f->io.send(f->io.ctx, out, n);
			n = 0;
		}
		if (data[i] == SLIP_END) {
			out[n++] = SLIP_ESC;
			out[n++] = SLIP_ESC_END;
		} else if (data[i] == SLIP_ESC) {
			out[n++] = SLIP_ESC;
			out[n++] = SLIP_ESC_ESC;
		} else {
			out[n++] = data[i];
		}
	}
	if (n) {
		f->io.send(f->io.ctx, out, n);
	}
}

// A command: direction 0, the command, the data's size, the checksum
// (FLASH_DATA's: of its block), then the data and, for FLASH_DATA, the block.
static void command(espflash_t *f, uint8_t cmd, const uint8_t *data, size_t len, const uint8_t *block,
	size_t block_len, uint32_t timeout, uint32_t now) {
	uint8_t head[8] = {0, cmd, (uint8_t)(len + block_len), (uint8_t)((len + block_len) >> 8)};
	uint32_t sum = CHECKSUM_SEED;
	for (size_t i = 0; i < block_len; ++i) {
		sum ^= block[i];
	}
	put32(head + 4, block ? sum : 0);
	static const uint8_t end = SLIP_END;
	f->io.send(f->io.ctx, &end, 1);
	send_escaped(f, head, sizeof head);
	send_escaped(f, data, len);
	if (block) {
		send_escaped(f, block, block_len);
	}
	f->io.send(f->io.ctx, &end, 1);
	f->waiting = cmd;
	f->timeout = timeout;
	f->since = now;
}

static void send_sync(espflash_t *f, uint32_t now) {
	uint8_t data[36] = {0x07, 0x07, 0x12, 0x20};
	memset(data + 4, 0x55, 32);
	command(f, CMD_SYNC, data, sizeof data, NULL, 0, ESPFLASH_SYNC_MS, now);
}

static void fail(espflash_t *f, const char *problem) {
	f->problem = problem;
	f->state = ESPFLASH_FAILED;
	f->step = ST_STOPPED;
	f->waiting = 0;
	f->io.hold_boot(f->io.ctx, false);
	f->io.hold_reset(f->io.ctx, false);
}

void espflash_init(espflash_t *f, const espflash_io_t *io) {
	*f = (espflash_t){.io = *io};
}

static void reset_into_loader(espflash_t *f, uint32_t now) {
	f->state = ESPFLASH_CONNECTING;
	f->tries = 0;
	f->waiting = 0;
	f->rx_in = false;
	f->io.hold_boot(f->io.ctx, true);
	f->io.hold_reset(f->io.ctx, true);
	to_step(f, ST_RESET, now);
}

void espflash_start(espflash_t *f, uint32_t total, uint32_t now_ms) {
	f->attempt = 0;
	f->problem = NULL;
	f->error = 0;
	f->written = 0;
	f->total = total;
	f->fill = 0;
	f->block_ready = false;
	f->part_left = 0;
	reset_into_loader(f, now_ms);
}

// The ESP8266 ROM's erase routine erases more than it is asked for: ask
// for less (esptool's ESP8266ROM.get_erase_size).
uint32_t espflash_erase_size(uint32_t offset, uint32_t size) {
	const uint32_t per_block = 16;
	uint32_t sectors = (size + ESPFLASH_SECTOR - 1) / ESPFLASH_SECTOR;
	uint32_t start = offset / ESPFLASH_SECTOR;
	uint32_t head = per_block - start % per_block;
	if (sectors < head) {
		head = sectors;
	}
	if (sectors < 2 * head) {
		return (sectors + 1) / 2 * ESPFLASH_SECTOR;
	}
	return (sectors - head) * ESPFLASH_SECTOR;
}

void espflash_part(espflash_t *f, uint32_t address, uint32_t length, uint32_t now_ms) {
	if (f->state != ESPFLASH_READY) {
		fail(f, "a part came while busy");
		return;
	}
	f->state = ESPFLASH_WRITING;
	f->part_address = address;
	f->part_left = length;
	f->seq = 0;
	f->blocks = (length + ESPFLASH_BLOCK - 1) / ESPFLASH_BLOCK;
	f->fill = 0;
	f->block_ready = false;
	uint8_t data[16];
	put32(data, espflash_erase_size(address, length));
	put32(data + 4, f->blocks);
	put32(data + 8, ESPFLASH_BLOCK);
	put32(data + 12, address);
	uint32_t erase_ms = (uint32_t)((uint64_t)length * ESPFLASH_ERASE_MS_PER_MB / 0x100000);
	to_step(f, ST_BEGIN, now_ms);
	command(f, CMD_FLASH_BEGIN, data, sizeof data, NULL, 0,
		erase_ms > ESPFLASH_ERASE_MIN_MS ? erase_ms : ESPFLASH_ERASE_MIN_MS, now_ms);
}

size_t espflash_room(const espflash_t *f) {
	if (f->state == ESPFLASH_READY) {
		return 1; // the next part's header a byte at a time: its data must not come before espflash_part
	}
	if (f->state != ESPFLASH_WRITING || f->block_ready) {
		return 0;
	}
	uint32_t room = ESPFLASH_BLOCK - f->fill;
	return room < f->part_left ? room : f->part_left;
}

void espflash_data(espflash_t *f, const uint8_t *data, size_t len, uint32_t now_ms) {
	(void)now_ms;
	if (len > espflash_room(f) || f->state != ESPFLASH_WRITING) {
		fail(f, "more data than room");
		return;
	}
	memcpy(f->block + f->fill, data, len);
	f->fill += (uint32_t)len;
	f->part_left -= (uint32_t)len;
	if (f->fill == ESPFLASH_BLOCK || f->part_left == 0) {
		memset(f->block + f->fill, 0xff, ESPFLASH_BLOCK - f->fill); // the last block padded
		f->block_ready = true;
	}
}

static void send_block(espflash_t *f, uint32_t now) {
	uint8_t data[16];
	put32(data, ESPFLASH_BLOCK);
	put32(data + 4, f->seq);
	put32(data + 8, 0);
	put32(data + 12, 0);
	to_step(f, ST_WRITE, now);
	command(f, CMD_FLASH_DATA, data, sizeof data, f->block, ESPFLASH_BLOCK, ESPFLASH_DATA_MS, now);
}

void espflash_finish(espflash_t *f, uint32_t now_ms) {
	(void)now_ms;
	if (f->state != ESPFLASH_READY) {
		fail(f, "finished while busy");
		return;
	}
	// BOOT high: the owner's next reset (the modem's start-up) runs the
	// new image.
	f->io.hold_boot(f->io.ctx, false);
	f->state = ESPFLASH_DONE;
	f->step = ST_STOPPED;
}

void espflash_abort(espflash_t *f, const char *problem) {
	if (f->state != ESPFLASH_DONE) {
		fail(f, problem);
	}
}

// A whole reply from the ROM: direction 1, the command, the data's size,
// a value (READ_REG's), the data, whose last two bytes are the status and
// the error code (the ESP8266 ROM's).
static void reply(espflash_t *f, uint32_t now) {
	if (f->rx_over || f->rx_len < 10 || f->rx[0] != 1 || f->rx[1] != f->waiting) {
		return; // noise, another SYNC reply, or not ours
	}
	unsigned size = f->rx[2] | (unsigned)f->rx[3] << 8;
	if (size < 2 || 8u + size > f->rx_len) {
		return;
	}
	uint32_t value = get32(f->rx + 4);
	uint8_t status = f->rx[8 + size - 2];
	f->error = f->rx[8 + size - 1];
	f->waiting = 0;
	if (status) {
		fail(f, f->step == ST_BEGIN ? "the loader refused to erase" :
			f->step == ST_WRITE ? "the loader refused a block (its checksum?)" : "the loader refused a command");
		return;
	}
	f->error = 0;
	switch (f->step) {
	case ST_SYNC:
		to_step(f, ST_SETTLE, now);
		break;
	case ST_DETECT:
		if (value != ESPFLASH_ESP8266_MAGIC) {
			fail(f, "the module is not an ESP8266");
			return;
		}
		f->state = ESPFLASH_READY;
		to_step(f, ST_IDLE, now);
		break;
	case ST_BEGIN:
		to_step(f, ST_DATA, now);
		break;
	case ST_WRITE:
		f->written += f->fill;
		++f->seq;
		f->fill = 0;
		f->block_ready = false;
		if (f->part_left == 0 && f->seq == f->blocks) {
			f->state = ESPFLASH_READY;
			to_step(f, ST_IDLE, now);
		} else {
			to_step(f, ST_DATA, now);
		}
		break;
	default:
		break;
	}
}

void espflash_feed(espflash_t *f, uint8_t c, uint32_t now_ms) {
	if (c == SLIP_END) {
		if (f->rx_in && f->rx_len) {
			reply(f, now_ms);
		}
		// Every END starts a frame (an END ends one too): bytes outside
		// frames, such as the ROM's boot text, are dropped.
		f->rx_in = true;
		f->rx_len = 0;
		f->rx_esc = f->rx_over = false;
		return;
	}
	if (!f->rx_in) {
		return;
	}
	if (f->rx_esc) {
		f->rx_esc = false;
		c = c == SLIP_ESC_END ? SLIP_END : c == SLIP_ESC_ESC ? SLIP_ESC : c;
	} else if (c == SLIP_ESC) {
		f->rx_esc = true;
		return;
	}
	if (f->rx_len < sizeof f->rx) {
		f->rx[f->rx_len++] = c;
	} else {
		f->rx_over = true;
	}
}

void espflash_poll(espflash_t *f, uint32_t now_ms) {
	uint32_t elapsed = now_ms - f->since;
	switch (f->step) {
	case ST_RESET:
		if (f->state == ESPFLASH_CONNECTING && elapsed >= ESPFLASH_RESET_MS) {
			f->io.hold_reset(f->io.ctx, false); // BOOT stays low while the ROM starts
			to_step(f, ST_BOOT, now_ms);
		}
		break;
	case ST_BOOT:
		if (elapsed >= ESPFLASH_BOOT_MS) {
			f->io.hold_boot(f->io.ctx, false);
			f->io.set_baud(f->io.ctx, ESPFLASH_BAUD);
			to_step(f, ST_SYNC, now_ms);
			send_sync(f, now_ms);
		}
		break;
	case ST_SYNC:
		if (f->waiting && elapsed >= f->timeout) {
			if (++f->tries < ESPFLASH_SYNC_TRIES) {
				send_sync(f, now_ms);
			} else if (++f->attempt < ESPFLASH_ATTEMPTS) {
				reset_into_loader(f, now_ms);
			} else {
				fail(f, "no answer from the loader (BOOT and RST wired? the module powered?)");
			}
		}
		break;
	case ST_SETTLE:
		if (elapsed >= ESPFLASH_SYNC_SETTLE_MS) {
			uint8_t data[4];
			put32(data, ESPFLASH_ESP8266_MAGIC_REG);
			to_step(f, ST_DETECT, now_ms);
			command(f, CMD_READ_REG, data, sizeof data, NULL, 0, ESPFLASH_REPLY_MS, now_ms);
		}
		break;
	case ST_DETECT:
	case ST_BEGIN:
	case ST_WRITE:
		if (f->waiting && elapsed >= f->timeout) {
			fail(f, f->step == ST_DETECT ? "no answer to READ_REG" :
				f->step == ST_BEGIN ? "the erase took too long" : "no answer to a block");
		}
		break;
	case ST_DATA:
		if (f->block_ready) {
			send_block(f, now_ms);
		}
		break;
	default:
		break;
	}
}

const char *espflash_step_name(const espflash_t *f) {
	switch (f->state) {
	case ESPFLASH_IDLE: return "idle";
	case ESPFLASH_DONE: return "written";
	case ESPFLASH_FAILED: return "failed";
	case ESPFLASH_READY: return "in the loader";
	default: break;
	}
	switch (f->step) {
	case ST_RESET:
	case ST_BOOT: return "resetting into the loader";
	case ST_SYNC:
	case ST_SETTLE: return "connecting";
	case ST_DETECT: return "checking the chip";
	case ST_BEGIN: return "erasing";
	default: return "writing";
	}
}
