#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "crc32.h"
#include "palette.h"
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

// The text color of each hue, for the logo's tint.
static const uint8_t logo_fgs[PALETTE_HUES] = {
	RGB222(2, 2, 2), RGB222(0, 2, 0), RGB222(2, 1, 0), RGB222(0, 1, 3),
};

const settings_field_t settings_fields[SETTINGS_FIELDS] = {
	{"Columns", 2, {80, 132}, {"80", "132"}, SETTINGS_PAGE_MAIN},
	{"Backspace sends", 2, {0x7f, 0x08}, {"DEL", "BS"}, SETTINGS_PAGE_MAIN},
	{"Local echo", 2, {0, 1}, {"off", "on"}, SETTINGS_PAGE_MAIN},
	{"Baud rate", 9, {0, 1, 2, 3, 4, 5, 6, 7, 8},
		{"300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"}, SETTINGS_PAGE_MAIN},
	{"Data/parity/stop", 8, {0, 1, 2, 3, 4, 5, 6, 7},
		{"8N1", "8E1", "8O1", "8N2", "7E1", "7O1", "7N1", "7N2"}, SETTINGS_PAGE_MAIN},
	{"New line (LNM)", 2, {0, 1}, {"off", "on"}, SETTINGS_PAGE_MAIN},
	{"Colors", 4, {0, 1, 2, 3}, {"white", "green", "amber", "blue"}, SETTINGS_PAGE_MAIN},
	{"Palette", 2, {0, 1}, {"color", "mono"}, SETTINGS_PAGE_MAIN},
	{"Mode", 2, {SETTINGS_MODE_TERMINAL, SETTINGS_MODE_CPM}, {"terminal", "CP/M machine"}, SETTINGS_PAGE_MAIN},
	{"Status line", 4, {1, 0, SETTINGS_STATUS_DEBUG, SETTINGS_STATUS_DEBUG_USB},
		{"shown", "hidden", "debug", "debug usb"}, SETTINGS_PAGE_MAIN},
	{"Blank after", 6, {0, 1, 5, 10, 30, 60}, {"never", "1 min", "5 min", "10 min", "30 min", "60 min"},
		SETTINGS_PAGE_MAIN},
	{"Terminal type", 3, {SETTINGS_TERM_VT100, SETTINGS_TERM_VT52, SETTINGS_TERM_ADM3A},
		{"VT100", "VT52", "ADM-3A"}, SETTINGS_PAGE_MAIN},
	{"Status line clock", 3, {SETTINGS_CLOCK_24H, SETTINGS_CLOCK_12H, SETTINGS_CLOCK_OFF},
		{"24 h", "12 h", "off"}, SETTINGS_PAGE_CLOCK},
	{"Seconds", 2, {0, 1}, {"off", "on"}, SETTINGS_PAGE_CLOCK},
	{"Date", 3, {SETTINGS_DATE_NONE, SETTINGS_DATE_ISO, SETTINGS_DATE_SHORT},
		{"off", "2026-10-22", "Thu 22 Oct"}, SETTINGS_PAGE_CLOCK},
	{"Bell", 4, {SETTINGS_BELL_OFF, SETTINGS_BELL_LOW, SETTINGS_BELL_MEDIUM, SETTINGS_BELL_HIGH},
		{"off", "low", "medium", "high"}, SETTINGS_PAGE_MAIN},
};

unsigned settings_main_field(unsigned row) {
	unsigned f = 0;
	for (; f < SETTINGS_FIELDS; ++f) {
		if (settings_fields[f].page == SETTINGS_PAGE_MAIN && row-- == 0) {
			break;
		}
	}
	return f;
}

unsigned settings_baud_rate(const settings_t *s) {
	return baud_rates[s->baud < COUNT(baud_rates) ? s->baud : BAUD_DEFAULT];
}

unsigned settings_columns(const settings_t *s) {
	return s->term_type == SETTINGS_TERM_ADM3A ? 80 : s->columns;
}

const char *settings_field_fixed(const settings_t *s, unsigned i) {
	if (i == offsetof(settings_t, columns) && s->term_type == SETTINGS_TERM_ADM3A) {
		return "ADM-3A";
	}
	return NULL;
}

void settings_framing(const settings_t *s, unsigned *data_bits, char *parity, unsigned *stop_bits) {
	unsigned i = s->framing < COUNT(framings) ? s->framing : 0;
	*data_bits = framings[i].data_bits;
	*parity = framings[i].parity;
	*stop_bits = framings[i].stop_bits;
}

settings_colors_t settings_colors(const settings_t *s) {
	unsigned hue = s->colors < PALETTE_HUES ? s->colors : PALETTE_HUE_WHITE;
	bool blue_bar = hue == PALETTE_HUE_WHITE && !s->palette;
	return (settings_colors_t){
		.status_fg = blue_bar ? PALETTE_BOLD_TEXT : PALETTE_BACKGROUND,
		.status_bg = blue_bar ? PALETTE_BLUE : PALETTE_TEXT,
		.logo_fg = logo_fgs[hue],
	};
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
		.framing = 0, .newline = 0, .colors = 0, .palette = 0, .mode = SETTINGS_MODE_TERMINAL,
		.status_line = 1, .blank_after = 0, .term_type = SETTINGS_TERM_VT100, .clock = SETTINGS_CLOCK_24H,
		.clock_seconds = 0, .clock_date = SETTINGS_DATE_NONE, .bell = SETTINGS_BELL_MEDIUM};
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
		get32(rec + CRC_OFFSET) != record_crc(rec, len)) {
		return false;
	}
	const uint8_t *payload = rec + HEADER_SIZE;
	settings_defaults(s);
	uint8_t *fields = (uint8_t *)s;
	for (unsigned i = 0; i < FIELD_COUNT && i < len; ++i) {
		for (unsigned v = 0; v < settings_fields[i].n_choices; ++v) {
			if (payload[i] == settings_fields[i].values[v]) {
				fields[i] = payload[i];
			}
		}
	}
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
