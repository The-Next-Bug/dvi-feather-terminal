#include <string.h>

#include "datetime.h"
#include "ds3231.h"
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

static void test_ds3231(void) {
	// 22.10.2026 13:45:30, a Thursday (day 5 counting Sunday as 1).
	datetime_t t = at(2026, 10, 22, 13, 45, 30), got;
	t.wday = 4;
	uint8_t regs[DS3231_TIME_REGS];
	ds3231_encode(&t, regs);
	static const uint8_t want[DS3231_TIME_REGS] = {0x30, 0x45, 0x13, 5, 0x22, 0x10, 0x26};
	CHECK(memcmp(regs, want, sizeof want) == 0);
	CHECK(ds3231_decode(regs, &got));
	CHECK(memcmp(&got, &t, sizeof t) == 0);

	// The century bit: 2100-2199.
	datetime_t later = at(2105, 3, 1, 0, 0, 0);
	ds3231_encode(&later, regs);
	CHECK_EQ(regs[5], 0x83);
	CHECK_EQ(regs[6], 0x05);
	CHECK(ds3231_decode(regs, &got));
	CHECK_EQ(got.year, 2105);

	// 12-hour mode (set by something else): 12 AM is 0, 1 PM is 13.
	uint8_t twelve[DS3231_TIME_REGS] = {0x00, 0x00, 0x40 | 0x12, 1, 0x01, 0x01, 0x26};
	CHECK(ds3231_decode(twelve, &got));
	CHECK_EQ(got.hour, 0);
	twelve[2] = 0x40 | 0x20 | 0x01;
	CHECK(ds3231_decode(twelve, &got));
	CHECK_EQ(got.hour, 13);
	twelve[2] = 0x40 | 0x20 | 0x12; // 12 PM: noon
	CHECK(ds3231_decode(twelve, &got));
	CHECK_EQ(got.hour, 12);

	// A weekday register that disagrees does not matter: it comes from the date.
	uint8_t wrong_day[DS3231_TIME_REGS] = {0x30, 0x45, 0x13, 7, 0x22, 0x10, 0x26};
	CHECK(ds3231_decode(wrong_day, &got));
	CHECK_EQ(got.wday, 4);

	// Not a time: bad BCD, out of range, a date that does not exist.
	static const uint8_t bad[][DS3231_TIME_REGS] = {
		{0x5a, 0x00, 0x00, 1, 0x01, 0x01, 0x26},
		{0x60, 0x00, 0x00, 1, 0x01, 0x01, 0x26},
		{0x00, 0x00, 0x24, 1, 0x01, 0x01, 0x26},
		{0x00, 0x00, 0x40, 1, 0x01, 0x01, 0x26}, // 12-hour 0
		{0x00, 0x00, 0x00, 1, 0x31, 0x04, 0x26}, // 31 April
		{0x00, 0x00, 0x00, 1, 0x29, 0x02, 0x27},
		{0x00, 0x00, 0x00, 1, 0x00, 0x01, 0x26},
		{0x00, 0x00, 0x00, 1, 0x01, 0x13, 0x26},
	};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
		CHECK(!ds3231_decode(bad[i], &got));
	}
}

static void test_fat(void) {
	// 22.10.2026 13:45:31: seconds in 2-second steps.
	CHECK_EQ(datetime_to_fat(secs_of(at(2026, 10, 22, 13, 45, 31))),
		46u << 25 | 10u << 21 | 22u << 16 | 13u << 11 | 45u << 5 | 15u);
	CHECK_EQ(datetime_to_fat(secs_of(at(1979, 6, 1, 12, 0, 0))), 1u << 21 | 1u << 16);
	CHECK_EQ(datetime_to_fat(secs_of(at(2110, 1, 1, 0, 0, 0))),
		127u << 25 | 12u << 21 | 31u << 16 | 23u << 11 | 59u << 5 | 29u);
}

int main(void) {
	RUN(test_known_days);
	RUN(test_round_trip);
	RUN(test_invalid);
	RUN(test_ds3231);
	RUN(test_fat);
	return tests_done();
}
