// CP/M mode (terminal.h): the screen and keyboard are the console of the
// Z80 machine (machine.c), run in slices from the main loop (cpm_step);
// the machine's platform and the monitor's.

#include <stdio.h>

#include "pico/stdlib.h"

#include "board.h"
#include "led.h"
#include "sdcard.h"
#include "serial.h"
#include "terminal.h"
#include "usbmsc.h"
#include "video.h"
#include "xfer.h"

#define CPU_SLICE_US 10000 // the Z80 runs this long between the other work

cpm_state_t cpm;

static uint64_t cpm_clock_us(void) {
	return time_us_64();
}

static void cpm_console_out(uint8_t c) {
	term_write(&term, &c, 1);
}

static void cpm_aux_out(uint8_t c) {
	serial_write(&c, 1);
}

static bool cpm_aux_in(uint8_t *c) {
	return serial_read(c, 1) == 1;
}

static uint8_t cpm_disk_read(unsigned drive, unsigned track, unsigned sector, uint8_t *buf) {
	return drives_read(drive, track, sector, buf);
}

static uint8_t cpm_disk_write(unsigned drive, unsigned track, unsigned sector, const uint8_t *buf) {
	return drives_write(drive, track, sector, buf);
}

// Queued keys to the CPU while it runs (as many as it has room for), else
// to the monitor.
static void cpm_feed_keys(uint32_t now_ms) {
	while (cpm.key_tail != cpm.key_head) {
		uint8_t c = cpm.keys[cpm.key_tail % CPM_KEY_QUEUE];
		if (cpm.running) {
			if (!machine_key(c))
				return;
		} else if (monitor_key(c, now_ms) == MONITOR_RUN) {
			cpm.running = true;
		}
		++cpm.key_tail;
	}
}

// The CPU waits (HALT, the speed limit): keep the keyboard, UART0 and the
// screen going.
static void cpm_idle(void) {
	keyboard_poll();
	serial_poll();
	led_poll(screen_blanked);
	if (cpm.running)
		cpm_feed_keys(to_ms_since_boot(get_absolute_time()));
	video_update(&screen);
}

static uint8_t cpm_disk_format(unsigned drive) {
	return (uint8_t)drives_format(drive);
}

static uint8_t cpm_disk_mounts(unsigned drive) {
	return drives_mounts(drive);
}

const machine_platform_t cpm_platform = {
	.clock_us = cpm_clock_us,
	.idle = cpm_idle,
	.console_out = cpm_console_out,
	.aux_out = cpm_aux_out,
	.aux_in = cpm_aux_in,
	.disk_read = cpm_disk_read,
	.disk_write = cpm_disk_write,
	.disk_format = cpm_disk_format,
	.disk_mounts = cpm_disk_mounts,
	.xfer = xfer_command,
};

static void cpm_write(const char *text) {
	term_puts(&term, text);
}

static const char *cpm_card_info(void) {
	static char text[32];
	sdcard_type_t type = sdcard_type();
	if (type == SDCARD_NONE)
		return NULL;
	uint32_t mb = sdcard_blocks() / 2048;
	snprintf(text, sizeof text, "%s, %lu.%lu GB", type == SDCARD_SDHC ? "SDHC/SDXC" : "SDSC",
		(unsigned long)(mb / 1024), (unsigned long)(mb % 1024 * 10 / 1024));
	return text;
}

static void cpm_flush(void) {
	video_update(&screen);
}

const monitor_platform_t cpm_monitor = {
	.write = cpm_write,
	.card_info = cpm_card_info,
	.card_pins = BOARD_SD_PINS_TEXT,
	.flush = cpm_flush,
	.usb_info = usbmsc_info,
};

// F11: reset the machine and boot from drive A, as the reset button of a
// real one would; with no disk in A, the monitor instead. The terminal is
// reset too (colours, modes, margins, a stuck escape sequence) and goes back
// to the SET-UP width, so a program that left it unreadable cannot keep it so.
static void cpm_reset(void) {
	cpm.reset_requested = false;
	cpm.key_tail = cpm.key_head; // typed-ahead keys were for the old run
	drives_flush();
	term_reset(&term);
	if (screen.cols != default_columns)
		term_set_columns(&term, default_columns); // the status line follows
	if (!drives_name(0)) {
		cpm.running = false;
		monitor_enter("Reset: no disk in drive A", false);
		return;
	}
	term_puts(&term, "\r\n*** Reset\r\n");
	machine_reset();
	cpm.running = true;
}

// Run the CPU for a slice, or the monitor's countdown.
void cpm_step(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if (cpm.reset_requested)
		cpm_reset();
	cpm_feed_keys(now);
	if (!setup.active) {
		if (cpm.running) {
			if (machine_run(CPU_SLICE_US) == MACHINE_STOPPED) {
				cpm.running = false;
				monitor_enter(machine_stop_reason(), false);
			}
		} else if (monitor_tick(now) == MONITOR_RUN) {
			cpm.running = true;
		}
	}
	drives_poll(now);
}
