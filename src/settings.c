#include "settings.h"

#include <string.h>

#include "screen.h" // RGB222

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define MAGIC "DVTS"
#define VERSION 1
#define HEADER_SIZE 16
#define CRC_OFFSET 12
#define PAYLOAD_MAX (SETTINGS_RECORD_SIZE - HEADER_SIZE)

#define FIELD_COUNT SETTINGS_FIELDS
_Static_assert(sizeof(settings_t) == SETTINGS_FIELDS, "one byte per setting");

static const unsigned baud_rates[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
#define BAUD_DEFAULT 8 // 115200

static const struct {
	uint8_t data_bits;
	char parity;
	uint8_t stop_bits;
} framings[] = {
	{8, 'N', 1}, {8, 'E', 1}, {8, 'O', 1}, {8, 'N', 2},
	{7, 'E', 1}, {7, 'O', 1}, {7, 'N', 1}, {7, 'N', 2},
};

// Normal text colours (bold is each lit channel one level brighter), and
// the status line's.
static const struct {
	uint8_t fg, bg, status_fg, status_bg;
} colour_sets[] = {
	// white on black; white on blue status line
	{RGB222(2, 2, 2), RGB222(0, 0, 0), RGB222(3, 3, 3), RGB222(0, 0, 2)},
	// green phosphor; reversed status line
	{RGB222(0, 2, 0), RGB222(0, 0, 0), RGB222(0, 0, 0), RGB222(0, 2, 0)},
	// amber phosphor; reversed status line
	{RGB222(2, 1, 0), RGB222(0, 0, 0), RGB222(0, 0, 0), RGB222(2, 1, 0)},
	// blue phosphor; reversed status line
	{RGB222(0, 1, 3), RGB222(0, 0, 0), RGB222(0, 0, 0), RGB222(0, 1, 3)},
};

const settings_field_t settings_fields[SETTINGS_FIELDS] = {
	{"Columns", 2, {80, 132}, {"80", "132"}},
	{"Backspace sends", 2, {0x7f, 0x08}, {"DEL", "BS"}},
	{"Local echo", 2, {0, 1}, {"off", "on"}},
	{"Baud rate", 9, {0, 1, 2, 3, 4, 5, 6, 7, 8},
		{"300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"}},
	{"Data/parity/stop", 8, {0, 1, 2, 3, 4, 5, 6, 7},
		{"8N1", "8E1", "8O1", "8N2", "7E1", "7O1", "7N1", "7N2"}},
	{"New line (LNM)", 2, {0, 1}, {"off", "on"}},
	{"Colors", 4, {0, 1, 2, 3}, {"white", "green", "amber", "blue"}},
	{"Palette", 2, {0, 1}, {"color", "mono"}},
};

unsigned settings_baud_rate(const settings_t *s) {
	return baud_rates[s->baud < COUNT(baud_rates) ? s->baud : BAUD_DEFAULT];
}

void settings_framing(const settings_t *s, unsigned *data_bits, char *parity, unsigned *stop_bits) {
	unsigned i = s->framing < COUNT(framings) ? s->framing : 0;
	*data_bits = framings[i].data_bits;
	*parity = framings[i].parity;
	*stop_bits = framings[i].stop_bits;
}

static uint8_t brighter(uint8_t c) {
	uint8_t out = 0;
	for (unsigned shift = 0; shift < 6; shift += 2) {
		unsigned level = c >> shift & 3;
		out |= (uint8_t)((level && level < 3 ? level + 1 : level) << shift);
	}
	return out;
}

settings_colours_t settings_colours(const settings_t *s) {
	unsigned i = s->colours < COUNT(colour_sets) ? s->colours : 0;
	settings_colours_t c = {
		.fg = colour_sets[i].fg,
		.bold_fg = brighter(colour_sets[i].fg),
		.bg = colour_sets[i].bg,
		.status_fg = colour_sets[i].status_fg,
		.status_bg = colour_sets[i].status_bg,
	};
	if (s->palette) {
		// Mono: nothing outside the text colours, so the status line is
		// them reversed (the phosphor sets already are; white's was blue).
		c.status_fg = c.bg;
		c.status_bg = c.fg;
	}
	return c;
}

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
	*s = (settings_t){.columns = 80, .backspace = 0x7f, .local_echo = 0, .baud = BAUD_DEFAULT,
		.framing = 0, .newline = 0, .colours = 0, .palette = 0};
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
		for (unsigned v = 0; v < settings_fields[i].n_choices; ++v)
			if (payload[i] == settings_fields[i].values[v])
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
