#include "rtc.h"

#include <stdbool.h>

#include "hardware/timer.h"

#include "board.h"
#include "datetime.h"

#ifdef BOARD_RTC_DS3231
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "ds3231.h"

#define RTC_I2C_HZ 100000
#define RTC_TIMEOUT_US 5000 // a transfer of a few bytes takes < 1 ms
#endif

static uint32_t base_secs = DATETIME_DEFAULT_DAYS * DATETIME_DAY; // at base_us
static uint64_t base_us;
static rtc_state_t state = RTC_NONE;

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
#endif

void rtc_init(void) {
	base_us = time_us_64();
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
	} else if (status & DS3231_STATUS_OSF || !ds3231_decode(regs, &t) || !datetime_to_secs(&t, &secs)) {
		state = RTC_NOT_SET;
	} else {
		base_us = time_us_64();
		base_secs = secs;
		state = RTC_OK;
	}
#endif
}

uint32_t rtc_now(void) {
	return base_secs + (uint32_t)((time_us_64() - base_us) / 1000000u);
}

void rtc_set(uint32_t secs) {
	base_us = time_us_64();
	base_secs = secs;
#ifdef BOARD_RTC_DS3231
	datetime_t t;
	datetime_from_secs(secs, &t);
	if (state == RTC_NONE || state == RTC_MISSING || t.year < 2000 || t.year > 2199) {
		return; // kept until power-off only
	}
	uint8_t buf[1 + DS3231_TIME_REGS] = {DS3231_REG_TIME}, status;
	ds3231_encode(&t, buf + 1);
	// The time, then the oscillator-stopped flag cleared: valid from now.
	if (ds3231_write(buf, sizeof buf) && ds3231_read(DS3231_REG_STATUS, &status, 1)) {
		uint8_t clear[2] = {DS3231_REG_STATUS, (uint8_t)(status & ~DS3231_STATUS_OSF)};
		if (ds3231_write(clear, sizeof clear)) {
			state = RTC_OK;
		}
	}
#endif
}

rtc_state_t rtc_state(void) {
	return state;
}
