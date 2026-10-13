#include "led.h"

#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "board.h"
#include "diag.h"

#if defined(PICO_DEFAULT_LED_PIN) && !VIDEO_DIAG

// Each flash is on this long, then off at least as long: continuous
// activity shows as a steady ~12 Hz flicker, a single sector as one flash.
#define LED_FLASH_US 40000u
// With the screen blanked: on for the first LED_BEAT_ON_US of each second.
#define LED_BEAT_PERIOD_US 1000000u
#define LED_BEAT_ON_US 100000u

static bool led_on;
static uint32_t led_changed_us;

static void led_set(bool on, uint32_t now) {
	gpio_put(PICO_DEFAULT_LED_PIN, on);
	led_on = on;
	led_changed_us = now;
}

void led_init(void) {
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	led_set(false, time_us_32());
}

// Also turns the LED back off, so it keeps flashing through long work (a
// disk install, a firmware update) that does not return to the main loop.
void led_activity(void) {
	uint32_t now = time_us_32();
	if (now - led_changed_us >= LED_FLASH_US)
		led_set(!led_on, now);
}

void led_poll(bool blanked) {
	uint32_t now = time_us_32();
	bool rest = blanked && now % LED_BEAT_PERIOD_US < LED_BEAT_ON_US;
	if (led_on != rest && now - led_changed_us >= LED_FLASH_US)
		led_set(rest, now);
}

#else

void led_init(void) {
}

void led_activity(void) {
}

void led_poll(bool blanked) {
	(void)blanked;
}

#endif
