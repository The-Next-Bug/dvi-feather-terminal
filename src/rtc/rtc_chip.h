#ifndef RTC_CHIP_H
#define RTC_CHIP_H

// A real-time clock chip's driver: the chip's registers on a bus it does
// not own (the firmware's I2C in rtc.c, a register file in the tests), so
// the drivers are portable. One driver per file (ds3231.c, ...); the board
// header picks the chip at build time (rtc.c).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "datetime.h"

typedef struct {
	// n bytes from register reg on, of the chip at I2C address addr; false
	// if it does not answer.
	bool (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
	// n bytes (at most RTC_BUS_MAX) to register reg on.
	bool (*write)(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n);
	void *ctx;
} rtc_bus_t;

#define RTC_BUS_MAX 16

typedef enum {
	RTC_CHIP_MISSING, // it did not answer
	RTC_CHIP_NOT_SET, // it answered, but its time is not valid
	RTC_CHIP_OK,
} rtc_chip_status_t;

typedef struct {
	const char *name;
	// Sets the chip up at start-up and reads its time into *t (valid with
	// RTC_CHIP_OK only).
	rtc_chip_status_t (*start)(const rtc_bus_t *bus, datetime_t *t);
	// Sets its time (valid from now; the chip's second starts over); false
	// if it did not take it, or *t is outside the years it keeps.
	bool (*set)(const rtc_bus_t *bus, const datetime_t *t);
	// Optional (NULL: the chip has none): its 1 Hz output on, the output
	// falling as the seconds go up; false if it did not answer.
	bool (*start_1hz)(const rtc_bus_t *bus);
} rtc_chip_t;

extern const rtc_chip_t rtc_ds3231;

#endif
