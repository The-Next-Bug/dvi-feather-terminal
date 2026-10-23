#include "rtc.h"

#include <stdbool.h>
#include <string.h>

#include "hardware/sync.h"
#include "hardware/timer.h"

#include "board.h"
#include "datetime.h"
#include "rtc_chip.h"
#include "wallclock.h"

#if defined(BOARD_RTC_DS3231)
#define RTC_CHIP rtc_ds3231
#elif defined(BOARD_RTC_PCF8523)
#define RTC_CHIP rtc_pcf8523
#endif

#ifdef RTC_CHIP
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define RTC_I2C_HZ 100000
#define RTC_TIMEOUT_US 5000 // a transfer of a few bytes takes < 1 ms
#endif

// The time; the 1 Hz output's interrupt moves it on, so read and changed
// with interrupts off.
static wallclock_t clock;
static rtc_state_t state = RTC_NONE;
static bool valid, square_wave;

#ifdef RTC_CHIP
static bool i2c_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
	(void)ctx;
	return i2c_write_timeout_us(BOARD_RTC_I2C_INST, addr, &reg, 1, true, RTC_TIMEOUT_US) == 1 &&
		i2c_read_timeout_us(BOARD_RTC_I2C_INST, addr, buf, n, false, RTC_TIMEOUT_US) == (int)n;
}

static bool i2c_write(void *ctx, uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n) {
	(void)ctx;
	uint8_t msg[1 + RTC_BUS_MAX];
	if (n > RTC_BUS_MAX) {
		return false;
	}
	msg[0] = reg;
	memcpy(msg + 1, buf, n);
	return i2c_write_timeout_us(BOARD_RTC_I2C_INST, addr, msg, 1 + n, false, RTC_TIMEOUT_US) == (int)(1 + n);
}

static const rtc_bus_t bus = {i2c_read, i2c_write, NULL};

#ifdef BOARD_RTC_SQW_PIN
static void sqw_irq(void) {
	if (gpio_get_irq_event_mask(BOARD_RTC_SQW_PIN) & GPIO_IRQ_EDGE_FALL) {
		gpio_acknowledge_irq(BOARD_RTC_SQW_PIN, GPIO_IRQ_EDGE_FALL);
		wallclock_edge(&clock, time_us_64());
	}
}

// The chip's 1 Hz output on, its falling edges counted.
static void sqw_start(void) {
	if (!RTC_CHIP.start_1hz || !RTC_CHIP.start_1hz(&bus)) {
		return;
	}
	gpio_init(BOARD_RTC_SQW_PIN);
	gpio_pull_up(BOARD_RTC_SQW_PIN); // open drain; the board's pull-up too
	gpio_add_raw_irq_handler(BOARD_RTC_SQW_PIN, sqw_irq);
	gpio_set_irq_enabled(BOARD_RTC_SQW_PIN, GPIO_IRQ_EDGE_FALL, true);
	irq_set_enabled(IO_IRQ_BANK0, true);
	square_wave = true;
}
#endif
#endif

void rtc_init(void) {
	wallclock_start(&clock, DATETIME_DEFAULT_DAYS * DATETIME_DAY, time_us_64(), false);
#ifdef RTC_CHIP
	i2c_init(BOARD_RTC_I2C_INST, RTC_I2C_HZ);
	gpio_set_function(BOARD_RTC_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(BOARD_RTC_SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(BOARD_RTC_SDA_PIN);
	gpio_pull_up(BOARD_RTC_SCL_PIN);
	datetime_t t;
	uint32_t secs;
	rtc_chip_status_t st = RTC_CHIP.start(&bus, &t);
	if (st == RTC_CHIP_MISSING) {
		state = RTC_MISSING;
		return;
	}
	if (st != RTC_CHIP_OK || !datetime_to_secs(&t, &secs)) {
		state = RTC_NOT_SET;
	} else {
		// Somewhere within that second: the first edge says where.
		wallclock_start(&clock, secs, time_us_64(), false);
		state = RTC_OK;
		valid = true;
	}
#ifdef BOARD_RTC_SQW_PIN
	sqw_start();
#endif
#endif
}

uint32_t rtc_now(void) {
	uint32_t irq = save_and_disable_interrupts();
	uint32_t secs = wallclock_now(&clock, time_us_64());
	restore_interrupts(irq);
	return secs;
}

uint32_t rtc_fraction_us(void) {
	uint32_t irq = save_and_disable_interrupts();
	uint32_t us = wallclock_fraction(&clock, time_us_64());
	restore_interrupts(irq);
	return us;
}

void rtc_set(uint32_t secs) {
	valid = true;
#ifdef RTC_CHIP
	datetime_t t;
	datetime_from_secs(secs, &t);
	if (state != RTC_NONE && state != RTC_MISSING && RTC_CHIP.set(&bus, &t)) {
		state = RTC_OK;
	}
#endif
	// Without a chip (or with one that did not take it) kept until
	// power-off only.
	uint32_t irq = save_and_disable_interrupts();
	wallclock_start(&clock, secs, time_us_64(), true);
	restore_interrupts(irq);
}

rtc_state_t rtc_state(void) {
	return state;
}

const char *rtc_chip_name(void) {
#ifdef RTC_CHIP
	return RTC_CHIP.name;
#else
	return NULL;
#endif
}

bool rtc_battery_low(void) {
#ifdef RTC_CHIP
	return state != RTC_NONE && state != RTC_MISSING && RTC_CHIP.battery_low && RTC_CHIP.battery_low(&bus);
#else
	return false;
#endif
}

bool rtc_valid(void) {
	return valid;
}

bool rtc_ticking(void) {
	if (!square_wave) {
		return false;
	}
	uint32_t irq = save_and_disable_interrupts();
	bool ticking = wallclock_ticking(&clock, time_us_64());
	restore_interrupts(irq);
	return ticking;
}
