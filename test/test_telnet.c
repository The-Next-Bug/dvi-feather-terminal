#include <string.h>

#include "telnet.h"
#include "test.h"

// What the terminal sent, as one string of bytes.
static uint8_t sent[512];
static size_t sent_len;

static void capture(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	CHECK(sent_len + len <= sizeof sent);
	memcpy(sent + sent_len, data, len);
	sent_len += len;
}

static bool sent_is(const uint8_t *want, size_t len) {
	bool same = sent_len == len && memcmp(sent, want, len) == 0;
	sent_len = 0;
	return same;
}
#define SENT(...) sent_is((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static telnet_t t;
static uint8_t out[600];

static size_t feed(const uint8_t *in, size_t n) {
	return telnet_filter(&t, in, n, out);
}
#define FEED(...) feed((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void start(telnet_mode_t mode) {
	telnet_init(&t, capture, NULL);
	telnet_set_mode(&t, mode);
	sent_len = 0;
}

#define IAC TELNET_IAC

static void test_off_passes_everything(void) {
	start(TELNET_OFF);
	CHECK_EQ(FEED('a', IAC, TELNET_DO, TELNET_OPT_NAWS, 'b'), 5u);
	CHECK(out[1] == IAC && out[2] == TELNET_DO);
	CHECK_EQ(sent_len, 0u);
	CHECK(!telnet_active(&t));
	telnet_send_data(&t, (const uint8_t[]){IAC}, 1);
	CHECK(SENT(IAC)); // not doubled
}

static void test_naws_with_the_size_and_its_changes(void) {
	start(TELNET_ON);
	telnet_set_size(&t, 132, 24);
	CHECK_EQ(sent_len, 0u); // not asked for yet
	CHECK_EQ(FEED('x', IAC, TELNET_DO, TELNET_OPT_NAWS, 'y'), 2u);
	CHECK(out[0] == 'x' && out[1] == 'y');
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 132, 0, 24, IAC, TELNET_SE));
	telnet_set_size(&t, 132, 24);
	CHECK_EQ(sent_len, 0u); // the same size: nothing
	telnet_set_size(&t, 80, 24);
	CHECK(SENT(IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 80, 0, 24, IAC, TELNET_SE));
	FEED(IAC, TELNET_DONT, TELNET_OPT_NAWS);
	CHECK(SENT(IAC, TELNET_WONT, TELNET_OPT_NAWS));
	telnet_set_size(&t, 132, 24);
	CHECK_EQ(sent_len, 0u); // refused: no more
}

static void test_naws_doubles_255(void) {
	start(TELNET_ON);
	telnet_set_size(&t, 255, 24);
	FEED(IAC, TELNET_DO, TELNET_OPT_NAWS);
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 255, 255, 0, 24, IAC,
		TELNET_SE));
}

static void test_ttype(void) {
	start(TELNET_ON);
	telnet_set_type(&t, "VT52");
	FEED(IAC, TELNET_DO, TELNET_OPT_TTYPE);
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_TTYPE));
	FEED(IAC, TELNET_SB, TELNET_OPT_TTYPE, TELNET_TTYPE_SEND, IAC, TELNET_SE);
	CHECK(SENT(IAC, TELNET_SB, TELNET_OPT_TTYPE, TELNET_TTYPE_IS, 'V', 'T', '5', '2', IAC, TELNET_SE));
	telnet_set_type(&t, "x term\x01");
	CHECK(strcmp(t.type, "xterm") == 0); // printable, no spaces
}

static void test_echo_sga_taken_rest_refused(void) {
	start(TELNET_ON);
	FEED(IAC, TELNET_WILL, TELNET_OPT_ECHO, IAC, TELNET_WILL, TELNET_OPT_SGA, IAC, TELNET_DO, TELNET_OPT_SGA);
	CHECK(SENT(IAC, TELNET_DO, TELNET_OPT_ECHO, IAC, TELNET_DO, TELNET_OPT_SGA, IAC, TELNET_WILL, TELNET_OPT_SGA));
	FEED(IAC, TELNET_DO, 32, IAC, TELNET_WILL, 0, IAC, TELNET_DO, 39);
	CHECK(SENT(IAC, TELNET_WONT, 32, IAC, TELNET_DONT, 0, IAC, TELNET_WONT, 39));
	FEED(IAC, TELNET_WONT, TELNET_OPT_ECHO);
	CHECK(SENT(IAC, TELNET_DONT, TELNET_OPT_ECHO));
	FEED(IAC, TELNET_WONT, TELNET_OPT_ECHO); // already off: no answer, no loop
	CHECK_EQ(sent_len, 0u);
	FEED(IAC, TELNET_DONT, 32);
	CHECK_EQ(sent_len, 0u);
}

static void test_data_255_and_other_commands(void) {
	start(TELNET_ON);
	CHECK_EQ(FEED('a', IAC, IAC, 'b', IAC, 241 /* NOP */, 'c', IAC, 249 /* GA */), 4u);
	CHECK(out[0] == 'a' && out[1] == IAC && out[2] == 'b' && out[3] == 'c');
	// A sub-negotiation's bytes never reach the screen, 255 inside included.
	CHECK_EQ(FEED('d', IAC, TELNET_SB, 99, 1, IAC, IAC, 2, IAC, TELNET_SE, 'e'), 2u);
	CHECK(out[0] == 'd' && out[1] == 'e');
	CHECK_EQ(sent_len, 0u);
}

static void test_split_across_reads(void) {
	start(TELNET_ON);
	telnet_set_size(&t, 80, 24);
	const uint8_t cmd[] = {'<', IAC, TELNET_DO, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_TTYPE, TELNET_TTYPE_SEND,
		IAC, TELNET_SE, '>'};
	size_t got = 0;
	char text[4] = "";
	for (size_t i = 0; i < sizeof cmd; ++i) { // a byte at a time
		size_t n = feed(cmd + i, 1);
		for (size_t j = 0; j < n; ++j) {
			text[got++] = (char)out[j];
		}
	}
	text[got] = '\0';
	CHECK(strcmp(text, "<>") == 0);
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 80, 0, 24, IAC, TELNET_SE,
		IAC, TELNET_SB, TELNET_OPT_TTYPE, TELNET_TTYPE_IS, 'V', 'T', '1', '0', '0', IAC, TELNET_SE));
}

static void test_auto_waits_for_a_command(void) {
	start(TELNET_AUTO);
	// A plain host's 0xFF: data, with what follows it.
	CHECK_EQ(FEED('a', IAC, 'b', IAC, IAC, 'c'), 6u);
	CHECK(out[1] == IAC && out[2] == 'b' && out[3] == IAC && out[4] == IAC && out[5] == 'c');
	CHECK(!telnet_active(&t));
	telnet_send_data(&t, (const uint8_t[]){IAC}, 1);
	CHECK(SENT(IAC));
	// An IAC at the end of one read, its command in the next.
	CHECK_EQ(FEED('x', IAC), 1u);
	CHECK_EQ(FEED(TELNET_DO, TELNET_OPT_NAWS, 'y'), 1u);
	CHECK(out[0] == 'y');
	CHECK(telnet_active(&t));
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 80, 0, 24, IAC, TELNET_SE));
	// A held IAC that was data comes out with the next read's first byte:
	// one more byte out than in, as telnet_filter allows.
	telnet_reset(&t);
	CHECK_EQ(FEED(IAC), 0u);
	CHECK_EQ(FEED('z'), 2u);
	CHECK(out[0] == IAC && out[1] == 'z');
}

static void test_reset_and_mode_forget(void) {
	start(TELNET_AUTO);
	FEED(IAC, TELNET_DO, TELNET_OPT_NAWS);
	sent_len = 0;
	telnet_reset(&t); // RIS
	CHECK(!telnet_active(&t));
	telnet_set_size(&t, 132, 24);
	CHECK_EQ(sent_len, 0u);
	telnet_set_mode(&t, TELNET_ON);
	FEED(IAC, TELNET_DO, TELNET_OPT_NAWS);
	sent_len = 0;
	telnet_set_mode(&t, TELNET_OFF);
	telnet_set_size(&t, 80, 24);
	CHECK_EQ(sent_len, 0u);
	// Asked again on a new call: answered again.
	telnet_set_mode(&t, TELNET_ON);
	FEED(IAC, TELNET_DO, TELNET_OPT_NAWS);
	sent_len = 0;
	FEED(IAC, TELNET_DO, TELNET_OPT_NAWS);
	CHECK(SENT(IAC, TELNET_WILL, TELNET_OPT_NAWS, IAC, TELNET_SB, TELNET_OPT_NAWS, 0, 80, 0, 24, IAC, TELNET_SE));
}

static void test_send_doubles_iac_when_active(void) {
	start(TELNET_ON);
	telnet_send_data(&t, (const uint8_t[]){'a', IAC, 'b', IAC}, 4);
	CHECK(SENT('a', IAC, IAC, 'b', IAC, IAC));
	telnet_send_data(&t, (const uint8_t[]){'q'}, 1);
	CHECK(SENT('q'));
}

int main(void) {
	RUN(test_off_passes_everything);
	RUN(test_naws_with_the_size_and_its_changes);
	RUN(test_naws_doubles_255);
	RUN(test_ttype);
	RUN(test_echo_sga_taken_rest_refused);
	RUN(test_data_255_and_other_commands);
	RUN(test_split_across_reads);
	RUN(test_auto_waits_for_a_command);
	RUN(test_reset_and_mode_forget);
	RUN(test_send_doubles_iac_when_active);
	return tests_done();
}
