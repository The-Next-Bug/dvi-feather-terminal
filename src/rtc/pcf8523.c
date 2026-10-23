#include "pcf8523.h"

#include "rtc_chip.h"
#include "rtc_internal.h"

bool pcf8523_decode(const uint8_t regs[PCF8523_TIME_REGS], bool twelve_hour, datetime_t *t) {
	datetime_t d = {0};
	uint8_t hour = regs[2], year;
	if (twelve_hour) { // bit 5 PM, hours 1-12
		uint8_t h;
		if (!rtc_from_bcd(hour & 0x1f, 12, &h) || h == 0) {
			return false;
		}
		d.hour = (uint8_t)(h % 12 + (hour & 0x20 ? 12 : 0));
	} else if (!rtc_from_bcd(hour & 0x3f, 23, &d.hour)) {
		return false;
	}
	if (!rtc_from_bcd(regs[0] & 0x7f, 59, &d.sec) || !rtc_from_bcd(regs[1] & 0x7f, 59, &d.min) ||
		!rtc_from_bcd(regs[3] & 0x3f, 31, &d.mday) || !rtc_from_bcd(regs[5] & 0x1f, 12, &d.month) ||
		!rtc_from_bcd(regs[6], 99, &year)) {
		return false;
	}
	d.year = (uint16_t)(2000 + year);
	uint32_t secs;
	if (!datetime_to_secs(&d, &secs)) {
		return false;
	}
	datetime_from_secs(secs, t); // with its weekday
	return true;
}

void pcf8523_encode(const datetime_t *t, uint8_t regs[PCF8523_TIME_REGS]) {
	regs[0] = rtc_to_bcd(t->sec);
	regs[1] = rtc_to_bcd(t->min);
	regs[2] = rtc_to_bcd(t->hour);
	regs[3] = rtc_to_bcd(t->mday);
	regs[4] = t->wday;
	regs[5] = rtc_to_bcd(t->month);
	regs[6] = rtc_to_bcd(t->year - 2000u);
}

static bool get(const rtc_bus_t *bus, uint8_t reg, uint8_t *buf, size_t n) {
	return bus->read(bus->ctx, PCF8523_ADDR, reg, buf, n);
}

static bool put(const rtc_bus_t *bus, uint8_t reg, uint8_t v) {
	return bus->write(bus->ctx, PCF8523_ADDR, reg, &v, 1);
}

// The battery switch-over on (the power-on state has it off: the time
// would go with the power) and CLKOUT off (it runs at 32 kHz from reset;
// no board uses it yet). Then the time, valid if the oscillator has not
// stopped since it was set and the clock runs.
static rtc_chip_status_t pcf8523_start(const rtc_bus_t *bus, datetime_t *t) {
	uint8_t control[3], clkout, regs[PCF8523_TIME_REGS];
	if (!get(bus, PCF8523_REG_CONTROL_1, control, sizeof control) || !get(bus, PCF8523_REG_TMR_CLKOUT, &clkout, 1)) {
		return RTC_CHIP_MISSING;
	}
	if ((control[2] & PCF8523_CONTROL_3_PM) &&
		!put(bus, PCF8523_REG_CONTROL_3, (uint8_t)(control[2] & ~PCF8523_CONTROL_3_PM))) {
		return RTC_CHIP_MISSING;
	}
	if ((clkout & PCF8523_CLKOUT_COF) != PCF8523_CLKOUT_COF &&
		!put(bus, PCF8523_REG_TMR_CLKOUT, (uint8_t)(clkout | PCF8523_CLKOUT_COF))) {
		return RTC_CHIP_MISSING;
	}
	if (!get(bus, PCF8523_REG_TIME, regs, sizeof regs)) {
		return RTC_CHIP_MISSING;
	}
	if (regs[0] & PCF8523_SECONDS_OS || control[0] & PCF8523_CONTROL_1_STOP ||
		!pcf8523_decode(regs, control[0] & PCF8523_CONTROL_1_12_24, t)) {
		return RTC_CHIP_NOT_SET;
	}
	return RTC_CHIP_OK;
}

// 24-hour mode and running, then the time with the OS flag clear: valid
// from now.
static bool pcf8523_set(const rtc_bus_t *bus, const datetime_t *t) {
	if (t->year < 2000 || t->year > 2099) {
		return false;
	}
	uint8_t control, regs[PCF8523_TIME_REGS];
	if (!get(bus, PCF8523_REG_CONTROL_1, &control, 1)) {
		return false;
	}
	uint8_t want = (uint8_t)(control & ~(PCF8523_CONTROL_1_STOP | PCF8523_CONTROL_1_12_24));
	if (want != control && !put(bus, PCF8523_REG_CONTROL_1, want)) {
		return false;
	}
	pcf8523_encode(t, regs);
	return bus->write(bus->ctx, PCF8523_ADDR, PCF8523_REG_TIME, regs, sizeof regs);
}

static bool pcf8523_battery_low(const rtc_bus_t *bus) {
	uint8_t control;
	return get(bus, PCF8523_REG_CONTROL_3, &control, 1) && control & PCF8523_CONTROL_3_BLF;
}

const rtc_chip_t rtc_pcf8523 = {
	.name = "PCF8523",
	.start = pcf8523_start,
	.set = pcf8523_set,
	.battery_low = pcf8523_battery_low,
};
