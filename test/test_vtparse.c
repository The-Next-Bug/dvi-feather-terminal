#include <stdio.h>
#include <string.h>

#include "test.h"
#include "vtparse.h"

// Each callback appends a token to `got`:
//   P(x) print (printable ASCII as itself, else hex), X(hh) execute,
//   E[<intermediates><final>] ESC dispatch,
//   C[<marker><params joined by ;><intermediates><final>] CSI dispatch.
static char got[1024];
static vtparse_t p;

static void append(const char *s) {
	strncat(got, s, sizeof got - strlen(got) - 1);
}

static void on_parse(vtparse_t *vp, vtparse_action_t action, uint8_t ch) {
	char tok[128];
	switch (action) {
	case VTPARSE_PRINT:
		if (ch > 0x20 && ch < 0x7f)
			snprintf(tok, sizeof tok, "P(%c)", ch);
		else
			snprintf(tok, sizeof tok, "P(%02x)", ch);
		break;
	case VTPARSE_EXECUTE:
		snprintf(tok, sizeof tok, "X(%02x)", ch);
		break;
	case VTPARSE_ESC_DISPATCH:
		snprintf(tok, sizeof tok, "E[%.*s%c]", vp->n_intermediates, (const char *)vp->intermediates, ch);
		break;
	case VTPARSE_CSI_DISPATCH: {
		char params[96] = "";
		for (unsigned i = 0; i < vp->n_params; ++i) {
			char n[8];
			snprintf(n, sizeof n, i ? ";%u" : "%u", vp->params[i]);
			strcat(params, n);
		}
		snprintf(tok, sizeof tok, "C[%s%s%.*s%c]", vp->private_marker ? (char[]){ (char)vp->private_marker, 0 } : "",
			params, vp->n_intermediates, (const char *)vp->intermediates, ch);
		break;
	}
	}
	append(tok);
}

static const char *feed(const char *bytes) {
	got[0] = 0;
	for (const char *b = bytes; *b; ++b)
		vtparse_byte(&p, (uint8_t)*b);
	return got;
}

#define EXPECT(input, want) do { \
	const char *g_ = feed(input); \
	if (strcmp(g_, want) != 0) { \
		fprintf(stderr, "%s:%d: feed(%s)\n  got  %s\n  want %s\n", __FILE__, __LINE__, #input, g_, want); \
		++test_failures; \
	} \
} while (0)

static void setup(void) {
	vtparse_init(&p, on_parse, NULL);
}

static void test_text_and_controls(void) {
	setup();
	EXPECT("ab\r\n", "P(a)P(b)X(0d)X(0a)");
	EXPECT("\x07\x08\x09", "X(07)X(08)X(09)");
	EXPECT("\xe9\xff", "P(e9)P(ff)"); // Latin-1 prints
}

static void test_escape_sequences(void) {
	setup();
	EXPECT("\x1b" "7\x1b" "8", "E[7]E[8]");
	EXPECT("\x1b(0\x1b)B", "E[(0]E[)B]");
	EXPECT("\x1b#8", "E[#8]");
	EXPECT("\x1b" "c", "E[c]");
}

static void test_csi_params(void) {
	setup();
	EXPECT("\x1b[H", "C[H]");
	EXPECT("\x1b[5;10H", "C[5;10H]");
	EXPECT("\x1b[;5H", "C[0;5H]"); // missing first parameter
	EXPECT("\x1b[5;H", "C[5;0H]");
	EXPECT("\x1b[0;1;31m", "C[0;1;31m]");
	EXPECT("\x1b[?25l\x1b[?1;7h", "C[?25l]C[?1;7h]");
	EXPECT("\x1b[99999A", "C[65535A]"); // clamped
}

static void test_too_many_params(void) {
	setup();
	feed("\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18m");
	CHECK_EQ(p.n_params, VTPARSE_MAX_PARAMS);
	CHECK_EQ(p.params[15], 16);
}

static void test_c0_inside_sequences_executes(void) {
	setup();
	// VT100 behaviour: controls act immediately and the sequence continues.
	EXPECT("\x1b[1\r2H", "X(0d)C[12H]");
	EXPECT("\x1b\n" "D", "X(0a)E[D]");
}

static void test_can_sub_and_esc_abort(void) {
	setup();
	EXPECT("\x1b[12\x18" "A", "X(18)P(A)");
	EXPECT("\x1b[12\x1a" "A", "X(1a)P(A)");
	EXPECT("\x1b[12\x1b[3m", "C[3m]"); // ESC restarts
	EXPECT("\x1b(\x18" "0", "X(18)P(0)");
}

static void test_ignored_sequences(void) {
	setup();
	EXPECT("\x1b[1:2mZ", "P(Z)"); // colon: CSI ignored
	EXPECT("\x1b[1?2mZ", "P(Z)"); // misplaced private marker
	EXPECT("\x1b[1 !qZ", "C[1 !q]P(Z)"); // two intermediates are fine
	EXPECT("\x1b[1 !\"qZ", "P(Z)"); // three are too many
	EXPECT("\x1b]0;window title\x07ok", "P(o)P(k)"); // OSC, BEL-terminated
	EXPECT("\x1b]2;t\x1b\\ok", "E[\\]P(o)P(k)"); // OSC, ST-terminated
	EXPECT("\x1bPq#0;2;0;0;0#0~~@@\x1b\\x", "E[\\]P(x)"); // DCS (sixel) discarded
	EXPECT("\x1b_apc\x1b\\x", "E[\\]P(x)");
}

static void test_del_ignored_in_sequences(void) {
	setup();
	EXPECT("\x1b[1\x7f;2H", "C[1;2H]");
}

static void test_c1_ignored_by_default(void) {
	setup();
	EXPECT("\x9b" "1m\x85", "P(1)P(m)");
	// UTF-8 for U+2502 (box drawing) is E2 94 82: the continuation bytes
	// must not act as controls.
	EXPECT("\xe2\x94\x82", "P(e2)");
}

static void test_c1_controls_when_enabled(void) {
	setup();
	p.c1_controls = true;
	EXPECT("\x9b" "1m", "C[1m]");
	EXPECT("\x85", "X(85)");
	EXPECT("\x9d" "0;t\x9cok", "P(o)P(k)"); // OSC ... ST
	EXPECT("\x1b[\xb1\xed", "C[1m]"); // GR bytes inside a sequence act as GL
}

static void test_unterminated_strings(void) {
	setup();
	// A C0 control ends the string and still acts (xterm brokenStringTerm).
	EXPECT("\x1b]0;title\rok", "X(0d)P(o)P(k)");
	EXPECT("\x1b]x\nok", "X(0a)P(o)P(k)");
	EXPECT("\x1bPq#0\rok", "X(0d)P(o)P(k)");
	EXPECT("\x1b_apc\bok", "X(08)P(o)P(k)");
	EXPECT("\x1bX\x07ok", "X(07)P(o)P(k)"); // BEL ends only OSC silently
	EXPECT("\x1b^pm\tok", "X(09)P(o)P(k)");
	// NUL (padding) and DEL stay in the string.
	static const uint8_t nul[] = { 0x1b, ']', '0', 0x00, 0x7f, 'x', 0x07, 'o' };
	got[0] = 0;
	for (unsigned i = 0; i < sizeof nul; ++i)
		vtparse_byte(&p, nul[i]);
	CHECK(strcmp(got, "P(o)") == 0);
	// GR bytes in a string do not end it.
	EXPECT("\x1b]0;\xe9\xff\x07ok", "P(o)P(k)");

	// The length limit: VTPARSE_MAX_STRING bytes are discarded, then the
	// parser is back in ground state.
	for (int kind = 0; kind < 2; ++kind) {
		got[0] = 0;
		vtparse_byte(&p, 0x1b);
		vtparse_byte(&p, kind ? 'P' : ']');
		for (unsigned i = 0; i < VTPARSE_MAX_STRING - 1; ++i)
			vtparse_byte(&p, 'a');
		CHECK(strcmp(got, "") == 0);
		vtparse_byte(&p, 'b'); // the last one discarded
		CHECK(strcmp(got, "") == 0);
		EXPECT("ok", "P(o)P(k)");
	}
	// Each string starts its own count.
	for (int n = 0; n < 3; ++n) {
		got[0] = 0;
		vtparse_byte(&p, 0x1b);
		vtparse_byte(&p, ']');
		for (unsigned i = 0; i < VTPARSE_MAX_STRING - 2; ++i)
			vtparse_byte(&p, 'a');
		vtparse_byte(&p, 0x07);
		CHECK(strcmp(got, "") == 0);
	}
	p.c1_controls = true;
	got[0] = 0;
	vtparse_byte(&p, 0x90);
	for (unsigned i = 0; i < VTPARSE_MAX_STRING - 1; ++i)
		vtparse_byte(&p, 'a');
	vtparse_byte(&p, 0x9c);
	EXPECT("ok", "P(o)P(k)");
}

static void test_reset(void) {
	setup();
	EXPECT("\x1b]0;stuck", ""); // unterminated OSC
	vtparse_reset(&p);
	EXPECT("ok", "P(o)P(k)");
	EXPECT("\x1bPq#0", ""); // unterminated DCS
	vtparse_reset(&p);
	EXPECT("ok", "P(o)P(k)");
	EXPECT("\x1b[?31;4", ""); // mid-CSI: marker and parameters dropped
	vtparse_reset(&p);
	EXPECT("m\x1b[m", "P(m)C[m]");
	// c1_controls survives.
	p.c1_controls = true;
	vtparse_reset(&p);
	EXPECT("\x9b" "1m", "C[1m]");
}

int main(void) {
	RUN(test_text_and_controls);
	RUN(test_escape_sequences);
	RUN(test_csi_params);
	RUN(test_too_many_params);
	RUN(test_c0_inside_sequences_executes);
	RUN(test_can_sub_and_esc_abort);
	RUN(test_ignored_sequences);
	RUN(test_del_ignored_in_sequences);
	RUN(test_c1_ignored_by_default);
	RUN(test_c1_controls_when_enabled);
	RUN(test_unterminated_strings);
	RUN(test_reset);
	return tests_done();
}
