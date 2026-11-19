#include "zmi.h"

#include <string.h>

#include "crc32.h"

enum {
	ST_HEADER,       // the fixed header into buf
	ST_HEADER_EXTRA, // a later version's extra header bytes, skipped
	ST_PART_HEADER,  // a part's address and length into buf
	ST_PART_DATA,
	ST_CRC,          // the CRC into buf
	ST_DONE,
};

#define PART_HEADER 8
#define ESP_IMAGE_MAGIC 0xE9

static uint32_t get16(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void zmi_init(zmi_reader_t *r, zmi_data_t data, void *ctx) {
	memset(r, 0, sizeof *r);
	r->data = data;
	r->ctx = ctx;
	r->state = ST_HEADER;
}

bool zmi_header_done(const zmi_reader_t *r) {
	return r->state > ST_HEADER_EXTRA;
}

// A NUL-padded text field: printable ASCII, then NULs only, one at least.
static bool text_field(const uint8_t *p, size_t size, char *out) {
	size_t n = 0;
	while (n < size && p[n]) {
		if (p[n] < 0x20 || p[n] > 0x7e) {
			return false;
		}
		++n;
	}
	if (n == 0 || n == size) {
		return false;
	}
	for (size_t i = n; i < size; ++i) {
		if (p[i]) {
			return false;
		}
	}
	memcpy(out, p, n);
	out[n] = '\0';
	return true;
}

static bool power_of_two(uint32_t v) {
	return v && !(v & (v - 1));
}

static zmi_result_t parse_header(zmi_reader_t *r) {
	const uint8_t *h = r->buf;
	zmi_info_t *info = &r->info;
	if (memcmp(h, "ZMI1", 4) != 0) {
		return ZMI_BAD_MAGIC;
	}
	r->header_size = get16(h + 4);
	info->chip = h[6];
	info->flash_size = get32(h + 48);
	info->parts = get32(h + 52);
	info->parts_size = get32(h + 56);
	if (r->header_size < ZMI_HEADER_SIZE || r->header_size > ZMI_HEADER_MAX || info->chip != ZMI_CHIP_ESP8266 ||
		!text_field(h + 8, ZMI_MODULE_MAX + 1, info->module) ||
		!text_field(h + 24, ZMI_VERSION_MAX + 1, info->version) ||
		!power_of_two(info->flash_size) || info->flash_size < 0x40000 || info->flash_size > 0x1000000 ||
		info->parts < 1 || info->parts > ZMI_MAX_PARTS || info->parts_size < info->parts * (PART_HEADER + 1u)) {
		return ZMI_BAD_HEADER;
	}
	return ZMI_OK;
}

static zmi_result_t parse_part_header(zmi_reader_t *r) {
	uint32_t address = get32(r->buf), length = get32(r->buf + 4);
	if (length == 0 || address % ZMI_SECTOR || address < r->next_free || address >= r->info.flash_size ||
		length > r->info.flash_size - address || length > r->info.parts_size - r->parts_used) {
		return ZMI_BAD_PART;
	}
	r->address = r->part_start = address;
	r->left = length;
	r->next_free = address + length;
	r->has_image = r->has_image || address == 0;
	return ZMI_OK;
}

// The ESP8266's image header: its flash size code (byte 3, high nibble).
static uint32_t esp8266_flash_size(uint8_t code) {
	switch (code >> 4) {
	case 0: return 0x80000;
	case 1: return 0x40000;
	case 2: return 0x100000;
	case 3: case 5: return 0x200000;
	case 4: case 6: return 0x400000;
	case 8: return 0x800000;
	case 9: return 0x1000000;
	default: return 0;
	}
}

static zmi_result_t check_image(const zmi_reader_t *r) {
	if (r->esp_have < sizeof r->esp || r->esp[0] != ESP_IMAGE_MAGIC || esp8266_flash_size(r->esp[3]) != r->info.flash_size) {
		return ZMI_BAD_IMAGE;
	}
	return ZMI_OK;
}

// One state's worth of bytes from data: how many were taken.
static size_t step(zmi_reader_t *r, const uint8_t *data, size_t len) {
	size_t take;
	switch (r->state) {
	case ST_HEADER:
	case ST_PART_HEADER:
	case ST_CRC: {
		uint32_t want = r->state == ST_HEADER ? ZMI_HEADER_SIZE : r->state == ST_PART_HEADER ? PART_HEADER : 4;
		take = want - r->have < len ? want - r->have : len;
		memcpy(r->buf + r->have, data, take);
		r->have += (uint32_t)take;
		if (r->state != ST_CRC) {
			r->crc = crc32_update(r->crc, data, take);
		}
		if (r->state == ST_PART_HEADER) {
			r->parts_used += (uint32_t)take;
		}
		if (r->have < want) {
			return take;
		}
		r->have = 0;
		if (r->state == ST_HEADER) {
			r->result = parse_header(r);
			r->state = r->header_size > ZMI_HEADER_SIZE ? ST_HEADER_EXTRA : ST_PART_HEADER;
		} else if (r->state == ST_PART_HEADER) {
			r->result = parse_part_header(r);
			r->state = ST_PART_DATA;
		} else {
			r->result = get32(r->buf) == r->crc ? ZMI_OK : ZMI_BAD_CRC;
			r->state = ST_DONE;
		}
		return take;
	}
	case ST_HEADER_EXTRA: {
		uint32_t left = r->header_size - ZMI_HEADER_SIZE - r->have;
		take = left < len ? left : len;
		r->crc = crc32_update(r->crc, data, take);
		r->have += (uint32_t)take;
		if (r->have == r->header_size - ZMI_HEADER_SIZE) {
			r->have = 0;
			r->state = ST_PART_HEADER;
		}
		return take;
	}
	case ST_PART_DATA: {
		take = r->left < len ? r->left : len;
		r->crc = crc32_update(r->crc, data, take);
		size_t from = 0; // data[from..take) goes on to the callback
		if (r->part_start == 0 && r->esp_have < sizeof r->esp) {
			// The start of the part at address 0, the ESP image's header:
			// held back until it is checked, so nothing of a refused image
			// is passed on.
			while (from < take && r->esp_have < sizeof r->esp) {
				r->esp[r->esp_have++] = data[from++];
			}
			if (r->esp_have < sizeof r->esp && take < r->left) {
				from = take; // more to come
			} else {
				r->result = check_image(r);
				if (r->result == ZMI_OK && r->data) {
					r->data(r->ctx, 0, r->esp, sizeof r->esp);
				}
			}
		}
		if (r->result == ZMI_OK && r->data && take > from) {
			r->data(r->ctx, r->address + (uint32_t)from, data + from, take - from);
		}
		r->address += (uint32_t)take;
		r->left -= (uint32_t)take;
		r->parts_used += (uint32_t)take;
		r->info.image_bytes += (uint32_t)take;
		if (r->left == 0) {
			++r->parts_seen;
			if (r->parts_seen < r->info.parts) {
				r->state = ST_PART_HEADER;
			} else if (r->parts_used != r->info.parts_size) {
				r->result = ZMI_BAD_PART;
			} else if (!r->has_image) {
				r->result = ZMI_BAD_IMAGE;
			} else {
				r->state = ST_CRC;
			}
		}
		return take;
	}
	default:
		r->result = ZMI_TOO_LONG;
		return len;
	}
}

zmi_result_t zmi_feed(zmi_reader_t *r, const uint8_t *data, size_t len) {
	while (len && r->result == ZMI_OK) {
		size_t n = step(r, data, len);
		data += n;
		len -= n;
	}
	return r->result;
}

zmi_result_t zmi_finish(zmi_reader_t *r) {
	if (r->result == ZMI_OK && r->state != ST_DONE) {
		r->result = ZMI_TRUNCATED;
	}
	return r->result;
}

zmi_result_t zmi_check_target(const zmi_info_t *info, uint8_t chip, const char *module, uint32_t flash_size) {
	if (info->chip != chip) {
		return ZMI_WRONG_CHIP;
	}
	if (strcmp(info->module, module) != 0) {
		return ZMI_WRONG_MODULE;
	}
	return info->flash_size > flash_size ? ZMI_TOO_BIG : ZMI_OK;
}

const char *zmi_result_text(zmi_result_t result) {
	switch (result) {
	case ZMI_OK: return "fine";
	case ZMI_TRUNCATED: return "cut short";
	case ZMI_BAD_MAGIC: return "not a modem image (ZMI)";
	case ZMI_BAD_HEADER: return "a header it cannot use";
	case ZMI_BAD_PART: return "its parts do not fit together";
	case ZMI_BAD_IMAGE: return "no ESP image, or for another flash size";
	case ZMI_BAD_CRC: return "damaged (CRC)";
	case ZMI_TOO_LONG: return "more after its end";
	case ZMI_WRONG_CHIP: return "for another chip";
	case ZMI_WRONG_MODULE: return "for another module";
	case ZMI_TOO_BIG: return "for a bigger flash";
	}
	return "?";
}
