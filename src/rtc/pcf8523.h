#ifndef PCF8523_H
#define PCF8523_H

// The PCF8523 real-time clock's registers (the Adalogger FeatherWing's);
// its driver is rtc_pcf8523 (rtc_chip.h). Years 2000-2099 (no century
// bit). At power-on its battery switch-over is off (PM = 111): the driver
// turns it on, else the time is lost with the power.

#include <stdbool.h>
#include <stdint.h>

#include "datetime.h"

#define PCF8523_ADDR 0x68
#define PCF8523_REG_CONTROL_1 0x00
#define PCF8523_CONTROL_1_STOP 0x20  // 1: the time is frozen
#define PCF8523_CONTROL_1_12_24 0x08 // 1: 12-hour mode
#define PCF8523_REG_CONTROL_3 0x02
#define PCF8523_CONTROL_3_PM 0xe0  // battery switch-over: 000 standard mode, low detection on
#define PCF8523_CONTROL_3_BLF 0x04 // the battery is low
#define PCF8523_REG_TIME 0x03      // 7 registers: seconds to years
#define PCF8523_TIME_REGS 7
#define PCF8523_SECONDS_OS 0x80 // the oscillator stopped: the time is not valid
#define PCF8523_REG_TMR_CLKOUT 0x0f
#define PCF8523_CLKOUT_COF 0x38 // CLKOUT frequency: 111 = off

// The time registers into *t (twelve_hour: Control_1's 12_24); false if
// they do not hold a valid time. The OS flag is not looked at.
bool pcf8523_decode(const uint8_t regs[PCF8523_TIME_REGS], bool twelve_hour, datetime_t *t);
// *t (year 2000-2099) as the time registers, 24-hour mode, the OS flag
// clear, day of the week 0 = Sunday.
void pcf8523_encode(const datetime_t *t, uint8_t regs[PCF8523_TIME_REGS]);

#endif
