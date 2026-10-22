#ifndef DS3231_H
#define DS3231_H

// The DS3231 (and DS3231M) real-time clock's registers, as plain bytes:
// the I2C transfers are the platform's (src/platform/rtc.c). The time is
// kept in 24-hour form; years 2000-2199 (the century bit).

#include <stdbool.h>
#include <stdint.h>

#include "datetime.h"

#define DS3231_ADDR 0x68
#define DS3231_REG_TIME 0x00   // 7 registers: seconds to year
#define DS3231_TIME_REGS 7
#define DS3231_REG_STATUS 0x0f
#define DS3231_STATUS_OSF 0x80 // the oscillator stopped: the time is not valid

// The time registers into *t; false if they do not hold a valid time (or
// one before 1978: datetime.h's range).
bool ds3231_decode(const uint8_t regs[DS3231_TIME_REGS], datetime_t *t);
// *t (year 2000-2199) as the time registers, 24-hour mode, day of the week
// 1 = Sunday.
void ds3231_encode(const datetime_t *t, uint8_t regs[DS3231_TIME_REGS]);

#endif
