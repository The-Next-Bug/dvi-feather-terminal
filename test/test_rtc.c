#include <string.h>

#include "datetime.h"
#include "ds3231.h"
#include "rtc_chip.h"
#include "test.h"

static datetime_t at(unsigned year, unsigned month, unsigned mday, unsigned hour, unsigned min, unsigned sec) {
	datetime_t t = {(uint16_t)year, (uint8_t)month, (uint8_t)mday, (uint8_t)hour, (uint8_t)min, (uint8_t)sec, 0};
	return t;
}

// A chip on the bus: its registers, auto-incrementing, wrapping at the end
// of the file as the DS3231's do.
typedef struct {
	uint8_t addr;
	bool present;
	uint8_t regs[0x20];
	unsigned writes;
} fake_chip_t;

static bool fake_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
	fake_chip_t *c = ctx;
	if (!c->present || addr != c->addr) {
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		buf[i] = c->regs[(reg + i) % sizeof c->regs];
	}
	return true;
}

static bool fake_write(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n) {
	fake_chip_t *c = ctx;
	CHECK(n <= RTC_BUS_MAX);
	if (!c->present || addr != c->addr) {
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		c->regs[(reg + i) % sizeof c->regs] = buf[i];
	}
	++c->writes;
	return true;
}

static rtc_bus_t bus_of(fake_chip_t *c) {
	rtc_bus_t b = {fake_read, fake_write, c};
	return b;
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

static void test_ds3231_driver(void) {
	// 22.10.2026 13:45:30, the oscillator-stopped flag set (as at power-on).
	fake_chip_t c = {.addr = DS3231_ADDR, .present = true,
		.regs = {0x30, 0x45, 0x13, 5, 0x22, 0x10, 0x26}};
	c.regs[DS3231_REG_CONTROL] = 0x1c; // reset state: INTCN, RS 8 kHz
	c.regs[DS3231_REG_STATUS] = DS3231_STATUS_OSF | 0x08;
	rtc_bus_t bus = bus_of(&c);
	datetime_t t;
	CHECK_EQ(rtc_ds3231.start(&bus, &t), RTC_CHIP_NOT_SET);

	c.regs[DS3231_REG_STATUS] = 0x08;
	CHECK_EQ(rtc_ds3231.start(&bus, &t), RTC_CHIP_OK);
	CHECK_EQ(t.year, 2026);
	CHECK_EQ(t.hour, 13);
	CHECK_EQ(t.wday, 4);

	// Not a time in the registers: not set.
	c.regs[0] = 0x60;
	CHECK_EQ(rtc_ds3231.start(&bus, &t), RTC_CHIP_NOT_SET);

	// Setting writes the time and clears only the flag.
	c.regs[DS3231_REG_STATUS] = DS3231_STATUS_OSF | 0x08;
	datetime_t set = at(2031, 7, 4, 9, 8, 7);
	datetime_t check;
	uint32_t secs;
	CHECK(datetime_to_secs(&set, &secs));
	datetime_from_secs(secs, &set);
	CHECK(rtc_ds3231.set(&bus, &set));
	CHECK_EQ(c.regs[DS3231_REG_STATUS], 0x08);
	CHECK_EQ(rtc_ds3231.start(&bus, &check), RTC_CHIP_OK);
	CHECK(memcmp(&check, &set, sizeof set) == 0);

	// Years it cannot keep: nothing written.
	unsigned writes = c.writes;
	datetime_t early = at(1999, 12, 31, 0, 0, 0), late = at(2200, 1, 1, 0, 0, 0);
	CHECK(!rtc_ds3231.set(&bus, &early));
	CHECK(!rtc_ds3231.set(&bus, &late));
	CHECK_EQ(c.writes, writes);

	// The 1 Hz square wave: INTCN, RS and EOSC cleared, the rest kept.
	c.regs[DS3231_REG_CONTROL] = 0x80 | 0x1c | 0x40;
	CHECK(rtc_ds3231.start_1hz(&bus));
	CHECK_EQ(c.regs[DS3231_REG_CONTROL], 0x40);

	// No chip.
	c.present = false;
	CHECK_EQ(rtc_ds3231.start(&bus, &t), RTC_CHIP_MISSING);
	CHECK(!rtc_ds3231.set(&bus, &set));
	CHECK(!rtc_ds3231.start_1hz(&bus));
}

int main(void) {
	RUN(test_ds3231);
	RUN(test_ds3231_driver);
	return tests_done();
}
