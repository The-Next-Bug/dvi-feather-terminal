#include "rtc.h"

#include <stdbool.h>

#include "hardware/sync.h"
#include "hardware/timer.h"

#include "board.h"
#include "datetime.h"
#include "wallclock.h"

#ifdef BOARD_RTC_DS3231
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "ds3231.h"

#define RTC_I2C_HZ 100000
#define RTC_TIMEOUT_US 5000 // a transfer of a few bytes takes < 1 ms
#endif

// The time; the 1 Hz output's interrupt moves it on, so read and changed
// with interrupts off.
static wallclock_t clock;
static rtc_state_t state = RTC_NONE;
static bool valid, square_wave;

#ifdef BOARD_RTC_DS3231
// Register reg's n bytes into buf; false if the chip does not answer.
static bool ds3231_read(uint8_t reg, uint8_t *buf, size_t n) {
	return i2c_write_timeout_us(BOARD_RTC_I2C_INST, DS3231_ADDR, &reg, 1, true, RTC_TIMEOUT_US) == 1 &&
		i2c_read_timeout_us(BOARD_RTC_I2C_INST, DS3231_ADDR, buf, n, false, RTC_TIMEOUT_US) == (int)n;
}

// buf[0] the first register, then its bytes.
static bool ds3231_write(const uint8_t *buf, size_t n) {
	return i2c_write_timeout_us(BOARD_RTC_I2C_INST, DS3231_ADDR, buf, n, false, RTC_TIMEOUT_US) == (int)n;
}

#ifdef BOARD_RTC_SQW_PIN
// The square wave falls as the chip's seconds go up (DS3231: high 500 ms
// after a seconds write, low at the next second).
static void sqw_irq(void) {
	if (gpio_get_irq_event_mask(BOARD_RTC_SQW_PIN) & GPIO_IRQ_EDGE_FALL) {
		gpio_acknowledge_irq(BOARD_RTC_SQW_PIN, GPIO_IRQ_EDGE_FALL);
		wallclock_edge(&clock, time_us_64());
	}
}

// INT/SQW as the 1 Hz square wave (the reset state is the alarms'
// interrupt, idle high), the oscillator kept running on the battery; the
// pin's falling edges counted.
static void sqw_start(void) {
	uint8_t control;
	if (!ds3231_read(DS3231_REG_CONTROL, &control, 1)) {
		return;
	}
	uint8_t set[2] = {DS3231_REG_CONTROL,
		(uint8_t)(control & ~(DS3231_CONTROL_EOSC | DS3231_CONTROL_RS | DS3231_CONTROL_INTCN))};
	if (!ds3231_write(set, sizeof set)) {
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
#ifdef BOARD_RTC_DS3231
	i2c_init(BOARD_RTC_I2C_INST, RTC_I2C_HZ);
	gpio_set_function(BOARD_RTC_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(BOARD_RTC_SCL_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(BOARD_RTC_SDA_PIN);
	gpio_pull_up(BOARD_RTC_SCL_PIN);
	uint8_t status, regs[DS3231_TIME_REGS];
	datetime_t t;
	uint32_t secs;
	if (!ds3231_read(DS3231_REG_STATUS, &status, 1) || !ds3231_read(DS3231_REG_TIME, regs, sizeof regs)) {
		state = RTC_MISSING;
		return;
	}
	if (status & DS3231_STATUS_OSF || !ds3231_decode(regs, &t) || !datetime_to_secs(&t, &secs)) {
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
#ifdef BOARD_RTC_DS3231
	datetime_t t;
	datetime_from_secs(secs, &t);
	if (state != RTC_NONE && state != RTC_MISSING && t.year >= 2000 && t.year <= 2199) {
		uint8_t buf[1 + DS3231_TIME_REGS] = {DS3231_REG_TIME}, status;
		ds3231_encode(&t, buf + 1);
		// The time, then the oscillator-stopped flag cleared: valid from
		// now. The write starts the chip's second over.
		if (ds3231_write(buf, sizeof buf) && ds3231_read(DS3231_REG_STATUS, &status, 1)) {
			uint8_t clear[2] = {DS3231_REG_STATUS, (uint8_t)(status & ~DS3231_STATUS_OSF)};
			if (ds3231_write(clear, sizeof clear)) {
				state = RTC_OK;
			}
		}
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
