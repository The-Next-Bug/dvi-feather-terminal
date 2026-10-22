#ifndef DATETIME_H
#define DATETIME_H

// Wall-clock time as seconds since 31.12.1977 00:00, CP/M 3's day 0: secs /
// DATETIME_DAY is CP/M's day number (1 = 1.1.1978), through 2114. Local
// time, no time zones (CP/M and FAT have none). Without the C library's
// localtime/mktime, which cost ~20 KB of SRAM in the firmware.

#include <stdbool.h>
#include <stdint.h>

#define DATETIME_DAY 86400u
// The clock's date when nothing better is known (no RTC, or its time not
// valid): 1.1.2026, CP/M day 17533.
#define DATETIME_DEFAULT_DAYS 17533u

typedef struct {
	uint16_t year;  // 1978-2114
	uint8_t month;  // 1-12
	uint8_t mday;   // 1-31
	uint8_t hour;   // 0-23
	uint8_t min;    // 0-59
	uint8_t sec;    // 0-59
	uint8_t wday;   // 0 Sunday - 6 Saturday
} datetime_t;

void datetime_from_secs(uint32_t secs, datetime_t *t);
// false (and *secs unchanged) for a date before 1.1.1978 or past the
// count's end, or one that does not exist (31.4., 29.2.2027, 24:00); the
// weekday is not read.
bool datetime_to_secs(const datetime_t *t, uint32_t *secs);
// As a FAT timestamp (FatFs's get_fattime: the year - 1980, month, day,
// hours, minutes, seconds / 2, from bit 25 down), held to 1980-2107.
uint32_t datetime_to_fat(uint32_t secs);

#endif
