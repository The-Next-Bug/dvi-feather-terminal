#ifndef RTC_INTERNAL_H
#define RTC_INTERNAL_H

// What the chip drivers share: BCD registers.

#include <stdbool.h>
#include <stdint.h>

// BCD byte b into *v; false if it is not BCD or above max.
static inline bool rtc_from_bcd(uint8_t b, unsigned max, uint8_t *v) {
	if ((b & 0x0f) > 9 || (b >> 4) > 9 || (b >> 4) * 10u + (b & 0x0f) > max) {
		return false;
	}
	*v = (uint8_t)((b >> 4) * 10 + (b & 0x0f));
	return true;
}

// v (0-99) as BCD.
static inline uint8_t rtc_to_bcd(unsigned v) {
	return (uint8_t)((v / 10 % 10) << 4 | v % 10);
}

#endif
