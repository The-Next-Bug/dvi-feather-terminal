#include <string.h>

#include "screen.h"
#include "settings.h"
#include "test.h"

// CRC-32 (IEEE) of header bytes 0-11 and len payload bytes, as settings.c
// computes it; written out again here so the test checks the format.
static uint32_t crc_of(const uint8_t *rec, unsigned len) {
	uint32_t crc = 0xffffffff;
	for (unsigned i = 0; i < 12 + len; ++i) {
		crc ^= rec[i < 12 ? i : i + 4];
		for (int b = 0; b < 8; ++b)
			crc = crc >> 1 ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

static uint8_t slot_a[SETTINGS_RECORD_SIZE], slot_b[SETTINGS_RECORD_SIZE];
static const uint8_t *const slots[SETTINGS_SLOTS] = {slot_a, slot_b};

static void erase_all(void) {
	memset(slot_a, 0xff, sizeof slot_a);
	memset(slot_b, 0xff, sizeof slot_b);
}

static void test_round_trip(void) {
	settings_t in = {.columns = 132, .backspace = 0x08, .local_echo = 1, .baud = 4, .framing = 4,
		.newline = 1, .colours = 2, .palette = 1}, out;
	uint8_t rec[SETTINGS_RECORD_SIZE];
	uint32_t seq = 0;
	settings_encode(&in, 0x12345678, rec);
	CHECK(settings_decode(rec, &out, &seq));
	CHECK_EQ(out.columns, 132);
	CHECK_EQ(out.backspace, 0x08);
	CHECK_EQ(out.local_echo, 1);
	CHECK_EQ(out.baud, 4);
	CHECK_EQ(out.framing, 4);
	CHECK_EQ(out.newline, 1);
	CHECK_EQ(out.colours, 2);
	CHECK_EQ(out.palette, 1);
	CHECK_EQ(seq, 0x12345678);
	CHECK_EQ(rec[SETTINGS_RECORD_SIZE - 1], 0xff); // unused bytes stay erased
}

static void test_erased_flash_gives_defaults(void) {
	erase_all();
	settings_t s = {.columns = 0};
	settings_target_t next;
	CHECK(!settings_load(slots, &s, &next));
	CHECK_EQ(s.columns, 80);
	CHECK_EQ(next.slot, 0);
	CHECK_EQ(next.seq, 1);
}

static void test_corruption_is_rejected(void) {
	settings_t in = {.columns = 132}, out;
	uint8_t rec[SETTINGS_RECORD_SIZE];
	uint32_t seq;
	settings_encode(&in, 5, rec);
	for (unsigned i = 0; i < 17; ++i) { // header and the payload byte
		uint8_t bad[SETTINGS_RECORD_SIZE];
		memcpy(bad, rec, sizeof bad);
		bad[i] ^= 0x01;
		CHECK(!settings_decode(bad, &out, &seq));
	}
	// A torn program: the page only partly written.
	uint8_t torn[SETTINGS_RECORD_SIZE];
	memset(torn, 0xff, sizeof torn);
	memcpy(torn, rec, 12);
	CHECK(!settings_decode(torn, &out, &seq));
}

static void test_newest_wins_and_next_alternates(void) {
	erase_all();
	settings_t s = {.columns = 80};
	settings_encode(&s, 7, slot_a);
	s.columns = 132;
	settings_encode(&s, 8, slot_b);
	settings_target_t next;
	CHECK(settings_load(slots, &s, &next));
	CHECK_EQ(s.columns, 132);
	CHECK_EQ(next.slot, 0); // the older record's sector
	CHECK_EQ(next.seq, 9);

	// A save interrupted in slot 0 (erased, nothing written): slot 1 stands.
	memset(slot_a, 0xff, sizeof slot_a);
	CHECK(settings_load(slots, &s, &next));
	CHECK_EQ(s.columns, 132);
	CHECK_EQ(next.slot, 0);
}

static void test_sequence_wraps(void) {
	erase_all();
	settings_t s = {.columns = 80};
	settings_encode(&s, 0xffffffff, slot_a);
	s.columns = 132;
	settings_encode(&s, 0, slot_b); // one after 0xffffffff
	settings_target_t next;
	CHECK(settings_load(slots, &s, &next));
	CHECK_EQ(s.columns, 132);
	CHECK_EQ(next.slot, 0);
	CHECK_EQ(next.seq, 1);
}

static void test_bad_value_keeps_default(void) {
	settings_t s = {.columns = 100, .backspace = 'x', .local_echo = 2}, out;
	uint8_t rec[SETTINGS_RECORD_SIZE];
	uint32_t seq;
	settings_encode(&s, 1, rec);
	CHECK(settings_decode(rec, &out, &seq));
	CHECK_EQ(out.columns, 80);
	CHECK_EQ(out.backspace, 0x7f);
	CHECK_EQ(out.local_echo, 0);
}

// A record from the first build (columns only): the later settings keep
// their defaults.
static void test_older_record(void) {
	static const uint8_t v1[17] = {'D', 'V', 'T', 'S', 1, 1, 0, 0, 3, 0, 0, 0,
		0x00, 0x00, 0x00, 0x00, 132};
	uint8_t rec[SETTINGS_RECORD_SIZE];
	memset(rec, 0xff, sizeof rec);
	memcpy(rec, v1, sizeof v1);
	settings_t probe;
	uint32_t seq;
	CHECK(!settings_decode(rec, &probe, &seq)); // CRC still zero
	uint32_t crc = crc_of(rec, 1);
	rec[12] = (uint8_t)crc;
	rec[13] = (uint8_t)(crc >> 8);
	rec[14] = (uint8_t)(crc >> 16);
	rec[15] = (uint8_t)(crc >> 24);
	CHECK(settings_decode(rec, &probe, &seq));
	CHECK_EQ(probe.columns, 132);
	CHECK_EQ(probe.backspace, 0x7f);
	CHECK_EQ(probe.local_echo, 0);
	CHECK_EQ(settings_baud_rate(&probe), 115200);
	CHECK_EQ(seq, 3);
}

static void test_line_and_colours(void) {
	settings_t s;
	settings_defaults(&s);
	unsigned bits, stop;
	char parity;
	CHECK_EQ(settings_baud_rate(&s), 115200);
	settings_framing(&s, &bits, &parity, &stop);
	CHECK(bits == 8 && parity == 'N' && stop == 1);
	s.baud = 4;
	s.framing = 4;
	CHECK_EQ(settings_baud_rate(&s), 9600);
	settings_framing(&s, &bits, &parity, &stop);
	CHECK(bits == 7 && parity == 'E' && stop == 1);
	settings_colours_t c = settings_colours(&s);
	CHECK_EQ(c.fg, RGB222(2, 2, 2)); // white, as before settings had colours
	CHECK_EQ(c.bold_fg, RGB222(3, 3, 3));
	CHECK_EQ(c.bg, 0);
	CHECK_EQ(c.status_fg, RGB222(3, 3, 3)); // the status line as before too
	CHECK_EQ(c.status_bg, RGB222(0, 0, 2));
	s.colours = 2; // amber
	c = settings_colours(&s);
	CHECK_EQ(c.fg, RGB222(2, 1, 0));
	CHECK_EQ(c.bold_fg, RGB222(3, 2, 0));
	CHECK_EQ(c.status_bg, RGB222(2, 1, 0)); // the status line follows
	// Each set's status line differs from its text background.
	for (s.colours = 0; s.colours < 4; ++s.colours) {
		c = settings_colours(&s);
		CHECK(c.status_bg != c.bg);
		CHECK(c.status_fg != c.status_bg);
	}
	// Mono: the status line is the text colours reversed (white's too).
	s.palette = 1;
	for (s.colours = 0; s.colours < 4; ++s.colours) {
		c = settings_colours(&s);
		CHECK(c.status_fg == c.bg && c.status_bg == c.fg);
	}
	// Every field's labels and values line up and the defaults are choices.
	settings_defaults(&s);
	for (unsigned i = 0; i < SETTINGS_FIELDS; ++i) {
		bool found = false;
		for (unsigned c = 0; c < settings_fields[i].n_choices; ++c) {
			CHECK(settings_fields[i].labels[c] != NULL);
			found |= settings_fields[i].values[c] == *settings_field(&s, i);
		}
		CHECK(found);
	}
}

int main(void) {
	RUN(test_round_trip);
	RUN(test_erased_flash_gives_defaults);
	RUN(test_corruption_is_rejected);
	RUN(test_newest_wins_and_next_alternates);
	RUN(test_sequence_wraps);
	RUN(test_bad_value_keeps_default);
	RUN(test_older_record);
	RUN(test_line_and_colours);
	return tests_done();
}
