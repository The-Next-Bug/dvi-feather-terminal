#ifndef LED_H
#define LED_H

// The board's LED (PICO_DEFAULT_LED_PIN, when the board header names one):
// off while the screen is on, flashing while the SD card, a USB stick or
// the serial line is busy; with the screen blanked, a short flash every
// second from the main loop besides, so a hung board shows as a light that
// stays on or stays off. Core 0 only. No-ops without an LED and in
// VIDEO_DIAG builds (diag.c has the LED then).

#include <stdbool.h>

// Set the LED up, off. Call once the firmware is up (after the clock change).
void led_init(void);

// Something happened: flash (on, then off, each for at least 40 ms).
void led_activity(void);

// From the main loop: back to rest once a flash is over, and the heartbeat
// while the screen is blanked.
void led_poll(bool blanked);

#endif
