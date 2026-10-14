// cpmrun: run CP/M on the Z80 machine (src/machine/machine.c) on the build host,
// with plain 8" floppy images as the drives and a script at the console.
// src/machine/cpm/cpm3/build.sh uses it to run RMAC, LINK and GENCPM, which make
// CPM3.SYS; built there with the host C compiler.
//
//   cpmrun SCRIPT A.DSK [B.DSK ...]
//
// Boots from A:. The images are read whole, and written back when the
// script has run to the end. SCRIPT has one step per line:
//
//   > TEXT    wait for TEXT on the console, after the last wait's text
//   < TEXT    type TEXT and a carriage return
//   ! TEXT    fail if TEXT shows up on the console from here on
//   # ...     a comment (as are blank lines)
//
// The console goes to stdout. Exits 1 (images untouched) on a failed
// step: a wait over 60 seconds, a ! text seen, or the CPU stopping.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"

#define DRIVES 8
#define SECTOR 128
#define TRACKS 77
#define SECTORS 26
#define IMAGE_SIZE (TRACKS * SECTORS * SECTOR)
#define WAIT_SECONDS 60
#define MAX_FAILS 8

static uint8_t *image[DRIVES];
static char out[1 << 16]; // the console after the last wait's text
static size_t out_len;
static const char *fails[MAX_FAILS];
static int nfails;

static uint64_t clock_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void idle(void) {
}

static void console_out(uint8_t c) {
	if (c == 0)
		return;
	putchar(c);
	if (out_len == sizeof out - 1) { // keep the newer half
		memmove(out, out + sizeof out / 2, sizeof out / 2);
		out_len -= sizeof out / 2;
	}
	out[out_len++] = (char)c;
	out[out_len] = '\0';
}

static void aux_out(uint8_t c) {
	(void)c;
}

static bool aux_in(uint8_t *c) {
	(void)c;
	return false;
}

// The sector's place in its image, or an FDC status (z80pack's sd-fdc.h:
// 2 no disk, 8 bad track, 9 bad sector).
static uint8_t locate(unsigned drive, unsigned track, unsigned sector, uint8_t **p) {
	if (drive >= DRIVES || !image[drive])
		return 2;
	if (track >= TRACKS)
		return 8;
	if (sector < 1 || sector > SECTORS)
		return 9;
	*p = image[drive] + ((size_t)track * SECTORS + sector - 1) * SECTOR;
	return 0;
}

static uint8_t disk_read(unsigned drive, unsigned track, unsigned sector, uint8_t *buf) {
	uint8_t *p;
	uint8_t st = locate(drive, track, sector, &p);
	if (st == 0)
		memcpy(buf, p, SECTOR);
	return st;
}

static uint8_t disk_write(unsigned drive, unsigned track, unsigned sector, const uint8_t *buf) {
	uint8_t *p;
	uint8_t st = locate(drive, track, sector, &p);
	if (st == 0)
		memcpy(p, buf, SECTOR);
	return st;
}

static uint8_t disk_format(unsigned drive) {
	return drive < DRIVES && image[drive] ? 1 : 0;
}

static const machine_platform_t platform = {
	.clock_us = clock_us,
	.idle = idle,
	.console_out = console_out,
	.aux_out = aux_out,
	.aux_in = aux_in,
	.disk_read = disk_read,
	.disk_write = disk_write,
	.disk_format = disk_format,
};

static void fail(const char *why, const char *text) {
	fflush(stdout);
	fprintf(stderr, "\ncpmrun: %s%s%s\n", why, text ? ": " : "", text ? text : "");
	exit(1);
}

static void check_fails(void) {
	for (int i = 0; i < nfails; ++i)
		if (strstr(out, fails[i]))
			fail("error on the console", fails[i]);
}

static void wait_for(const char *text) {
	uint64_t end = clock_us() + WAIT_SECONDS * 1000000ull;
	const char *at;
	while (!(at = strstr(out, text))) {
		if (clock_us() > end)
			fail("timed out waiting for", text);
		if (machine_run(10000) == MACHINE_STOPPED)
			fail("the CPU stopped", machine_stop_reason());
		check_fails();
	}
	check_fails();
	// The next wait looks at what came after this text.
	size_t used = (size_t)(at - out) + strlen(text);
	out_len -= used;
	memmove(out, out + used, out_len + 1);
}

static void type_line(const char *text) {
	for (const char *p = text; *p; ++p) {
		// The key queue is short: let the CPU take keys while it is full.
		while (!machine_key((uint8_t)*p))
			if (machine_run(1000) == MACHINE_STOPPED)
				fail("the CPU stopped", machine_stop_reason());
	}
	while (!machine_key('\r'))
		if (machine_run(1000) == MACHINE_STOPPED)
			fail("the CPU stopped", machine_stop_reason());
}

static uint8_t *load(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f)
		fail("cannot open", path);
	uint8_t *p = malloc(IMAGE_SIZE);
	if (!p || fread(p, 1, IMAGE_SIZE, f) != IMAGE_SIZE || fgetc(f) != EOF)
		fail("not an 8\" floppy image", path);
	fclose(f);
	return p;
}

int main(int argc, char **argv) {
	if (argc < 3 || argc - 2 > DRIVES) {
		fprintf(stderr, "usage: cpmrun SCRIPT A.DSK [B.DSK ...]\n");
		return 2;
	}
	for (int d = 0; d < argc - 2; ++d)
		image[d] = load(argv[2 + d]);
	FILE *script = fopen(argv[1], "r");
	if (!script)
		fail("cannot open", argv[1]);

	machine_init(&platform);
	static char line[256];
	while (fgets(line, sizeof line, script)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (strlen(line) < 3 || line[1] != ' ')
			fail("bad script line", line);
		const char *text = line + 2;
		switch (line[0]) {
		case '>':
			wait_for(text);
			break;
		case '<':
			type_line(text);
			break;
		case '!':
			if (nfails == MAX_FAILS)
				fail("too many ! lines", text);
			fails[nfails++] = strdup(text);
			break;
		default:
			fail("bad script line", line);
		}
	}
	fclose(script);
	fflush(stdout);

	for (int d = 0; d < argc - 2; ++d) {
		FILE *f = fopen(argv[2 + d], "wb");
		if (!f || fwrite(image[d], 1, IMAGE_SIZE, f) != IMAGE_SIZE || fclose(f) != 0)
			fail("cannot write", argv[2 + d]);
	}
	return 0;
}
