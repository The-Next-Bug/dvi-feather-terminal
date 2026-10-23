#ifndef RTC_H
#define RTC_H

// The wall clock (seconds since 31.12.1977, datetime.h): the board's
// real-time clock chip (BOARD_RTC_DS3231) read at start-up, then kept on
// the chip's 1 Hz output (BOARD_RTC_SQW_PIN: a GPIO interrupt per second,
// wallclock.h), else by the timer. Without a chip, or with its time not
// valid (the oscillator stopped: no battery, never set), it starts at
// 1.1.2026. Setting it writes the chip too. Core 0 only.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	RTC_NONE,    // the board has no RTC
	RTC_MISSING, // the board's RTC did not answer
	RTC_NOT_SET, // it answered, but its time was not valid at start-up
	RTC_OK,      // its time is in use (read at start-up, or set since)
} rtc_state_t;

// After video_init (the I2C clock comes from clk_peri).
void rtc_init(void);
uint32_t rtc_now(void);
// Microseconds into the current second: from the chip's last 1 Hz edge
// while it ticks.
uint32_t rtc_fraction_us(void);
void rtc_set(uint32_t secs);
rtc_state_t rtc_state(void);
// The time is real: the chip's, read at start-up, or set since.
bool rtc_valid(void);
// The chip's 1 Hz output keeps the time (an edge in the last 1.5 s).
bool rtc_ticking(void);

#endif
