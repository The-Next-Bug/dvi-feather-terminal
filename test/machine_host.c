// The Z80 machine on the host for the tests (machine_host.h).

#include "machine_host.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "datetime.h"
#include "drives.h"
#include "xfer.h"

char screen[HOST_SCREEN_SIZE];
size_t screen_len;
bool host_echo;
const char *host_cpr_reply;
unsigned host_hwinfo_extra;
void (*host_console_hook)(uint8_t c);
char aux_sent[256];
size_t aux_len;

uint64_t host_clock_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void idle(void) {
}

static void console_out(uint8_t c) {
	if (host_console_hook) {
		host_console_hook(c);
	}
	if (c == 0) { // padding (CPUTEST sends it), as a terminal ignores it
		return;
	}
	if (host_echo) {
		putchar(c);
		if (c == '\n') {
			fflush(stdout);
		}
	}
	if (screen_len == sizeof screen - 1) { // long runs: keep the newer half
		memmove(screen, screen + sizeof screen / 2, sizeof screen / 2);
		screen_len -= sizeof screen / 2;
	}
	screen[screen_len++] = (char)c;
	screen[screen_len] = '\0';
	if (host_cpr_reply && c == 'n' && screen_len >= 4 && memcmp(screen + screen_len - 4, "\x1b[6n", 4) == 0) {
		for (const char *r = host_cpr_reply; *r; ++r) {
			machine_key((uint8_t)*r);
		}
	}
}

static void aux_out(uint8_t c) {
	if (aux_len < sizeof aux_sent) {
		aux_sent[aux_len++] = (char)c;
	}
}

static bool aux_in(uint8_t *c) {
	(void)c;
	return false;
}

static uint8_t disk_read(unsigned drive, unsigned track, unsigned sector, uint8_t *buf) {
	return drives_read(drive, track, sector, buf);
}

static uint8_t disk_write(unsigned drive, unsigned track, unsigned sector, const uint8_t *buf) {
	return drives_write(drive, track, sector, buf);
}

static uint8_t disk_format(unsigned drive) {
	return (uint8_t)drives_format(drive);
}

static uint8_t disk_mounts(unsigned drive) {
	return drives_mounts(drive);
}

// Port 12's items (machine.h) as a board might have them: one of each
// status, a detail that wraps, and a record at a number HWINFO does not
// know of (as newer firmware could add).
static size_t hwinfo(uint8_t item, uint8_t *buf) {
	switch (item) {
	case HWINFO_FIRMWARE:
		return hwinfo_text(buf, "Host test firmware");
	case HWINFO_BOARD:
		return hwinfo_text(buf, "host-board");
	case HWINFO_CPU:
		return hwinfo_record(buf, HWINFO_ST_PRESENT, "CPU", "RP2350A at 400 MHz, 4 MB flash");
	case HWINFO_PSRAM:
		return hwinfo_record(buf, HWINFO_ST_NOT_FOUND, "PSRAM", "none answered on CS1 (GP8)");
	case HWINFO_SD_CARD:
		return hwinfo_record(buf, HWINFO_ST_PRESENT, "SD card",
			"SDHC/SDXC, 29.7 GB; SPI0: GP4 MISO, GP5 CS, GP6 SCK, GP7 MOSI");
	case HWINFO_USB_STICK:
		return hwinfo_record(buf, HWINFO_ST_ABSENT, "USB stick", "none");
	case HWINFO_RTC:
		return hwinfo_record(buf, HWINFO_ST_PRESENT, "Real-time clock", "DS3231 on I2C1: GP10 SDA, GP11 SCL");
	case HWINFO_NEOPIXEL:
		return hwinfo_record(buf, HWINFO_ST_PLANNED, "RGB status LED", "GP21: not supported yet");
	case 200:
		return hwinfo_record(buf, 9, "Future thing", "a status HWINFO does not know");
	default:
		if (item >= 40 && item < 40 + host_hwinfo_extra) {
			char name[16];
			snprintf(name, sizeof name, "Extra %u", item - 40);
			return hwinfo_record(buf, HWINFO_ST_PRESENT, name, "filler");
		}
		return 0;
	}
}

// The wall clock: the time since the host's start plus an offset in
// microseconds, so that a set zeroes the seconds' fraction as a real clock
// chip's write does.
uint64_t host_time_offset_us = (uint64_t)DATETIME_DEFAULT_DAYS * DATETIME_DAY * 1000000u;
unsigned host_time_sets;

static uint32_t time_get(void) {
	return (uint32_t)((host_time_offset_us + host_clock_us()) / 1000000u);
}

static void time_set(uint32_t secs) {
	host_time_offset_us = (uint64_t)secs * 1000000u - host_clock_us();
	++host_time_sets;
}

const machine_platform_t host_platform = {
	.clock_us = host_clock_us,
	.idle = idle,
	.console_out = console_out,
	.aux_out = aux_out,
	.aux_in = aux_in,
	.disk_read = disk_read,
	.disk_write = disk_write,
	.disk_format = disk_format,
	.disk_mounts = disk_mounts,
	.xfer = xfer_command,
	.hwinfo = hwinfo,
	.time_get = time_get,
	.time_set = time_set,
};

void clear_screen(void) {
	screen_len = 0;
	screen[0] = '\0';
}

// The callers' budgets are wall-clock seconds for an idle host; on a loaded
// one (ctest beside a parallel build) the Z80 gets less of it. A run that
// gets there returns at once, so the slack costs only a failing test's time.
#define RUN_UNTIL_SLACK 4

machine_state_t run_until_any(const char *const texts[], double seconds, int *which) {
	size_t from = screen_len;
	uint64_t end = host_clock_us() + (uint64_t)(seconds * RUN_UNTIL_SLACK * 1e6);
	while (host_clock_us() < end) {
		machine_state_t st = machine_run(10000);
		if (screen_len < from) { // the older half was dropped
			from = 0;
		}
		for (int i = 0; texts[i]; ++i) {
			if (strstr(screen + from, texts[i])) {
				*which = i;
				return MACHINE_RUNNING;
			}
		}
		if (st == MACHINE_STOPPED) {
			return st;
		}
	}
	fprintf(stderr, "timed out waiting for \"%s\"; console:\n%s\n", texts[0], screen);
	return MACHINE_STOPPED;
}

machine_state_t run_until(const char *text, double seconds) {
	const char *const texts[] = {text, NULL};
	int which;
	return run_until_any(texts, seconds, &which);
}

bool type(const char *s) {
	bool ok = true;
	while (*s) {
		ok = machine_key((uint8_t)*s++) && ok;
	}
	return ok;
}
