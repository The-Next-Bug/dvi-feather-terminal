// The Z80 machine on the host for the tests (machine_host.h).

#include "machine_host.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "drives.h"

char screen[HOST_SCREEN_SIZE];
size_t screen_len;
bool host_echo;
const char *host_cpr_reply;
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
	if (c == 0) // padding (CPUTEST sends it), as a terminal ignores it
		return;
	if (host_echo) {
		putchar(c);
		if (c == '\n')
			fflush(stdout);
	}
	if (screen_len == sizeof screen - 1) { // long runs: keep the newer half
		memmove(screen, screen + sizeof screen / 2, sizeof screen / 2);
		screen_len -= sizeof screen / 2;
	}
	screen[screen_len++] = (char)c;
	screen[screen_len] = '\0';
	if (host_cpr_reply && c == 'n' && screen_len >= 4 && memcmp(screen + screen_len - 4, "\x1b[6n", 4) == 0)
		for (const char *r = host_cpr_reply; *r; ++r)
			machine_key((uint8_t)*r);
}

static void aux_out(uint8_t c) {
	if (aux_len < sizeof aux_sent)
		aux_sent[aux_len++] = (char)c;
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

const machine_platform_t host_platform = {
	.clock_us = host_clock_us,
	.idle = idle,
	.console_out = console_out,
	.aux_out = aux_out,
	.aux_in = aux_in,
	.disk_read = disk_read,
	.disk_write = disk_write,
	.disk_format = disk_format,
};

void clear_screen(void) {
	screen_len = 0;
	screen[0] = '\0';
}

machine_state_t run_until_any(const char *const texts[], double seconds, int *which) {
	size_t from = screen_len;
	uint64_t end = host_clock_us() + (uint64_t)(seconds * 1e6);
	while (host_clock_us() < end) {
		machine_state_t st = machine_run(10000);
		if (screen_len < from) // the older half was dropped
			from = 0;
		for (int i = 0; texts[i]; ++i) {
			if (strstr(screen + from, texts[i])) {
				*which = i;
				return MACHINE_RUNNING;
			}
		}
		if (st == MACHINE_STOPPED)
			return st;
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
	while (*s)
		ok = machine_key((uint8_t)*s++) && ok;
	return ok;
}
