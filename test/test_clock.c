#include <string.h>

#include "datetime.h"
#include "wallclock.h"
#include "test.h"

static datetime_t at(unsigned year, unsigned month, unsigned mday, unsigned hour, unsigned min, unsigned sec) {
	datetime_t t = {(uint16_t)year, (uint8_t)month, (uint8_t)mday, (uint8_t)hour, (uint8_t)min, (uint8_t)sec, 0};
	return t;
}

static uint32_t secs_of(datetime_t t) {
	uint32_t s = 0;
	CHECK(datetime_to_secs(&t, &s));
	return s;
}

// CP/M day numbers (1 = 1.1.1978) and weekdays, against Python's datetime.
static void test_known_days(void) {
	static const struct {
		unsigned year, month, mday, days, wday;
	} known[] = {
		{1978, 1, 1, 1, 0},     // a Sunday
		{2000, 2, 29, 8095, 2}, // Tuesday
		{2026, 1, 1, 17533, 4}, // Thursday: DATETIME_DEFAULT_DAYS
		{2026, 10, 22, 17827, 4},
		{2114, 2, 6, 49710, 2}, // the last whole day of the count
	};
	for (unsigned i = 0; i < sizeof known / sizeof known[0]; ++i) {
		uint32_t s = secs_of(at(known[i].year, known[i].month, known[i].mday, 0, 0, 0));
		CHECK_EQ(s / DATETIME_DAY, known[i].days);
		datetime_t t;
		datetime_from_secs(s, &t);
		CHECK_EQ(t.year, known[i].year);
		CHECK_EQ(t.month, known[i].month);
		CHECK_EQ(t.mday, known[i].mday);
		CHECK_EQ(t.wday, known[i].wday);
	}
	CHECK_EQ(DATETIME_DEFAULT_DAYS, 17533);
	CHECK_EQ(secs_of(at(2026, 10, 22, 13, 45, 30)), 17827u * DATETIME_DAY + 13 * 3600 + 45 * 60 + 30);
}

// Every day of the count there and back, with a time of day.
static void test_round_trip(void) {
	unsigned bad = 0;
	for (uint32_t day = 1; day <= 49710; ++day) {
		uint32_t s = day * DATETIME_DAY + (day * 7919u) % DATETIME_DAY, back = 0;
		datetime_t t;
		datetime_from_secs(s, &t);
		if (!datetime_to_secs(&t, &back) || back != s || t.wday != (day + 6) % 7) {
			++bad;
		}
	}
	CHECK_EQ(bad, 0);
}

static void test_invalid(void) {
	static const datetime_t bad[] = {
		{1977, 12, 31, 23, 59, 59, 0}, // before CP/M's day 1
		{2026, 4, 31, 0, 0, 0, 0},
		{2027, 2, 29, 0, 0, 0, 0},
		{2100, 2, 29, 0, 0, 0, 0}, // not a leap year
		{2026, 13, 1, 0, 0, 0, 0},
		{2026, 0, 1, 0, 0, 0, 0},
		{2026, 1, 0, 0, 0, 0, 0},
		{2026, 1, 1, 24, 0, 0, 0},
		{2026, 1, 1, 0, 60, 0, 0},
		{2026, 1, 1, 0, 0, 60, 0},
		{2114, 2, 7, 6, 28, 16, 0}, // past 2^32 seconds
	};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
		uint32_t s = 12345;
		CHECK(!datetime_to_secs(&bad[i], &s));
		CHECK_EQ(s, 12345);
	}
	datetime_t leap = at(2024, 2, 29, 23, 59, 59), y2000 = at(2000, 2, 29, 0, 0, 0);
	uint32_t s;
	CHECK(datetime_to_secs(&leap, &s));
	CHECK(datetime_to_secs(&y2000, &s));
}

static void test_fat(void) {
	// 22.10.2026 13:45:31: seconds in 2-second steps.
	CHECK_EQ(datetime_to_fat(secs_of(at(2026, 10, 22, 13, 45, 31))),
		46u << 25 | 10u << 21 | 22u << 16 | 13u << 11 | 45u << 5 | 15u);
	CHECK_EQ(datetime_to_fat(secs_of(at(1979, 6, 1, 12, 0, 0))), 1u << 21 | 1u << 16);
	CHECK_EQ(datetime_to_fat(secs_of(at(2110, 1, 1, 0, 0, 0))),
		127u << 25 | 12u << 21 | 31u << 16 | 23u << 11 | 59u << 5 | 29u);
}

static void test_clock_text(void) {
	char out[DATETIME_CLOCK_MAX + 1];
	uint32_t pm = secs_of(at(2026, 10, 22, 13, 45, 30)), am = secs_of(at(2026, 10, 2, 0, 5, 9));
	static const struct {
		datetime_clock_t format;
		bool colons;
		const char *pm, *am;
	} cases[] = {
		{{false, false, DATETIME_DATE_NONE}, true, "13:45", "00:05"},
		{{false, false, DATETIME_DATE_NONE}, false, "13 45", "00 05"},
		{{false, true, DATETIME_DATE_NONE}, true, "13:45:30", "00:05:09"},
		{{false, true, DATETIME_DATE_NONE}, false, "13 45 30", "00 05 09"},
		{{true, false, DATETIME_DATE_NONE}, true, " 1:45 PM", "12:05 AM"},
		{{true, true, DATETIME_DATE_NONE}, true, " 1:45:30 PM", "12:05:09 AM"},
		{{false, false, DATETIME_DATE_ISO}, true, "2026-10-22 13:45", "2026-10-02 00:05"},
		{{false, false, DATETIME_DATE_SHORT}, true, "Thu 22 Oct 13:45", "Fri 02 Oct 00:05"},
		{{true, true, DATETIME_DATE_SHORT}, true, "Thu 22 Oct  1:45:30 PM", "Fri 02 Oct 12:05:09 AM"},
		{{true, true, DATETIME_DATE_ISO}, false, "2026-10-22  1 45 30 PM", "2026-10-02 12 05 09 AM"},
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		unsigned n = datetime_clock_text(out, pm, cases[i].colons, &cases[i].format);
		CHECK(strcmp(out, cases[i].pm) == 0);
		CHECK_EQ(n, strlen(cases[i].pm));
		CHECK(n <= DATETIME_CLOCK_MAX);
		CHECK_EQ(datetime_clock_text(out, am, cases[i].colons, &cases[i].format), n); // the same width
		CHECK(strcmp(out, cases[i].am) == 0);
	}
	// Noon is 12 PM.
	datetime_clock_t twelve = {true, false, DATETIME_DATE_NONE};
	datetime_clock_text(out, secs_of(at(2026, 1, 1, 12, 0, 0)), true, &twelve);
	CHECK(strcmp(out, "12:00 PM") == 0);
}

#define S 1000000ull // a second in microseconds

static void test_wallclock_timer(void) {
	// No edges: the timer counts from the read; never ticking.
	wallclock_t w = {0};
	wallclock_start(&w, 1000, 5 * S + 300000, false);
	CHECK_EQ(wallclock_now(&w, 5 * S + 300000), 1000);
	CHECK_EQ(wallclock_now(&w, 6 * S + 299999), 1000);
	CHECK_EQ(wallclock_now(&w, 6 * S + 300000), 1001);
	CHECK_EQ(wallclock_fraction(&w, 6 * S + 550000), 250000);
	CHECK(!wallclock_ticking(&w, 6 * S));
}

static void test_wallclock_edges(void) {
	// Read at 0.3 s into second 1000; the chip's next second starts at
	// 0.9 s on the timer: 1001 from there, the fraction from the edge.
	wallclock_t w = {0};
	wallclock_start(&w, 1000, 300000, false);
	wallclock_edge(&w, 900000);
	CHECK_EQ(wallclock_now(&w, 900000), 1001);
	CHECK_EQ(wallclock_now(&w, 899999 + S), 1001);
	CHECK_EQ(wallclock_fraction(&w, 900000 + 400000), 400000);
	CHECK(wallclock_ticking(&w, 900000 + S));

	// A timer 2% fast: the edges keep the chip's seconds.
	uint64_t t = 900000;
	for (unsigned i = 1; i <= 100; ++i) {
		t += S + 20000;
		wallclock_edge(&w, t);
	}
	CHECK_EQ(wallclock_now(&w, t), 1101);
	// 2% slow likewise.
	for (unsigned i = 1; i <= 100; ++i) {
		t += S - 20000;
		wallclock_edge(&w, t);
	}
	CHECK_EQ(wallclock_now(&w, t), 1201);

	// A missed edge (interrupts off): the next counts two.
	t += 2 * S;
	wallclock_edge(&w, t);
	CHECK_EQ(wallclock_now(&w, t), 1203);
	// A glitch 0.1 s after a second's start: ignored.
	wallclock_edge(&w, t + 100000);
	CHECK_EQ(wallclock_now(&w, t + 100000), 1203);
	CHECK_EQ(wallclock_fraction(&w, t + 100000), 100000);

	// Never back: just before each edge the time is at most the edge's.
	wallclock_t v = {0};
	wallclock_start(&v, 50, 0, true);
	uint64_t at = 0;
	unsigned backwards = 0;
	for (unsigned i = 0; i < 1000; ++i) {
		at += S - 300000 + (i * 7919u) % 600000; // 0.7-1.3 s apart
		uint32_t before = wallclock_now(&v, at - 1);
		wallclock_edge(&v, at);
		if (wallclock_now(&v, at) < before) {
			++backwards;
		}
	}
	CHECK_EQ(backwards, 0);

	// Edges stop: not ticking 1.5 s after the last, the timer carries on.
	CHECK(wallclock_ticking(&w, t + 1499999));
	CHECK(!wallclock_ticking(&w, t + 1500000));
	CHECK_EQ(wallclock_now(&w, t + 10 * S), 1213);
}

static void test_wallclock_set(void) {
	// A set (the chip written: its second starts then) and the edge a
	// second later: no extra second.
	wallclock_t w = {0};
	wallclock_start(&w, 1000, 0, false);
	wallclock_edge(&w, 600000);
	wallclock_start(&w, 5000, 2 * S + 250000, true);
	CHECK_EQ(wallclock_now(&w, 2 * S + 250000), 5000);
	wallclock_edge(&w, 3 * S + 250000);
	CHECK_EQ(wallclock_now(&w, 3 * S + 250000), 5001);
	CHECK_EQ(wallclock_fraction(&w, 3 * S + 750000), 500000);
}

int main(void) {
	RUN(test_known_days);
	RUN(test_round_trip);
	RUN(test_invalid);
	RUN(test_fat);
	RUN(test_clock_text);
	RUN(test_wallclock_timer);
	RUN(test_wallclock_edges);
	RUN(test_wallclock_set);
	return tests_done();
}
