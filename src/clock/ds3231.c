#include "ds3231.h"

static bool from_bcd(uint8_t b, unsigned max, uint8_t *v) {
	if ((b & 0x0f) > 9 || (b >> 4) > 9 || (b >> 4) * 10u + (b & 0x0f) > max) {
		return false;
	}
	*v = (uint8_t)((b >> 4) * 10 + (b & 0x0f));
	return true;
}

static uint8_t to_bcd(unsigned v) {
	return (uint8_t)((v / 10 % 10) << 4 | v % 10);
}

bool ds3231_decode(const uint8_t regs[DS3231_TIME_REGS], datetime_t *t) {
	datetime_t d = {0};
	uint8_t hour = regs[2], year;
	if (hour & 0x40) { // 12-hour mode: bit 5 PM, hours 1-12
		uint8_t h;
		if (!from_bcd(hour & 0x1f, 12, &h) || h == 0) {
			return false;
		}
		d.hour = (uint8_t)(h % 12 + (hour & 0x20 ? 12 : 0));
	} else if (!from_bcd(hour & 0x3f, 23, &d.hour)) {
		return false;
	}
	if (!from_bcd(regs[0] & 0x7f, 59, &d.sec) || !from_bcd(regs[1] & 0x7f, 59, &d.min) ||
		!from_bcd(regs[4] & 0x3f, 31, &d.mday) || !from_bcd(regs[5] & 0x1f, 12, &d.month) ||
		!from_bcd(regs[6], 99, &year)) {
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
	regs[0] = to_bcd(t->sec);
	regs[1] = to_bcd(t->min);
	regs[2] = to_bcd(t->hour);
	regs[3] = (uint8_t)(t->wday + 1);
	regs[4] = to_bcd(t->mday);
	regs[5] = (uint8_t)(to_bcd(t->month) | (year >= 100 ? 0x80 : 0));
	regs[6] = to_bcd(year % 100);
}
