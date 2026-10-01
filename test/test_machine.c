// The Z80 machine end to end: a FAT32 volume (in RAM), the embedded disk
// images installed on it, CP/M 2.2 booted from them.

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diskimg.h"
#include "diskio_ram.h"
#include "drivepanel.h"
#include "drives.h"
#include "ff.h"
#include "lz.h"
#include "machine.h"
#include "monitor.h"
#include "test.h"

#define VOLUME_SECTORS (40u * 1024 * 1024 / 512) // FAT32 needs 65525+ clusters

static FATFS fs;
static uint8_t window[LZ_WINDOW];

static char screen[65536];
static size_t screen_len;
static char aux_sent[256];
static size_t aux_len;

static uint64_t host_clock_us(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void idle(void) {
}

static void console_out(uint8_t c) {
	if (screen_len < sizeof screen - 1)
		screen[screen_len++] = (char)c;
	screen[screen_len] = '\0';
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

static const machine_platform_t platform = {
	.clock_us = host_clock_us,
	.idle = idle,
	.console_out = console_out,
	.aux_out = aux_out,
	.aux_in = aux_in,
	.disk_read = disk_read,
	.disk_write = disk_write,
	.disk_format = disk_format,
};

static void clear_screen(void) {
	screen_len = 0;
	screen[0] = '\0';
}

// Run until the console shows text (after what was there already) or the
// machine stops; at most `seconds`.
static machine_state_t run_until(const char *text, double seconds) {
	size_t from = screen_len;
	uint64_t end = host_clock_us() + (uint64_t)(seconds * 1e6);
	while (host_clock_us() < end) {
		machine_state_t st = machine_run(10000);
		if (strstr(screen + from, text))
			return MACHINE_RUNNING;
		if (st == MACHINE_STOPPED)
			return st;
	}
	fprintf(stderr, "timed out waiting for \"%s\"; console:\n%s\n", text, screen);
	return MACHINE_STOPPED;
}

static void type(const char *s) {
	while (*s)
		CHECK(machine_key((uint8_t)*s++));
}

static bool fat_file_exists(const char *path) {
	FILINFO info;
	return f_stat(path, &info) == FR_OK;
}

static void test_fat32_volume(void) {
	diskio_ram_init(VOLUME_SECTORS);
	static uint8_t work[4096];
	const MKFS_PARM opt = {.fmt = FM_FAT32, .au_size = 512};
	CHECK_EQ(f_mkfs("", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
	CHECK_EQ(fs.fs_type, FS_FAT32);
}

static void test_install(void) {
	for (unsigned i = 0; i < diskimg_count; ++i)
		CHECK_EQ(diskimg_install(&diskimg_list[i], false, window), DISKIMG_WRITTEN);
	CHECK(fat_file_exists("/DISKS80/CPM22.DSK"));
	// Installed already: left alone unless asked.
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), false, window), DISKIMG_EXISTS);
	CHECK_EQ(diskimg_install(diskimg_find("EMPTY"), true, window), DISKIMG_WRITTEN);
}

static void test_mount(void) {
	CHECK_EQ(drives_mount(0, "cpm22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "Z80TESTS"), DRIVES_OK);
	CHECK_EQ(drives_mount(2, "CPM22"), DRIVES_IN_USE);
	CHECK_EQ(drives_mount(2, "NOSUCH"), DRIVES_NOT_FOUND);
	CHECK_EQ(drives_mount(2, "bad.name"), DRIVES_NOT_FOUND);
	CHECK(strcmp(drives_name(0), "CPM22") == 0);
	CHECK(drives_name(3) == NULL);

	// Not an image: wrong size.
	FIL f;
	UINT n;
	CHECK_EQ(f_open(&f, "/DISKS80/SHORT.DSK", FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
	CHECK_EQ(f_write(&f, "x", 1, &n), FR_OK);
	f_close(&f);
	CHECK_EQ(drives_mount(3, "SHORT"), DRIVES_BAD_IMAGE);

	uint8_t buf[DRIVES_SECTOR];
	drives_activity();
	CHECK_EQ(drives_read(3, 0, 1, buf), 2);  // no disk
	CHECK_EQ(drives_read(0, 77, 1, buf), 8); // track out of range
	CHECK_EQ(drives_read(0, 0, 27, buf), 9); // sector out of range
	CHECK_EQ(drives_activity(), 0); // only real accesses light up
	CHECK_EQ(drives_read(0, 0, 1, buf), 0);
	CHECK_EQ(drives_read(1, 2, 1, buf), 0);
	CHECK_EQ(drives_activity(), 0x03);
	CHECK_EQ(drives_activity(), 0);
}

static void test_boot_cpm22(void) {
	clear_screen();
	machine_init(&platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "62K CP/M 2.2, 8 drives") != NULL);

	type("DIR\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "PIP") != NULL);

	type("DIR B:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "CPUTEST") != NULL);
}

// A file saved from CP/M is on the image after a flush, a remount and a
// cold boot.
static void test_write_persists(void) {
	clear_screen();
	type("SAVE 2 HELLO.COM\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(drives_flush());
	drives_unmount_all();
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "Z80TESTS"), DRIVES_OK);

	clear_screen();
	machine_reset();
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	type("DIR HELLO.COM\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "HELLO") != NULL);
	CHECK(strstr(screen, "NO FILE") == NULL);
}

// The hard disk formats: blank images made by drives_create, mounted on E
// and F, seen by CP/M with their capacities (STAT), written to; an empty
// drive is a select error.
static void test_hard_disks(void) {
	CHECK_EQ(drives_create("HD8", DRIVES_HD8), DRIVES_OK);
	CHECK_EQ(drives_create("HD4", DRIVES_HD4), DRIVES_OK);
	CHECK_EQ(drives_create("HD8", DRIVES_HD4), DRIVES_EXISTS);
	CHECK_EQ(drives_mount(4, "HD8"), DRIVES_OK);
	CHECK_EQ(drives_mount(5, "HD4"), DRIVES_OK);
	CHECK_EQ(drives_format(4), DRIVES_HD8);
	CHECK_EQ(drives_format(5), DRIVES_HD4);
	CHECK_EQ(drives_format(0), DRIVES_FLOPPY);

	uint8_t buf[DRIVES_SECTOR];
	CHECK_EQ(drives_read(4, 255, 255, buf), 0); // the last sector
	CHECK_EQ(buf[0], 0xe5);
	CHECK_EQ(drives_read(4, 0, 256, buf), 9);
	CHECK_EQ(drives_read(5, 254, 128, buf), 0);
	CHECK_EQ(drives_read(5, 255, 1, buf), 8);

	clear_screen();
	type("\x03"); // ^C: warm boot, so CP/M sees the new drives
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	type("STAT E:DSK:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "8160: Kilobyte Drive  Capacity") != NULL);
	CHECK(strstr(screen, "1024: 32  Byte Directory Entries") != NULL);
	clear_screen();
	type("STAT F:DSK:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "4080: Kilobyte Drive  Capacity") != NULL);

	// Files on both, read back.
	clear_screen();
	type("PIP E:=A:*.COM\r");
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
	type("PIP F:=E:PIP.COM[V]\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	type("DIR F:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "F: PIP      COM") != NULL);
	CHECK(strstr(screen, "VERIFY ERROR") == NULL);
	clear_screen();
	type("STAT E:*.*\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "STAT.COM") != NULL);

	// G: has no disk: BDOS reports a select error and waits for a key.
	clear_screen();
	type("DIR G:\r");
	CHECK_EQ(run_until("Bdos Err On G: Select", 10), MACHINE_RUNNING);
	type("x");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
}

static drivepanel_action_t panel_keys(drivepanel_t *p, const char *keys) {
	drivepanel_action_t last = DRIVEPANEL_NONE;
	for (; *keys; ++keys) {
		drivepanel_action_t a = drivepanel_input(p, (uint8_t)*keys, 0);
		if (a != DRIVEPANEL_NONE)
			last = a;
	}
	return last;
}

// SET-UP's Drives panel: shows the mounts, steps through the images
// (skipping those on other drives), mounts the choices and saves them.
static void test_drive_panel(void) {
	static drivepanel_t p;
	drivepanel_open(&p);
	// On the card: CPM22 EMPTY HD4 HD8 SHORT Z80TESTS (sorted).
	CHECK_EQ(p.n_images, 6);
	CHECK(strcmp(p.images[p.choice[0]], "CPM22") == 0);
	CHECK(strcmp(p.images[p.choice[4]], "HD8") == 0);
	CHECK_EQ(p.choice[2], -1);

	screen_t s;
	screen_init(&s, 80, 0x3f, 0);
	drivepanel_draw(&p, &s, 0, 0x3f, 0);
	char row[81] = "";
	for (unsigned c = 0; c < 80; ++c)
		row[c] = (char)screen_glyph(&s, c, 5);
	CHECK(strstr(row, "E:") && strstr(row, "HD8") && strstr(row, "8 MB disk"));

	// C: none -> CPM22 is on A, EMPTY is free: the first step takes EMPTY.
	CHECK_EQ(panel_keys(&p, "\x1b[B\x1b[B"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.drive, 2);
	CHECK_EQ(panel_keys(&p, " "), DRIVEPANEL_REDRAW);
	CHECK(p.choice[2] >= 0 && strcmp(p.images[p.choice[2]], "EMPTY") == 0);
	// B: Z80TESTS -> back one: SHORT; back again skips HD8 (E), HD4 (F),
	// EMPTY (C, just chosen) and CPM22 (A): none. Forward twice: Z80TESTS.
	CHECK_EQ(panel_keys(&p, "\x1b[A\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK(strcmp(p.images[p.choice[1]], "SHORT") == 0);
	CHECK_EQ(panel_keys(&p, "\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.choice[1], -1);
	CHECK_EQ(panel_keys(&p, "\x1b[C\x1b[C"), DRIVEPANEL_REDRAW);
	CHECK(p.choice[1] >= 0 && strcmp(p.images[p.choice[1]], "Z80TESTS") == 0);

	CHECK_EQ(panel_keys(&p, "\r"), DRIVEPANEL_APPLY);
	bool failed;
	CHECK_EQ(drivepanel_apply(&p, &failed), 1);
	CHECK(!failed);
	CHECK(strcmp(drives_name(2), "EMPTY") == 0);

	// Saved: a reload gives the same.
	CHECK(drives_load_config());
	CHECK(strcmp(drives_name(2), "EMPTY") == 0);
	CHECK(strcmp(drives_name(4), "HD8") == 0);

	// A bad image fails to mount.
	drivepanel_open(&p);
	p.drive = 7;
	p.choice[7] = 4; // SHORT
	CHECK_EQ(drivepanel_apply(&p, &failed), 1);
	CHECK(failed);
	CHECK(drives_name(7) == NULL);

	// Escape goes back.
	drivepanel_open(&p);
	CHECK_EQ(panel_keys(&p, "\x1b"), DRIVEPANEL_NONE);
	CHECK_EQ(drivepanel_tick(&p, DRIVEPANEL_ESC_TIMEOUT_MS), DRIVEPANEL_CANCEL);
}

static void test_benchmark(void) {
	uint32_t khz = machine_benchmark_khz(200);
	printf("     host Z80 speed: %u.%02u MHz\n", (unsigned)(khz / 1000), (unsigned)(khz % 1000 / 10));
	CHECK(khz > 1000);
	// The machine carries on where it was.
	clear_screen();
	type("DIR\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "PIP") != NULL);
}

static void test_bye_halts(void) {
	clear_screen();
	type("BYE\r");
	CHECK_EQ(run_until("never", 10), MACHINE_STOPPED);
	CHECK(strcmp(machine_stop_reason(), "System halted") == 0);
}

static void test_no_disk_boot_rom(void) {
	// Drive A empty: the boot ROM reads "no disk" and jumps to 0, where the
	// noise is: whatever happens, the machine must not crash the host.
	drives_unmount_all();
	clear_screen();
	machine_reset();
	for (int i = 0; i < 20 && machine_run(1000) == MACHINE_RUNNING; ++i) {
	}
	CHECK(1);
}

static void console_text(const char *text) {
	while (*text)
		console_out((uint8_t)*text++);
}

static const char *card_info(void) {
	return "RAM, 40 MB";
}

static const monitor_platform_t monitor_platform = {.write = console_text, .card_info = card_info};

static monitor_action_t keys(const char *s) {
	monitor_action_t a = MONITOR_NONE;
	while (*s)
		a = monitor_key((uint8_t)*s++, 0);
	return a;
}

// A blank card: the monitor offers to install, mounts the new disks, boots.
static void test_monitor_fresh_card(void) {
	drives_unmount_all();
	f_unmount("");
	diskio_ram_init(VOLUME_SECTORS);
	static uint8_t work[4096];
	const MKFS_PARM opt = {.fmt = FM_FAT32, .au_size = 512};
	CHECK_EQ(f_mkfs("", &opt, work, sizeof work), FR_OK);

	clear_screen();
	machine_init(&platform);
	monitor_start(&monitor_platform, true, 0);
	CHECK(strstr(screen, "RAM, 40 MB, FAT32") != NULL);
	CHECK(strstr(screen, "No boot disk") != NULL);
	CHECK_EQ(monitor_tick(MONITOR_AUTOBOOT_MS), MONITOR_NONE); // nothing to boot

	clear_screen();
	CHECK_EQ(keys("i"), MONITOR_NONE);
	CHECK(strstr(screen, "CPM22    written") != NULL);
	CHECK(strstr(screen, "A: CPM22") != NULL);
	CHECK(strstr(screen, "B: Z80TESTS") != NULL);

	clear_screen();
	CHECK_EQ(keys("mcempty\r"), MONITOR_NONE);
	CHECK(strstr(screen, "C: EMPTY") != NULL);
	CHECK_EQ(keys("mdnosuch\r"), MONITOR_NONE);
	CHECK(strstr(screen, "no such disk") != NULL);
	CHECK_EQ(keys("l"), MONITOR_NONE);
	CHECK(strstr(screen, "Z80TESTS") != NULL);

	// The assignments survive checking the card again (DRIVES.CFG).
	clear_screen();
	CHECK_EQ(keys("c"), MONITOR_NONE);
	CHECK(strstr(screen, "C: EMPTY") != NULL);

	// Install again: asks before replacing.
	clear_screen();
	CHECK_EQ(keys("i"), MONITOR_NONE);
	CHECK(strstr(screen, "(Y/N)") != NULL);
	CHECK_EQ(keys("n"), MONITOR_NONE);
	CHECK(strstr(screen, "already there") != NULL);

	clear_screen();
	CHECK_EQ(keys("\r"), MONITOR_RUN);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	type("DIR C:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "NO FILE") != NULL);

	// Stopped from SET-UP: resume carries on.
	clear_screen();
	monitor_enter(NULL, true);
	CHECK(strstr(screen, "R       resume") != NULL);
	CHECK_EQ(keys("r"), MONITOR_RUN);
	type("DIR B:\r");
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "CPUTEST") != NULL);
}

static void test_monitor_autoboot(void) {
	clear_screen();
	monitor_start(&monitor_platform, true, 1000);
	CHECK(strstr(screen, "Booting from A: in 3 s") != NULL);
	CHECK_EQ(monitor_tick(1000 + MONITOR_AUTOBOOT_MS - 1), MONITOR_NONE);
	CHECK_EQ(monitor_tick(1000 + MONITOR_AUTOBOOT_MS), MONITOR_RUN);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// A key during the countdown shows the menu instead.
	clear_screen();
	monitor_start(&monitor_platform, true, 0);
	CHECK_EQ(monitor_key('x', 10), MONITOR_NONE);
	CHECK(strstr(screen, "M  mount") != NULL);
	CHECK_EQ(monitor_tick(MONITOR_AUTOBOOT_MS), MONITOR_NONE);
}

int main(void) {
	srand(1);
	RUN(test_fat32_volume);
	RUN(test_install);
	RUN(test_mount);
	RUN(test_boot_cpm22);
	RUN(test_write_persists);
	RUN(test_hard_disks);
	RUN(test_drive_panel);
	RUN(test_benchmark);
	RUN(test_bye_halts);
	RUN(test_no_disk_boot_rom);
	RUN(test_monitor_fresh_card);
	RUN(test_monitor_autoboot);
	drives_unmount_all();
	f_unmount("");
	diskio_ram_free();
	return tests_done();
}
