// Keys from the USB keyboard (terminal.h): usbkbd.c calls these from
// inside the USB stack. They go to the host, SET-UP or the Z80 machine's
// queue; keyboard_poll gives the keyboard the modes that change what keys
// send.

#include <string.h>

#include "pico/stdlib.h"

#include "serial.h"
#include "terminal.h"
#include "usbkbd.h"

last_key_t last_key;

// A key while the screen is blanked only wakes it (it is dropped, not
// typed blind); any other key restarts the blanking delay.
static bool key_wakes(void) {
	return term_started && blank_activity(&blank, to_ms_since_boot(get_absolute_time()));
}

// Keystrokes go to the host like replies, through the TX queue (or to
// SET-UP while it is open). With local echo they are shown too.
void send_key(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	key_pressed = true;
	if (key_wakes()) {
		return;
	}
	if (setup.active) {
		setup_feed(data, len);
		return;
	}
	if (cpm.on) {
		for (size_t i = 0; i < len && cpm.key_head - cpm.key_tail < CPM_KEY_QUEUE; ++i) {
			cpm.keys[cpm.key_head++ % CPM_KEY_QUEUE] = data[i];
		}
	} else {
		serial_write(data, len);
	}
	last_key.len = (uint8_t)(len < sizeof last_key.data ? len : sizeof last_key.data);
	memcpy(last_key.data, data, last_key.len);
	++last_key.count;
	if (term_started && settings.local_echo && !cpm.on) {
		term_write(&term, data, len);
	}
}

// The SET-UP key opens SET-UP, or leaves it without saving.
void setup_key(void *ctx) {
	(void)ctx;
	key_pressed = true;
	if (!term_started || key_wakes()) {
		return;
	}
	if (setup.demo) {
		setup.demo_end = true; // back to SET-UP, like any key
	} else if (setup.active) {
		setup.action = SETUP_CANCEL;
	} else {
		setup.open_requested = true;
	}
}

// F11 in CP/M mode: the Z80 machine's reset button (cpm_step acts on it).
void reset_key(void *ctx) {
	(void)ctx;
	key_pressed = true;
	if (term_started && !key_wakes() && !setup.active) {
		cpm.reset_requested = true;
	}
}

// The terminal modes that change what keys send (none before it starts).
// SET-UP and the CP/M monitor read keys as a VT100 in ANSI mode sends them
// (ESC [ A ...), whatever the host has set: only the host's program gets
// its own modes.
void keyboard_poll(void) {
	crash_heartbeat(); // every loop core 0 waits in comes here
	kbd_modes_t modes = {.backspace_bs = settings.backspace == 0x08};
	if (term_started) {
		modes.newline_mode = term.newline_mode;
		modes.backspace_bs = term.backspace_bs;
		if (setup.active || (cpm.on && !cpm.running)) {
			// VT100 keys for SET-UP or the monitor
		} else if (term.personality == TERM_ADM3A) {
			modes.adm3a = true;
		} else {
			modes.cursor_keys_app = term.vt100.cursor_keys_app;
			modes.keypad_app = term.vt100.keypad_app;
			modes.vt52 = term.vt100.vt52;
		}
	}
	usbkbd_poll(&modes);
}
