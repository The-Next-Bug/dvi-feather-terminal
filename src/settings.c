#include "settings.h"

#include <string.h>

#define MAGIC "DVTS"
#define VERSION 1
#define HEADER_SIZE 16
#define CRC_OFFSET 12
#define PAYLOAD_MAX (SETTINGS_RECORD_SIZE - HEADER_SIZE)

// The payload is settings_t's bytes, in order; each field's valid values
// (anything else keeps the default).
#define FIELD_COUNT sizeof(settings_t)
static const struct {
	uint8_t n;
	uint8_t values[2];
} valid[FIELD_COUNT] = {
	{2, {80, 132}}, // columns
	{2, {0x7f, 0x08}}, // backspace
	{2, {0, 1}}, // local_echo
};

// CRC-32 (IEEE, reflected), bitwise: a table would cost 1 KB of RAM for a
// few hundred bytes checked at boot and on save.
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
	crc = ~crc;
	while (len--) {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i)
			crc = crc >> 1 ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

static uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t record_crc(const uint8_t *rec, unsigned len) {
	return crc32_update(crc32_update(0, rec, CRC_OFFSET), rec + HEADER_SIZE, len);
}

void settings_defaults(settings_t *s) {
	*s = (settings_t){.columns = 80, .backspace = 0x7f, .local_echo = 0};
}

void settings_encode(const settings_t *s, uint32_t seq, uint8_t out[SETTINGS_RECORD_SIZE]) {
	memset(out, 0xff, SETTINGS_RECORD_SIZE);
	memcpy(out, MAGIC, 4);
	out[4] = VERSION;
	out[5] = FIELD_COUNT;
	out[6] = out[7] = 0;
	put32(out + 8, seq);
	memcpy(out + HEADER_SIZE, s, FIELD_COUNT);
	put32(out + CRC_OFFSET, record_crc(out, FIELD_COUNT));
}

bool settings_decode(const uint8_t rec[SETTINGS_RECORD_SIZE], settings_t *s, uint32_t *seq) {
	unsigned len = rec[5];
	if (memcmp(rec, MAGIC, 4) != 0 || rec[4] != VERSION || len > PAYLOAD_MAX ||
		get32(rec + CRC_OFFSET) != record_crc(rec, len))
		return false;
	const uint8_t *payload = rec + HEADER_SIZE;
	settings_defaults(s);
	uint8_t *fields = (uint8_t *)s;
	for (unsigned i = 0; i < FIELD_COUNT && i < len; ++i)
		for (unsigned v = 0; v < valid[i].n; ++v)
			if (payload[i] == valid[i].values[v])
				fields[i] = payload[i];
	*seq = get32(rec + 8);
	return true;
}

bool settings_load(const uint8_t *const slots[SETTINGS_SLOTS], settings_t *s, settings_target_t *next) {
	bool found = false;
	unsigned newest = 0;
	uint32_t newest_seq = 0;
	settings_defaults(s);
	for (unsigned i = 0; i < SETTINGS_SLOTS; ++i) {
		settings_t candidate;
		uint32_t seq;
		// Sequence numbers are compared modulo 2^32, so they may wrap.
		if (settings_decode(slots[i], &candidate, &seq) && (!found || (int32_t)(seq - newest_seq) > 0)) {
			found = true;
			newest = i;
			newest_seq = seq;
			*s = candidate;
		}
	}
	next->slot = found ? (newest + 1) % SETTINGS_SLOTS : 0;
	next->seq = found ? newest_seq + 1 : 1;
	return found;
}
