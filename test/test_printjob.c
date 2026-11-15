// Print jobs through a Zimodem (printjob.h, bd wt6.3), against a fake one:
// command mode with echo and quiet settings, AT+PRINT's print mode (ended
// by its S61 time-out, or +++), a stream left by an earlier job, +++ with
// its guard times.

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "printjob.h"
#include "test.h"

// ---- The fake modem ----

enum { M_CMD, M_PRINT, M_STREAM };

static struct {
	int mode;
	bool echo, quiet, present;
	uint64_t now, last_in;   // the time, the last byte from the engine
	unsigned pluses;         // +s after a guard time
	uint64_t plus_at;        // the last of them
	char line[160];
	size_t line_len;
	unsigned s61;
	char job[8192];          // what the current print job or connection got
	size_t job_len;
	char done[8192];         // the last finished job's data
	size_t done_len;
	unsigned jobs_done, hangups;
	char spec[96];
	const char *refuse;      // a target (or host) the modem cannot reach
	bool lose_after;         // drop the connection after 100 bytes
	char out[512];           // to the engine
	size_t out_len, out_pos;
	unsigned room;           // what the line takes per poll (0: 64)
	unsigned taken;
	char sent[16384];        // everything the engine sent, for the checks
	size_t sent_len;
} m;

static void modem_reset(void) {
	memset(&m, 0, sizeof m);
	m.present = true;
	m.echo = true;
	m.quiet = true; // the J8 setup: ATE0Q1, but echo on here to see it ignored
	m.s61 = 5;
}

static void say(const char *s) {
	size_t n = strlen(s);
	if (m.out_len + n < sizeof m.out) {
		memcpy(m.out + m.out_len, s, n);
		m.out_len += n;
	}
}

static void respond(const char *s) {
	if (!m.quiet) {
		say("\r\n");
		say(s);
		say("\r\n");
	}
}

static void finish_job(void) {
	memcpy(m.done, m.job, m.job_len);
	m.done_len = m.job_len;
	m.job_len = 0;
	++m.jobs_done;
}

static void job_byte(char c) {
	if (m.job_len < sizeof m.job) {
		m.job[m.job_len++] = c;
	}
	if (m.lose_after && m.job_len == 100 && m.mode == M_PRINT) {
		m.mode = M_CMD;
		respond("ERROR");
	}
}

static void command(void) {
	char up[160];
	size_t n = m.line_len;
	for (size_t i = 0; i <= n; ++i) {
		up[i] = (char)toupper((unsigned char)m.line[i]);
	}
	m.line_len = 0;
	if (n < 2 || strncmp(up, "AT", 2) != 0) {
		return;
	}
	const char *a = up + 2;
	const char *raw = m.line + 2;
	if (strncmp(a, "+PRINT", 6) == 0) {
		snprintf(m.spec, sizeof m.spec, "%s", raw + 6);
		if (m.refuse && strstr(m.spec, m.refuse)) {
			respond("ERROR");
			return;
		}
		m.mode = M_PRINT;
		m.job_len = 0;
		return;
	}
	// Simple commands, any number of them on the line.
	while (*a) {
		if (*a == 'E' || *a == 'Q' || *a == 'V') {
			char c = *a++;
			int v = isdigit((unsigned char)*a) ? *a++ - '0' : 0;
			if (c == 'E') {
				m.echo = v != 0;
			} else if (c == 'Q') {
				m.quiet = v != 0;
			}
		} else if (*a == 'S') {
			unsigned reg = 0, val = 0;
			++a;
			while (isdigit((unsigned char)*a)) {
				reg = reg * 10 + (unsigned)(*a++ - '0');
			}
			if (*a == '=') {
				++a;
			}
			while (isdigit((unsigned char)*a)) {
				val = val * 10 + (unsigned)(*a++ - '0');
			}
			if (reg == 61) {
				m.s61 = val;
			}
		} else if (*a == 'H') {
			++a;
			while (isdigit((unsigned char)*a)) {
				++a;
			}
			++m.hangups;
		} else {
			respond("ERROR");
			return;
		}
	}
	respond("OK");
}

static void modem_in(uint8_t c) {
	if (m.sent_len < sizeof m.sent - 1) {
		m.sent[m.sent_len++] = (char)c;
	}
	uint64_t gap = m.now - m.last_in;
	m.last_in = m.now;
	if (!m.present) {
		return;
	}
	if (m.mode == M_CMD) {
		if (m.echo) {
			char e[2] = {(char)c, 0};
			say(e);
		}
		if (c == '\r') {
			m.line[m.line_len] = '\0';
			command();
		} else if (m.line_len < sizeof m.line - 1) {
			m.line[m.line_len++] = (char)c;
		}
		return;
	}
	// Print or stream mode: +++ after 900 ms of silence, quick, then
	// silence again (modem_tick); anything else is data.
	if (c == '+' && (m.pluses ? gap < 900000 && m.pluses < 3 : gap >= 900000)) {
		++m.pluses;
		m.plus_at = m.now;
		return;
	}
	for (; m.pluses; --m.pluses) {
		job_byte('+');
	}
	job_byte((char)c);
}

static void modem_tick(void) {
	if (!m.present) {
		return;
	}
	if (m.pluses == 3 && m.now - m.plus_at >= 900000) {
		m.pluses = 0;
		if (m.mode == M_PRINT) {
			finish_job();
		}
		m.mode = M_CMD;
		respond("OK");
	} else if (m.mode == M_PRINT && m.now - m.last_in >= (uint64_t)m.s61 * 1000000u) {
		m.mode = M_CMD; // Zimodem's own end of a print job
		finish_job();
		respond("OK");
	}
}

static size_t io_room(void) {
	unsigned room = m.room ? m.room : 64; // a poll's worth of the UART's queue
	return m.taken < room ? room - m.taken : 0;
}

static void io_put(uint8_t c) {
	++m.taken;
	modem_in(c);
}

static bool io_get(uint8_t *c) {
	if (m.out_pos == m.out_len) {
		m.out_pos = m.out_len = 0;
		return false;
	}
	*c = (uint8_t)m.out[m.out_pos++];
	return true;
}

static const printjob_io_t io = {.room = io_room, .put = io_put, .get = io_get};

// ---- The spool hook ----

static unsigned spool_bytes, spool_ends;

static bool spool_offer(void *ctx, uint8_t c) {
	(void)ctx;
	(void)c;
	++spool_bytes;
	return true;
}

static void spool_end(void *ctx) {
	(void)ctx;
	++spool_ends;
}

static const printjob_spool_t spool = {.offer = spool_offer, .job_end = spool_end};

// ---- Driving it ----

static printjob_t pj;

static void setup(uint8_t kind, bool text, const char *target) {
	modem_reset();
	m.now = 10000000; // well after power-on
	spool_bytes = spool_ends = 0;
	printjob_init(&pj, &io, &spool);
	printjob_config_t cfg = {.kind = kind, .text = text, .idle_s = 5};
	snprintf(cfg.target, sizeof cfg.target, "%s", target);
	printjob_configure(&pj, &cfg);
}

// Run for ms in 1 ms steps.
static void run(unsigned ms) {
	for (unsigned i = 0; i < ms; ++i) {
		m.now += 1000;
		m.taken = 0;
		printjob_poll(&pj, m.now);
		modem_tick();
	}
}

// Print text as the Z80 would: a byte whenever the printer is ready, the
// engine polled 1 ms apart meanwhile; false if it stayed busy for 30 s.
static bool print_n(const char *text, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		unsigned waited = 0;
		while (!printjob_ready(&pj)) {
			run(1);
			if (++waited > 30000) {
				return false;
			}
		}
		printjob_put(&pj, (uint8_t)text[i], m.now);
	}
	return true;
}

static bool print(const char *text) {
	return print_n(text, strlen(text));
}

// Run until the engine is idle again, at most ms.
static bool run_until_idle(unsigned ms) {
	for (unsigned i = 0; i < ms; ++i) {
		run(1);
		if (pj.state == PRINTJOB_IDLE) {
			return true;
		}
	}
	return false;
}

static const char *const TEXT = "Hello, printer.\r\nA second line\r\n\f";

// ---- The tests ----

static void test_ipp_text_job(void) {
	setup(PRINTJOB_IPP, true, "192.168.1.10:631/ipp/print");
	CHECK(printjob_ready(&pj));
	CHECK(print(TEXT));
	CHECK(pj.state == PRINTJOB_OPENING); // all taken while opening
	run(3000);
	CHECK(pj.state == PRINTJOB_PRINTING);
	CHECK_EQ(m.mode, M_PRINT);
	CHECK(strcmp(m.spec, "A:192.168.1.10:631/ipp/print") == 0);
	CHECK_EQ(m.s61, 5); // Zimodem ends the job after the idle time
	CHECK(!m.quiet && !m.echo);
	CHECK(run_until_idle(10000));
	// Ended by Zimodem's S61, no +++ (it would have printed).
	CHECK_EQ(m.jobs_done, 1);
	CHECK_EQ(m.done_len, strlen(TEXT));
	CHECK(memcmp(m.done, TEXT, strlen(TEXT)) == 0);
	CHECK(strstr(m.sent, "+++") == NULL);
	CHECK_EQ(m.mode, M_CMD);
	CHECK_EQ(pj.error, PRINTJOB_ERR_NONE);
	CHECK_EQ(pj.jobs, 1);
	CHECK_EQ(pj.failed, 0);
	CHECK_EQ(pj.bytes, strlen(TEXT));
	CHECK_EQ(m.hangups, 0); // nothing to hang up
	// A second job the same way.
	CHECK(print("again\r\n"));
	CHECK(run_until_idle(10000));
	CHECK_EQ(m.jobs_done, 2);
	CHECK(m.done_len == 7 && memcmp(m.done, "again\r\n", 7) == 0);
	CHECK_EQ(spool_bytes, 0);
}

// OUT 5,1 stops taking the job's data; Zimodem still ends it after its
// idle time.
static void test_ipp_raw_and_end_now(void) {
	setup(PRINTJOB_IPP, false, "printer.lan:631/ipp/print");
	CHECK(print("%!PS\n"));
	printjob_end(&pj);
	run(3000);
	CHECK(pj.state == PRINTJOB_CLOSING);
	CHECK(!printjob_ready(&pj));
	CHECK(run_until_idle(10000));
	CHECK(strncmp(m.spec, "R:printer.lan", 13) == 0);
	CHECK(m.done_len == 5 && memcmp(m.done, "%!PS\n", 5) == 0);
	CHECK_EQ(pj.error, PRINTJOB_ERR_NONE);
}

static void test_pending_while_opening(void) {
	static char big[600];
	setup(PRINTJOB_IPP, true, "h:631/p");
	for (size_t i = 0; i < sizeof big - 1; ++i) {
		big[i] = (char)('a' + i % 26);
	}
	printjob_put(&pj, 'x', m.now);
	unsigned took = 1;
	while (printjob_ready(&pj)) {
		printjob_put(&pj, 'x', m.now);
		++took;
	}
	CHECK_EQ(took, PRINTJOB_PENDING); // then busy until the job is open
	CHECK(pj.state == PRINTJOB_OPENING);
	CHECK(print(big));
	CHECK(run_until_idle(10000));
	CHECK_EQ(m.done_len, PRINTJOB_PENDING + strlen(big));
	CHECK(memcmp(m.done + PRINTJOB_PENDING, big, strlen(big)) == 0);
}

static void test_line_full(void) {
	setup(PRINTJOB_IPP, true, "h:631/p");
	m.room = 2; // two bytes a poll
	CHECK(print(TEXT));
	CHECK(run_until_idle(10000));
	CHECK_EQ(m.done_len, strlen(TEXT));
}

static void test_modem_left_in_a_job(void) {
	// The board reset during a job: the modem still prints. CR and AT go
	// into that job; no answer, so +++ (after the guard time) and ATH.
	setup(PRINTJOB_IPP, true, "h:631/p");
	m.mode = M_PRINT;
	m.quiet = false;
	m.echo = false;
	m.s61 = 60;
	CHECK(print(TEXT));
	CHECK(run_until_idle(20000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_NONE);
	CHECK_EQ(m.jobs_done, 2); // the stray one, then ours
	CHECK(m.done_len == strlen(TEXT) && memcmp(m.done, TEXT, strlen(TEXT)) == 0);
	CHECK(strstr(m.sent, "+++ATH\r") != NULL);
	CHECK_EQ(pj.failed, 0);
	// Left in a stream (the dropped raw TCP mode's): the same.
	setup(PRINTJOB_IPP, true, "h:631/p");
	m.mode = M_STREAM;
	m.quiet = false;
	m.echo = false;
	CHECK(print(TEXT));
	CHECK(run_until_idle(20000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_NONE);
	CHECK(m.done_len == strlen(TEXT) && memcmp(m.done, TEXT, strlen(TEXT)) == 0);
}

static void test_no_modem(void) {
	setup(PRINTJOB_IPP, true, "h:631/p");
	m.present = false;
	CHECK(print(TEXT)); // never stuck
	CHECK(run_until_idle(30000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_NO_MODEM);
	CHECK_EQ(pj.failed, 1);
	CHECK_EQ(spool_bytes, strlen(TEXT)); // all of the job went to the spool hook
	CHECK_EQ(spool_ends, 1);
	CHECK(strstr(m.sent, "+++") != NULL); // tried the escape once
}

static void test_refused(void) {
	setup(PRINTJOB_IPP, true, "dead.lan:631/p");
	m.refuse = "dead.lan";
	CHECK(print(TEXT));
	CHECK(run_until_idle(30000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_REFUSED);
	CHECK_EQ(m.jobs_done, 0);
	CHECK_EQ(spool_bytes, strlen(TEXT));
	// The next job tries again (and works).
	m.refuse = NULL;
	CHECK(print(TEXT));
	CHECK(run_until_idle(30000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_NONE);
	CHECK_EQ(pj.failed, 1);
}

static void test_lost(void) {
	static char big[300];
	memset(big, 'z', sizeof big - 1);
	setup(PRINTJOB_IPP, true, "h:631/p");
	m.lose_after = true;
	CHECK(print(big));
	CHECK(run_until_idle(30000));
	CHECK_EQ(pj.error, PRINTJOB_ERR_LOST);
	CHECK(spool_bytes > 0);
}

static void test_off(void) {
	setup(PRINTJOB_OFF, true, "h:631/p");
	CHECK(print(TEXT));
	run(1000);
	CHECK_EQ(m.sent_len, 0);
	CHECK_EQ(pj.jobs, 0);
}

int main(void) {
	RUN(test_ipp_text_job);
	RUN(test_ipp_raw_and_end_now);
	RUN(test_pending_while_opening);
	RUN(test_line_full);
	RUN(test_modem_left_in_a_job);
	RUN(test_no_modem);
	RUN(test_refused);
	RUN(test_lost);
	RUN(test_off);
	return tests_done();
}
