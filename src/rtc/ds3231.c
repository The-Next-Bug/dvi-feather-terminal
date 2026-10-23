#include "ds3231.h"

#include "rtc_chip.h"
#include "rtc_internal.h"

bool ds3231_decode(const uint8_t regs[DS3231_TIME_REGS], datetime_t *t) {
	datetime_t d = {0};
	uint8_t hour = regs[2], year;
	if (hour & 0x40) { // 12-hour mode: bit 5 PM, hours 1-12
		uint8_t h;
		if (!rtc_from_bcd(hour & 0x1f, 12, &h) || h == 0) {
			return false;
		}
		d.hour = (uint8_t)(h % 12 + (hour & 0x20 ? 12 : 0));
	} else if (!rtc_from_bcd(hour & 0x3f, 23, &d.hour)) {
		return false;
	}
	if (!rtc_from_bcd(regs[0] & 0x7f, 59, &d.sec) || !rtc_from_bcd(regs[1] & 0x7f, 59, &d.min) ||
		!rtc_from_bcd(regs[4] & 0x3f, 31, &d.mday) || !rtc_from_bcd(regs[5] & 0x1f, 12, &d.month) ||
		!rtc_from_bcd(regs[6], 99, &year)) {
		return false;
	}
	d.year = (uint16_t)(2000 + year + (regs[5] & 0x80 ? 100 : 0));
	uint32_t secs;
	if (!datetime_to_secs(&d, &secs)) {
		return false;
	}
	datetime_from_secs(secs, t); // with its weekday
	return true;
}

void ds3231_encode(const datetime_t *t, uint8_t regs[DS3231_TIME_REGS]) {
	unsigned year = t->year - 2000u;
	regs[0] = rtc_to_bcd(t->sec);
	regs[1] = rtc_to_bcd(t->min);
	regs[2] = rtc_to_bcd(t->hour);
	regs[3] = (uint8_t)(t->wday + 1);
	regs[4] = rtc_to_bcd(t->mday);
	regs[5] = (uint8_t)(rtc_to_bcd(t->month) | (year >= 100 ? 0x80 : 0));
	regs[6] = rtc_to_bcd(year % 100);
}

static bool get(const rtc_bus_t *bus, uint8_t reg, uint8_t *buf, size_t n) {
	return bus->read(bus->ctx, DS3231_ADDR, reg, buf, n);
}

static bool put(const rtc_bus_t *bus, uint8_t reg, const uint8_t *buf, size_t n) {
	return bus->write(bus->ctx, DS3231_ADDR, reg, buf, n);
}

static rtc_chip_status_t ds3231_start(const rtc_bus_t *bus, datetime_t *t) {
	uint8_t status, regs[DS3231_TIME_REGS];
	if (!get(bus, DS3231_REG_STATUS, &status, 1) || !get(bus, DS3231_REG_TIME, regs, sizeof regs)) {
		return RTC_CHIP_MISSING;
	}
	if (status & DS3231_STATUS_OSF || !ds3231_decode(regs, t)) {
		return RTC_CHIP_NOT_SET;
	}
	return RTC_CHIP_OK;
}

static bool ds3231_set(const rtc_bus_t *bus, const datetime_t *t) {
	if (t->year < 2000 || t->year > 2199) {
		return false;
	}
	uint8_t regs[DS3231_TIME_REGS], status;
	ds3231_encode(t, regs);
	// The time, then the oscillator-stopped flag cleared: valid from now.
	if (!put(bus, DS3231_REG_TIME, regs, sizeof regs) || !get(bus, DS3231_REG_STATUS, &status, 1)) {
		return false;
	}
	status &= (uint8_t)~DS3231_STATUS_OSF;
	return put(bus, DS3231_REG_STATUS, &status, 1);
}

// INT/SQW as the 1 Hz square wave (the reset state is the alarms'
// interrupt, idle high), the oscillator kept running on the battery. It
// goes high 500 ms after a seconds write and falls at the next second.
static bool ds3231_start_1hz(const rtc_bus_t *bus) {
	uint8_t control;
	if (!get(bus, DS3231_REG_CONTROL, &control, 1)) {
		return false;
	}
	control &= (uint8_t)~(DS3231_CONTROL_EOSC | DS3231_CONTROL_RS | DS3231_CONTROL_INTCN);
	return put(bus, DS3231_REG_CONTROL, &control, 1);
}

const rtc_chip_t rtc_ds3231 = {
	.name = "DS3231",
	.start = ds3231_start,
	.set = ds3231_set,
	.start_1hz = ds3231_start_1hz,
};
