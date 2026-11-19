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
#define PAYLOAD_MAX (SETTINGS_PAGE0_SIZE - HEADER_SIZE)

#define FIELD_COUNT SETTINGS_FIELDS
_Static_assert(offsetof(settings_t, printer_target) == SETTINGS_FIELDS, "one byte per setting, then the texts");
_Static_assert(HEADER_SIZE + SETTINGS_FIELDS <= SETTINGS_TEXT2_OFFSET, "the payload stops short of the text blocks");

#define TEXT_MAGIC 'T'
#define TEXT2_MAGIC 'U'
#if WIFI_MODEM
#define NET_MAGIC 'N'
// What the network block holds now (it may grow: SETTINGS_NET_TEXT_MAX).
#define NET_LEN_MAX (SETTINGS_SSID_MAX + 1 + SETTINGS_PASSWORD_MAX)
_Static_assert(NET_LEN_MAX <= SETTINGS_NET_TEXT_MAX && SETTINGS_NET_TEXT_MAX <= 255, "the network block fits page 1");
#endif

// Appended only: the record keeps the index. 230400 and up are for a host
// with RTS/CTS (the Frankless's Zimodem, ATB): without flow control the
// terminal falls behind a full-screen burst at these rates.
static const unsigned baud_rates[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800,
	921600};
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
	{"Baud rate", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
		{"300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"},
		SETTINGS_PAGE_MAIN},
	{"Data/parity/stop", 8, {0, 1, 2, 3, 4, 5, 6, 7},
		{"8N1", "8E1", "8O1", "8N2", "7E1", "7O1", "7N1", "7N2"}, SETTINGS_PAGE_MAIN},
	{"New line (LNM)", 2, {0, 1}, {"off", "on"}, SETTINGS_PAGE_MAIN},
	{"Colors", 4, {0, 1, 2, 3}, {"white", "green", "amber", "blue"}, SETTINGS_PAGE_MAIN},
	{"Palette", 2, {0, 1}, {"color", "mono"}, SETTINGS_PAGE_MAIN},
	{"Mode", 2, {SETTINGS_MODE_TERMINAL, SETTINGS_MODE_CPM}, {"terminal", "CP/M machine"}, SETTINGS_PAGE_MAIN},
	{"Status line", 5, {1, 0, SETTINGS_STATUS_DEBUG, SETTINGS_STATUS_DEBUG_USB, SETTINGS_STATUS_DEBUG_SERIAL},
		{"shown", "hidden", "debug", "debug usb", "debug serial"}, SETTINGS_PAGE_MAIN},
	{"Blank after", 6, {0, 1, 5, 10, 30, 60}, {"never", "1 min", "5 min", "10 min", "30 min", "60 min"},
		SETTINGS_PAGE_MAIN},
	{"Terminal type", 3, {SETTINGS_TERM_VT100, SETTINGS_TERM_VT52, SETTINGS_TERM_ADM3A},
		{"VT100", "VT52", "ADM-3A"}, SETTINGS_PAGE_MAIN},
	{"Status line clock", 3, {SETTINGS_CLOCK_24H, SETTINGS_CLOCK_12H, SETTINGS_CLOCK_OFF},
		{"24 h", "12 h", "off"}, SETTINGS_PAGE_CLOCK},
	{"Seconds", 2, {0, 1}, {"off", "on"}, SETTINGS_PAGE_CLOCK},
	{"Date", 3, {SETTINGS_DATE_NONE, SETTINGS_DATE_ISO, SETTINGS_DATE_SHORT},
		{"off", "2026-10-22", "Thu 22 Oct"}, SETTINGS_PAGE_CLOCK},
	{"Bell", SETTINGS_BELL_MAX + 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
		{"off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, SETTINGS_PAGE_CLOCK},
	{"USB log", 2, {SETTINGS_USB_LOG_OFF, SETTINGS_USB_LOG_SD}, {"off", "SD card USBLOG.TXT"}, SETTINGS_PAGE_MAIN},
	{"Sound chip", SETTINGS_SOUND_MAX + 1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
		{"off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}, SETTINGS_PAGE_CLOCK},
	{"Sound DAC", 2, {1, 0}, {"fitted", "not fitted"}, SETTINGS_PAGE_CLOCK},
	{"Stereo", 3, {SETTINGS_STEREO_MONO, SETTINGS_STEREO_ABC, SETTINGS_STEREO_ACB},
		{"mono", "ABC", "ACB"}, SETTINGS_PAGE_CLOCK},
	{"Flow control", 3, {SETTINGS_FLOW_NONE, SETTINGS_FLOW_RTSCTS, SETTINGS_FLOW_XONXOFF},
		{"none", "RTS/CTS", "XON/XOFF"}, SETTINGS_PAGE_MAIN},
	{"Display", 2, {SETTINGS_DISPLAY_1024, SETTINGS_DISPLAY_800}, {"1024x600", "800x600"}, SETTINGS_PAGE_MAIN},
	{"Display before", 3, {SETTINGS_DISPLAY_NONE, SETTINGS_DISPLAY_1024, SETTINGS_DISPLAY_800},
		{"none", "1024x600", "800x600"}, SETTINGS_PAGE_NONE},
	{"Printer on TTY2", 2, {SETTINGS_PRINTER_OFF, SETTINGS_PRINTER_IPP},
		{"off", "IPP (AT+PRINT)"}, SETTINGS_PAGE_PRINTER},
	{"IPP sends", 2, {1, 0}, {"text (AT+PRINTA)", "raw (AT+PRINTR)"}, SETTINGS_PAGE_PRINTER},
	{"Job ends after", 5, {5, 10, 30, 60, 120}, {"5 s idle", "10 s idle", "30 s idle", "60 s idle", "120 s idle"},
		SETTINGS_PAGE_PRINTER},
	{"Telnet", 3, {SETTINGS_TELNET_OFF, SETTINGS_TELNET_AUTO, SETTINGS_TELNET_ON}, {"off", "auto", "on"},
		SETTINGS_PAGE_TELNET},
	{"Port", 2, {SETTINGS_PORT_SERIAL, SETTINGS_PORT_WIFI}, {"serial", "WiFi modem"},
		WIFI_MODEM ? SETTINGS_PAGE_MAIN : SETTINGS_PAGE_NONE},
	{"Character set", 2, {SETTINGS_CHARSET_UTF8, SETTINGS_CHARSET_LATIN1}, {"UTF-8", "Latin-1"},
		SETTINGS_PAGE_MAIN},
};

// SET-UP's rows: the line's settings together (the Port first, in
// WIFI_MODEM builds), then the rest.
static const uint8_t main_rows[SETTINGS_MAIN_FIELDS] = {
	offsetof(settings_t, columns), offsetof(settings_t, backspace), offsetof(settings_t, local_echo),
#if WIFI_MODEM
	offsetof(settings_t, port),
#endif
	offsetof(settings_t, baud), offsetof(settings_t, framing), offsetof(settings_t, flow),
	offsetof(settings_t, newline), offsetof(settings_t, colors), offsetof(settings_t, palette),
	offsetof(settings_t, display), offsetof(settings_t, mode), offsetof(settings_t, status_line), offsetof(settings_t, blank_after),
	offsetof(settings_t, term_type), offsetof(settings_t, charset), offsetof(settings_t, usb_log),
};

unsigned settings_main_field(unsigned row) {
	return row < SETTINGS_MAIN_FIELDS ? main_rows[row] : SETTINGS_FIELDS;
}

bool settings_wifi(const settings_t *s) {
	return WIFI_MODEM && s->port == SETTINGS_PORT_WIFI;
}

bool settings_utf8(const settings_t *s) {
	return s->charset == SETTINGS_CHARSET_UTF8 && s->term_type == SETTINGS_TERM_VT100 &&
		s->mode == SETTINGS_MODE_TERMINAL;
}

unsigned settings_flow(const settings_t *s) {
	return settings_wifi(s) ? SETTINGS_FLOW_RTSCTS : s->flow;
}

unsigned settings_baud_rate(const settings_t *s) {
	if (settings_wifi(s)) {
		return SETTINGS_MODEM_BAUD;
	}
	return baud_rates[s->baud < COUNT(baud_rates) ? s->baud : BAUD_DEFAULT];
}

unsigned settings_columns(const settings_t *s) {
	return s->term_type == SETTINGS_TERM_ADM3A ? 80 : s->columns;
}

const char *settings_field_fixed(const settings_t *s, unsigned i) {
	if (i == offsetof(settings_t, columns) && s->term_type == SETTINGS_TERM_ADM3A) {
		return "ADM-3A";
	}
	if ((i == offsetof(settings_t, baud) || i == offsetof(settings_t, framing) || i == offsetof(settings_t, flow)) &&
		settings_wifi(s)) {
		return "WiFi modem";
	}
	if (i == offsetof(settings_t, charset) && s->term_type != SETTINGS_TERM_VT100) {
		return s->term_type == SETTINGS_TERM_ADM3A ? "ADM-3A" : "VT52";
	}
	if (i == offsetof(settings_t, charset) && s->mode != SETTINGS_MODE_TERMINAL) {
		return "CP/M";
	}
	return NULL;
}

settings_t settings_in_use(const settings_t *s) {
	settings_t used = *s;
	used.columns = (uint8_t)settings_columns(s);
	used.flow = (uint8_t)settings_flow(s);
	used.charset = settings_utf8(s) ? SETTINGS_CHARSET_UTF8 : SETTINGS_CHARSET_LATIN1;
	if (settings_wifi(s)) {
		used.framing = 0; // 8N1
		for (unsigned i = 0; i < COUNT(baud_rates); ++i) {
			if (baud_rates[i] == SETTINGS_MODEM_BAUD) {
				used.baud = (uint8_t)i;
			}
		}
	}
	return used;
}

void settings_framing(const settings_t *s, unsigned *data_bits, char *parity, unsigned *stop_bits) {
	unsigned i = !settings_wifi(s) && s->framing < COUNT(framings) ? s->framing : 0; // the modem: 8N1
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
	*s = (settings_t){.columns = 80, .backspace = 0x08, .local_echo = 0, .baud = BAUD_DEFAULT,
		.framing = 0, .newline = 0, .colors = 0, .palette = 0, .mode = SETTINGS_MODE_TERMINAL,
		.status_line = 1, .blank_after = 0, .term_type = SETTINGS_TERM_VT100, .clock = SETTINGS_CLOCK_24H,
		.clock_seconds = 0, .clock_date = SETTINGS_DATE_NONE, .bell = SETTINGS_BELL_DEFAULT,
		.usb_log = SETTINGS_USB_LOG_OFF, .sound_level = SETTINGS_SOUND_DEFAULT, .sound_dac = 1,
		.sound_stereo = SETTINGS_STEREO_MONO, .flow = SETTINGS_FLOW_NONE,
		.display = SETTINGS_DISPLAY_DEFAULT, .display_was = SETTINGS_DISPLAY_NONE,
		.printer = SETTINGS_PRINTER_OFF, .printer_text = 1, .printer_idle = SETTINGS_PRINTER_IDLE_DEFAULT,
		.telnet = SETTINGS_TELNET_OFF, .port = SETTINGS_PORT_SERIAL, .charset = SETTINGS_CHARSET_UTF8};
}

// Copy src into dst (size max + 1): printable ASCII only (and no spaces
// unless spaces), cut at max, NUL-padded so settings compare with memcmp.
static void set_text(char *dst, size_t max, const char *src, bool spaces) {
	size_t n = 0;
	memset(dst, 0, max + 1);
	for (; *src && n < max; ++src) {
		if (*src >= (spaces ? 0x20 : 0x21) && *src < 0x7f) {
			dst[n++] = *src;
		}
	}
}

void settings_set_target(settings_t *s, const char *target) {
	set_text(s->printer_target, SETTINGS_TARGET_MAX, target, true);
}

void settings_set_telnet_type(settings_t *s, const char *type) {
	set_text(s->telnet_type, SETTINGS_TYPE_MAX, type, false);
}

void settings_set_telnet_host(settings_t *s, const char *host) {
	set_text(s->telnet_host, SETTINGS_HOST_MAX, host, false);
}

#if WIFI_MODEM
void settings_set_wifi_ssid(settings_t *s, const char *ssid) {
	set_text(s->wifi_ssid, SETTINGS_SSID_MAX, ssid, true);
}

void settings_set_wifi_password(settings_t *s, const char *password) {
	set_text(s->wifi_password, SETTINGS_PASSWORD_MAX, password, true);
}
#endif

void settings_encode(const settings_t *s, uint32_t seq, uint8_t out[SETTINGS_RECORD_SIZE]) {
	memset(out, 0xff, SETTINGS_RECORD_SIZE);
	memcpy(out, MAGIC, 4);
	out[4] = VERSION;
	out[5] = FIELD_COUNT;
	out[6] = out[7] = 0;
	put32(out + 8, seq);
	memcpy(out + HEADER_SIZE, s, FIELD_COUNT);
	put32(out + CRC_OFFSET, record_crc(out, FIELD_COUNT));
	uint8_t *t = out + SETTINGS_TEXT_OFFSET;
	size_t len = 0;
	while (len < SETTINGS_TARGET_MAX && s->printer_target[len]) {
		++len;
	}
	t[0] = TEXT_MAGIC;
	t[1] = (uint8_t)len;
	memcpy(t + 6, s->printer_target, len);
	put32(t + 2, crc32_update(0, t + 6, len));
	uint8_t *u = out + SETTINGS_TEXT2_OFFSET;
	size_t type_len = strnlen(s->telnet_type, SETTINGS_TYPE_MAX);
	size_t host_len = strnlen(s->telnet_host, SETTINGS_HOST_MAX);
	u[0] = TEXT2_MAGIC;
	u[1] = (uint8_t)(type_len + 1 + host_len);
	memcpy(u + 6, s->telnet_type, type_len);
	u[6 + type_len] = '\0';
	memcpy(u + 7 + type_len, s->telnet_host, host_len);
	put32(u + 2, crc32_update(0, u + 6, u[1]));
#if WIFI_MODEM
	uint8_t *n = out + SETTINGS_NET_OFFSET;
	size_t ssid_len = strnlen(s->wifi_ssid, SETTINGS_SSID_MAX);
	size_t pass_len = strnlen(s->wifi_password, SETTINGS_PASSWORD_MAX);
	n[0] = NET_MAGIC;
	n[1] = (uint8_t)(ssid_len + 1 + pass_len);
	memcpy(n + 6, s->wifi_ssid, ssid_len);
	n[6 + ssid_len] = '\0';
	memcpy(n + 7 + ssid_len, s->wifi_password, pass_len);
	put32(n + 2, crc32_update(0, n + 6, n[1]));
#endif
}

// The text block, if the record has a valid one.
static void decode_texts(const uint8_t rec[SETTINGS_RECORD_SIZE], settings_t *s) {
	const uint8_t *t = rec + SETTINGS_TEXT_OFFSET;
	unsigned len = t[1];
	if (t[0] != TEXT_MAGIC || len > SETTINGS_TARGET_MAX || get32(t + 2) != crc32_update(0, t + 6, len)) {
		return;
	}
	char text[SETTINGS_TARGET_MAX + 1];
	memcpy(text, t + 6, len);
	text[len] = '\0';
	settings_set_target(s, text);
}

// The telnet texts' block, if the record has a valid one: "type\0host".
static void decode_texts2(const uint8_t rec[SETTINGS_RECORD_SIZE], settings_t *s) {
	const uint8_t *u = rec + SETTINGS_TEXT2_OFFSET;
	unsigned len = u[1];
	if (u[0] != TEXT2_MAGIC || len < 1 || len > SETTINGS_TYPE_MAX + 1 + SETTINGS_HOST_MAX ||
		get32(u + 2) != crc32_update(0, u + 6, len)) {
		return;
	}
	char text[SETTINGS_TYPE_MAX + 1 + SETTINGS_HOST_MAX + 1];
	memcpy(text, u + 6, len);
	text[len] = '\0';
	size_t type_len = strnlen(text, len);
	if (type_len > SETTINGS_TYPE_MAX || type_len + 1 + SETTINGS_HOST_MAX < len) {
		return; // no NUL where it can be: not ours
	}
	settings_set_telnet_type(s, text);
	settings_set_telnet_host(s, type_len < len ? text + type_len + 1 : "");
}

#if WIFI_MODEM
// The network block, if page 1 has a valid one: NUL-separated texts, the
// SSID and the password, then (later) more; missing ones stay empty.
static void decode_net(const uint8_t rec[SETTINGS_RECORD_SIZE], settings_t *s) {
	const uint8_t *n = rec + SETTINGS_NET_OFFSET;
	unsigned len = n[1];
	if (n[0] != NET_MAGIC || len > SETTINGS_NET_TEXT_MAX || get32(n + 2) != crc32_update(0, n + 6, len)) {
		return;
	}
	char text[SETTINGS_NET_TEXT_MAX + 1];
	memcpy(text, n + 6, len);
	text[len] = '\0';
	size_t ssid_len = strnlen(text, len);
	settings_set_wifi_ssid(s, text);
	settings_set_wifi_password(s, ssid_len < len ? text + ssid_len + 1 : "");
}
#endif

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
	decode_texts(rec, s);
	decode_texts2(rec, s);
#if WIFI_MODEM
	decode_net(rec, s);
#endif
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
