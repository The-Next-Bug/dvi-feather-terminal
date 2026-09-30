#include <string.h>

#include "keyname.h"
#include "test.h"

static const char *name(const char *bytes) {
	static char out[32];
	keyname((const uint8_t *)bytes, strlen(bytes), out, sizeof out);
	return out;
}

static void test_printable(void) {
	CHECK(!strcmp(name("a"), "a"));
	CHECK(!strcmp(name("~"), "~"));
	CHECK(!strcmp(name(" "), "SP"));
}

static void test_controls(void) {
	CHECK(!strcmp(name("\x7f"), "DEL"));
	CHECK(!strcmp(name("\b"), "BS"));
	CHECK(!strcmp(name("\t"), "TAB"));
	CHECK(!strcmp(name("\r\n"), "CR LF"));
	CHECK(!strcmp(name("\x03"), "^C"));
	CHECK(!strcmp(name("\x1c"), "^\\"));
	uint8_t nul = 0;
	char out[8];
	keyname(&nul, 1, out, sizeof out);
	CHECK(!strcmp(out, "NUL"));
}

static void test_sequences(void) {
	CHECK(!strcmp(name("\x1b[A"), "ESC [A"));
	CHECK(!strcmp(name("\x1bOP"), "ESC OP"));
	CHECK(!strcmp(name("\x1b[24~"), "ESC [24~"));
	CHECK(!strcmp(name("\x1b" "a"), "ESC a"));
	CHECK(!strcmp(name("\x1b\x03"), "ESC ^C"));
	CHECK(!strcmp(name("\x9b"), "<9b>"));
}

static void test_truncates(void) {
	char out[6];
	keyname((const uint8_t *)"\x1b[24~", 5, out, sizeof out);
	CHECK(!strcmp(out, "ESC ["));
	keyname((const uint8_t *)"a", 1, out, 1);
	CHECK(!strcmp(out, ""));
}

int main(void) {
	RUN(test_printable);
	RUN(test_controls);
	RUN(test_sequences);
	RUN(test_truncates);
	return tests_done();
}
