#include "datetime.h"

#include <stdio.h>

// Day counts from 1.3.0000 (Howard Hinnant's civil_from_days and
// days_from_civil, unsigned): 1.1.1978 is day 722390 of that count, CP/M's
// day 1.
#define CPM_DAY0 722389u

void datetime_from_secs(uint32_t secs, datetime_t *t) {
	uint32_t days = secs / DATETIME_DAY, day_secs = secs % DATETIME_DAY;
	uint32_t z = days + CPM_DAY0, era = z / 146097, doe = z - era * 146097;
	uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	uint32_t month = mp < 10 ? mp + 3 : mp - 9;
	t->mday = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
	t->month = (uint8_t)month;
	t->year = (uint16_t)(yoe + era * 400 + (month <= 2));
	t->hour = (uint8_t)(day_secs / 3600);
	t->min = (uint8_t)(day_secs / 60 % 60);
	t->sec = (uint8_t)(day_secs % 60);
	t->wday = (uint8_t)((days + 6) % 7); // day 1, 1.1.1978, was a Sunday
}

static unsigned month_days(unsigned year, unsigned month) {
	static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	return days[month - 1] + (month == 2 && leap);
}

bool datetime_to_secs(const datetime_t *t, uint32_t *secs) {
	if (t->year < 1978 || t->month < 1 || t->month > 12 || t->mday < 1 ||
		t->mday > month_days(t->year, t->month) || t->hour > 23 || t->min > 59 || t->sec > 59) {
		return false;
	}
	uint32_t y = t->year - (t->month <= 2), era = y / 400, yoe = y - era * 400;
	uint32_t mp = t->month > 2 ? t->month - 3u : t->month + 9u;
	uint32_t doy = (153 * mp + 2) / 5 + t->mday - 1;
	uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	uint64_t days = (uint64_t)era * 146097 + doe - CPM_DAY0;
	uint64_t s = days * DATETIME_DAY + t->hour * 3600u + t->min * 60u + t->sec;
	if (s > UINT32_MAX) {
		return false;
	}
	*secs = (uint32_t)s;
	return true;
}

uint32_t datetime_to_fat(uint32_t secs) {
	datetime_t t;
	datetime_from_secs(secs, &t);
	if (t.year < 1980) {
		return 1u << 21 | 1u << 16; // 1.1.1980 00:00
	}
	if (t.year > 2107) {
		t = (datetime_t){2107, 12, 31, 23, 59, 59, 0};
	}
	return (uint32_t)(t.year - 1980) << 25 | (uint32_t)t.month << 21 | (uint32_t)t.mday << 16 |
		(uint32_t)t.hour << 11 | (uint32_t)t.min << 5 | t.sec / 2u;
}

unsigned datetime_clock_text(char out[DATETIME_CLOCK_MAX + 1], uint32_t secs, bool colons,
	const datetime_clock_t *format) {
	static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
		"Nov", "Dec"};
	datetime_t t;
	datetime_from_secs(secs, &t);
	unsigned n = 0;
	if (format->date == DATETIME_DATE_ISO) {
		n = (unsigned)snprintf(out, DATETIME_CLOCK_MAX + 1, "%04u-%02u-%02u ", t.year % 10000u, t.month % 100u,
			t.mday % 100u);
	} else if (format->date == DATETIME_DATE_SHORT) {
		n = (unsigned)snprintf(out, DATETIME_CLOCK_MAX + 1, "%s %02u %s ", days[t.wday % 7], t.mday % 100u,
			months[(t.month + 11u) % 12]);
	}
	char colon = colons ? ':' : ' ';
	if (format->twelve_hour) { // 12 AM, 1 AM ... 12 PM, 1 PM ...
		n += (unsigned)snprintf(out + n, DATETIME_CLOCK_MAX + 1 - n, "%2u", t.hour % 12 ? t.hour % 12u : 12u);
	} else {
		n += (unsigned)snprintf(out + n, DATETIME_CLOCK_MAX + 1 - n, "%02u", t.hour % 100u);
	}
	n += (unsigned)snprintf(out + n, DATETIME_CLOCK_MAX + 1 - n, "%c%02u", colon, t.min % 100u);
	if (format->seconds) {
		n += (unsigned)snprintf(out + n, DATETIME_CLOCK_MAX + 1 - n, "%c%02u", colon, t.sec % 100u);
	}
	if (format->twelve_hour) {
		n += (unsigned)snprintf(out + n, DATETIME_CLOCK_MAX + 1 - n, " %s", t.hour < 12 ? "AM" : "PM");
	}
	return n;
}
