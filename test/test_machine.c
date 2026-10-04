// The Z80 machine end to end: a FAT32 volume (in RAM), the embedded disk
// images installed on it, CP/M 2.2 booted from them.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "diskimg.h"
#include "diskio_ram.h"
#include "drivepanel.h"
#include "drives.h"
#include "ff.h"
#include "font.h"
#include "fwpanel.h"
#include "lz.h"
#include "machine.h"
#include "machine_host.h"
#include "monitor.h"
#include "xfer.h"
#include "scratch.h"
#include "test.h"

#define VOLUME_SECTORS (40u * 1024 * 1024 / 512) // FAT32 needs 65525+ clusters

static FATFS fs;
static uint8_t window[LZ_WINDOW];

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

// Progress callbacks seen: how many, and the last one.
static struct {
	unsigned calls;
	uint32_t done, total;
} progress_seen;

static void progress(void *ctx, uint32_t done, uint32_t total) {
	CHECK(ctx == &progress_seen);
	CHECK(done > progress_seen.done || !progress_seen.calls);
	++progress_seen.calls;
	progress_seen.done = done;
	progress_seen.total = total;
}

static void test_install(void) {
	for (unsigned i = 0; i < diskimg_count; ++i) {
		memset(&progress_seen, 0, sizeof progress_seen);
		CHECK_EQ(diskimg_install(&diskimg_list[i], false, window, progress, &progress_seen), DISKIMG_WRITTEN);
		CHECK(progress_seen.calls > 0);
		CHECK_EQ(progress_seen.done, diskimg_list[i].size);
		CHECK_EQ(progress_seen.total, diskimg_list[i].size);
	}
	CHECK(fat_file_exists("/DISKS80/CPM22.DSK"));
	// Installed already: left alone unless asked.
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), false, window, NULL, NULL), DISKIMG_EXISTS);
	CHECK_EQ(diskimg_install(diskimg_find("EMPTY"), true, window, NULL, NULL), DISKIMG_WRITTEN);
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
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "62K CP/M 2.2, 8 drives") != NULL);

	CHECK(type("DIR\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "PIP") != NULL);

	CHECK(type("DIR B:\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "CPUTEST") != NULL);
}

// A file saved from CP/M is on the image after a flush, a remount and a
// cold boot.
static void test_write_persists(void) {
	clear_screen();
	CHECK(type("SAVE 2 HELLO.COM\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(drives_flush());
	drives_unmount_all();
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "Z80TESTS"), DRIVES_OK);

	clear_screen();
	machine_reset();
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(type("DIR HELLO.COM\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "HELLO") != NULL);
	CHECK(strstr(screen, "NO FILE") == NULL);
}

// The hard disk formats: blank images made by drives_create, mounted on E
// and F, seen by CP/M with their capacities (STAT), written to; an empty
// drive is a select error.
static void test_hard_disks(void) {
	memset(&progress_seen, 0, sizeof progress_seen);
	CHECK_EQ(drives_create("HD8", DRIVES_HD8, window, sizeof window, progress, &progress_seen), DRIVES_OK);
	CHECK_EQ(progress_seen.calls, (drives_geometry(DRIVES_HD8)->size + sizeof window - 1) / sizeof window);
	CHECK_EQ(progress_seen.done, drives_geometry(DRIVES_HD8)->size);
	FIL f;
	uint8_t last[DRIVES_SECTOR];
	UINT n;
	CHECK_EQ(f_open(&f, "/DISKS80/HD8.DSK", FA_READ), FR_OK);
	CHECK_EQ(f_size(&f), drives_geometry(DRIVES_HD8)->size);
	CHECK_EQ(f_lseek(&f, f_size(&f) - sizeof last), FR_OK);
	CHECK_EQ(f_read(&f, last, sizeof last, &n), FR_OK);
	CHECK(last[0] == 0xe5 && last[sizeof last - 1] == 0xe5);
	f_close(&f);
	CHECK_EQ(drives_create("HD4", DRIVES_HD4, window, sizeof window, NULL, NULL), DRIVES_OK);
	CHECK_EQ(drives_create("HD8", DRIVES_HD4, window, sizeof window, NULL, NULL), DRIVES_EXISTS);
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
	CHECK(type("\x03")); // ^C: warm boot, so CP/M sees the new drives
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(type("STAT E:DSK:\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "8160: Kilobyte Drive  Capacity") != NULL);
	CHECK(strstr(screen, "1024: 32  Byte Directory Entries") != NULL);
	clear_screen();
	CHECK(type("STAT F:DSK:\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "4080: Kilobyte Drive  Capacity") != NULL);

	// Files on both, read back.
	clear_screen();
	CHECK(type("PIP E:=A:*.COM\r"));
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
	CHECK(type("PIP F:=E:PIP.COM[V]\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(type("DIR F:\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "F: PIP      COM") != NULL);
	CHECK(strstr(screen, "VERIFY ERROR") == NULL);
	clear_screen();
	CHECK(type("STAT E:*.*\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "STAT.COM") != NULL);

	// G: has no disk: BDOS reports a select error and waits for a key.
	clear_screen();
	CHECK(type("DIR G:\r"));
	CHECK_EQ(run_until("Bdos Err On G: Select", 10), MACHINE_RUNNING);
	CHECK(type("x"));
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
	drivepanel_draw(&p, &s, 0, 0x3f, 0, 0x0c);
	CHECK_EQ(screen_glyph(&s, 0, 0), GLYPH_DEC_GRAPHICS('l')); // in a box
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
	CHECK(type("DIR\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "PIP") != NULL);
}

// WORMS (src/cpm/worms.asm) on the CPM22 disk: colour worms, the options,
// the screen size from a cursor position report, any key back to CP/M.
static void test_worms(void) {
	// No size report from this console: 24 x 80; a worm per colour.
	clear_screen();
	CHECK(type("WORMS -D 0 -N 2\r"));
	CHECK_EQ(run_until("\x1b[92m", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "\x1b[?25l") != NULL);
	CHECK(strstr(screen, "\x1b[91m") != NULL);
	CHECK(strstr(screen, "\x1b[24;1H") != NULL); // they start bottom left
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "\x1b[?25h") != NULL);

	clear_screen();
	CHECK(type("WORMS -X\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Usage: WORMS") != NULL);

	// Monochrome: no colour codes.
	clear_screen();
	CHECK(type("WORMS -D0 -M\r"));
	CHECK_EQ(run_until("\x1b[23;", 10), MACHINE_RUNNING);
	for (int i = 0; i < 20; ++i)
		machine_run(1000);
	for (char sgr[] = "\x1b[91m"; sgr[3] <= '6'; ++sgr[3])
		CHECK(strstr(screen, sgr) == NULL);
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// -C: 256-colour rainbows, each worm from its own place on the wheel
	// (worm 0 from red, worm 1 seven steps on), stepping on per head.
	clear_screen();
	CHECK(type("WORMS -D 0 -N 2 -C\r"));
	CHECK_EQ(run_until("\x1b[38;5;154m", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "\x1b[38;5;202m") != NULL);
	for (int i = 0; i < 20; ++i)
		machine_run(1000);
	CHECK(strstr(screen, "\x1b[38;5;208m") != NULL);
	CHECK(strstr(screen, "\x1b[91m") == NULL);
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("WORMS -D0 -C -M\r")); // -M still wins
	CHECK_EQ(run_until("\x1b[23;", 10), MACHINE_RUNNING);
	for (int i = 0; i < 20; ++i)
		machine_run(1000);
	CHECK(strstr(screen, "38;5;") == NULL);
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// A terminal answering 24 x 132: the field fills 132 columns.
	clear_screen();
	host_cpr_reply = "\x1b[24;132R";
	CHECK(type("WORMS -D 0 -F\r"));
	CHECK_EQ(run_until("\x1b[24;1H", 10), MACHINE_RUNNING);
	host_cpr_reply = NULL;
	char row[133];
	memset(row, '.', 132);
	row[132] = '\0';
	CHECK(strstr(screen, row) != NULL);
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
}

static void test_bye_halts(void) {
	clear_screen();
	CHECK(type("BYE\r"));
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
		host_platform.console_out((uint8_t)*text++);
}

static const char *card_info(void) {
	return "RAM, 40 MB";
}

static unsigned flushes;

static void flush(void) {
	++flushes;
}

// The firmware update's platform side, stubbed: the file's first byte says
// whether it checks ('G'ood) and stages ('S' too).
static unsigned stages, commits;

static char firmware_first(const char *path) {
	FIL f;
	char c = 0;
	UINT got;
	if (f_open(&f, path, FA_READ) == FR_OK) {
		f_read(&f, &c, 1, &got);
		f_close(&f);
	}
	return c;
}

static bool firmware_check(const char *path, char *info, size_t n) {
	bool ok = firmware_first(path) == 'G' || firmware_first(path) == 'S';
	snprintf(info, n, "%s", ok ? "test firmware" : "not a UF2 file");
	return ok;
}

static bool firmware_stage(const char *path, void (*progress)(void *, uint32_t, uint32_t), void *ctx, char *info,
	size_t n) {
	++stages;
	for (uint32_t done = 0; done <= 128 * 1024; done += 512)
		progress(ctx, done, 128 * 1024);
	bool ok = firmware_first(path) == 'S';
	snprintf(info, n, "%s", ok ? "" : "flash verify failed");
	return ok;
}

static void firmware_commit(void) {
	++commits;
}

static const monitor_platform_t monitor_platform = {.write = console_text, .card_info = card_info, .flush = flush};

// The whole menu was shown, up to its prompt (lpt.8: it was cut short).
static bool menu_shown(void) {
	return strstr(screen, "C  check the card again\r\n") && strstr(screen, "F12 SET-UP (Drives, Mode, firmware update)\r\n> ") &&
		screen_len >= 2 && strcmp(screen + screen_len - 2, "> ") == 0;
}

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
	machine_init(&host_platform);
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
	CHECK(menu_shown());

	// The scratch buffer has one holder at a time: while another job has
	// it, installing and blank disks say so and write nothing.
	uint8_t *held = scratch_acquire();
	CHECK(held != NULL);
	CHECK(scratch_acquire() == NULL);
	clear_screen();
	CHECK_EQ(keys("in"), MONITOR_NONE); // not over the ones there
	CHECK(strstr(screen, "Busy.") != NULL);
	CHECK(strstr(screen, "written") == NULL);
	clear_screen();
	CHECK_EQ(keys("nbusy\r1"), MONITOR_NONE);
	CHECK(strstr(screen, "Busy.") != NULL);
	CHECK(menu_shown());
	CHECK_EQ(drives_image_format("BUSY"), DRIVES_NONE);
	scratch_release(held);

	// A new 8 MB disk: progress dots, shown as they come (flush).
	clear_screen();
	flushes = 0;
	CHECK_EQ(keys("nbig\r3"), MONITOR_NONE);
	CHECK(strstr(screen, "Writing BIG.DSK (8160 KB)... ............................... done") != NULL);
	CHECK(flushes >= 31);
	CHECK(menu_shown());
	CHECK_EQ(drives_image_format("BIG"), DRIVES_HD8);

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
	CHECK(type("DIR C:\r"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "NO FILE") != NULL);

	// Stopped from SET-UP: resume carries on.
	clear_screen();
	monitor_enter(NULL, true);
	CHECK(strstr(screen, "R       resume") != NULL);
	CHECK(menu_shown());
	CHECK_EQ(keys("r"), MONITOR_RUN);
	CHECK(type("DIR B:\r"));
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

static void write_file(const char *path, const char *text) {
	FIL f;
	UINT n;
	CHECK_EQ(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS), FR_OK);
	CHECK_EQ(f_write(&f, text, (UINT)strlen(text), &n), FR_OK);
	CHECK_EQ(f_close(&f), FR_OK);
}

// The firmware update is SET-UP's (U) only: the monitor neither offers nor
// takes U, even with FIRMWARE.UF2 on the card, and points at SET-UP.
static void test_monitor_no_firmware_update(void) {
	write_file(FWPANEL_FILE, "STAGES");
	clear_screen();
	monitor_enter(NULL, false);
	CHECK(strstr(screen, "U  ") == NULL);
	CHECK(strstr(screen, "F12 SET-UP (Drives, Mode, firmware update)") != NULL);
	clear_screen();
	CHECK_EQ(keys("u"), MONITOR_NONE);
	CHECK_EQ(screen_len, 0);
	f_unlink(FWPANEL_FILE);
}

// SET-UP's firmware update panel, with the same stubs.
static bool panel_card;
static unsigned panel_shows, panel_unmounts, panel_commit_unmounts;

static bool panel_mount(char *info, size_t n) {
	if (!panel_card)
		snprintf(info, n, "no SD card found");
	return panel_card;
}

static void panel_unmount(bool commit) {
	++*(commit ? &panel_commit_unmounts : &panel_unmounts);
}

static screen_t panel_shown; // as panel_show last drew it

static void panel_show(const fwpanel_t *p) {
	++panel_shows;
	screen_init(&panel_shown, 80, 7, 0);
	fwpanel_draw(p, &panel_shown, 0, 7, 0, 4);
}

static bool shown_row(unsigned row, const char *text) {
	char line[81];
	for (unsigned c = 0; c < 80; ++c)
		line[c] = (char)screen_glyph(&panel_shown, c, row);
	line[80] = 0;
	return strstr(line, text) != NULL;
}

static const fwpanel_platform_t panel_platform = {.mount = panel_mount, .check = firmware_check,
	.stage = firmware_stage, .unmount = panel_unmount, .commit = firmware_commit, .show = panel_show};

static bool panel_row(const fwpanel_t *p, unsigned row, const char *text) {
	static screen_t s;
	screen_init(&s, 80, 7, 0);
	fwpanel_draw(p, &s, 0, 7, 0, 4);
	char line[81];
	for (unsigned c = 0; c < 80; ++c)
		line[c] = (char)screen_glyph(&s, c, row);
	line[80] = 0;
	return strstr(line, text) != NULL;
}

static void test_firmware_panel(void) {
	static fwpanel_t p;
	FILINFO info;
	stages = commits = panel_unmounts = panel_commit_unmounts = 0;

	// No card: says so, any key goes back, nothing to let go.
	panel_card = false;
	fwpanel_open(&p);
	CHECK(panel_row(&p, 0, "SET-UP  Firmware update"));
	CHECK(panel_row(&p, 2, "Checking FIRMWARE.UF2"));
	CHECK_EQ(fwpanel_input(&p, 'y'), FWPANEL_NONE); // busy
	CHECK_EQ(fwpanel_run(&p, &panel_platform), FWPANEL_REDRAW);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 3, "no SD card found"));
	CHECK_EQ(fwpanel_input(&p, 'x'), FWPANEL_CANCEL);
	fwpanel_close(&p, &panel_platform);
	CHECK_EQ(panel_unmounts, 0);

	// No file: the check refuses it; leaving lets the card go.
	panel_card = true;
	f_unlink(FWPANEL_FILE);
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 2, "cannot be installed"));
	fwpanel_close(&p, &panel_platform);
	CHECK_EQ(panel_unmounts, 1);

	// Declined with N, or Escape.
	write_file(FWPANEL_FILE, "GOOD");
	for (const char *k = "N\x1b"; *k; ++k) {
		fwpanel_open(&p);
		fwpanel_run(&p, &panel_platform);
		CHECK_EQ(p.state, FWPANEL_ASK);
		CHECK(panel_row(&p, 2, "Install it and restart (Y/N)?"));
		CHECK(panel_row(&p, 3, "test firmware"));
		CHECK(panel_row(&p, 6, "Y install and restart"));
		CHECK_EQ(fwpanel_input(&p, 'x'), FWPANEL_NONE);
		CHECK_EQ(fwpanel_input(&p, (uint8_t)*k), FWPANEL_CANCEL);
		fwpanel_close(&p, &panel_platform);
	}
	CHECK_EQ(stages, 0);

	// Staging fails: the file stays, nothing committed.
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(fwpanel_input(&p, 'y'), FWPANEL_WORK);
	CHECK_EQ(fwpanel_run(&p, &panel_platform), FWPANEL_REDRAW);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 3, "flash verify failed"));
	CHECK_EQ(stages, 1);
	CHECK_EQ(commits, 0);
	CHECK_EQ(f_stat(FWPANEL_FILE, &info), FR_OK);
	fwpanel_close(&p, &panel_platform);

	// Installed: a progress bar (redrawn as it grows, not per block),
	// renamed, the card closed for the commit, committed.
	write_file(FWPANEL_FILE_OLD, "older");
	write_file(FWPANEL_FILE, "STAGES");
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	fwpanel_input(&p, 'Y');
	panel_shows = 0;
	fwpanel_run(&p, &panel_platform);
	CHECK(panel_shows > 10 && panel_shows < 60); // 257 progress calls
	// Last shown before the commit:
	CHECK(shown_row(2, "Installing the new firmware: do not power off."));
	CHECK(shown_row(4, "[##################################################] 100%"));
	CHECK_EQ(commits, 1);
	CHECK_EQ(panel_commit_unmounts, 1);
	CHECK_EQ(f_stat(FWPANEL_FILE, &info), FR_NO_FILE);
	CHECK_EQ(f_stat(FWPANEL_FILE_OLD, &info), FR_OK);
	CHECK_EQ(info.fsize, 6);
	f_unlink(FWPANEL_FILE_OLD);
}

// On a card's usual 32 KB clusters a blank disk reaches the card many
// sectors at a time (multi-block writes), not one by one (lpt.9).
static void test_blank_disk_chunks(void) {
	drives_unmount_all();
	f_unmount("");
	diskio_ram_init(VOLUME_SECTORS);
	static uint8_t work[4096];
	const MKFS_PARM opt = {.fmt = FM_ANY, .au_size = 32768};
	CHECK_EQ(f_mkfs("", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
	diskio_ram_max_write();
	CHECK_EQ(drives_create("HD8", DRIVES_HD8, window, sizeof window, NULL, NULL), DRIVES_OK);
	CHECK(diskio_ram_max_write() >= sizeof window / 512);
	CHECK_EQ(drives_image_format("HD8"), DRIVES_HD8);
}

// ---- the file transfer device (port 10; xfer.c's own tests are test_xfer) ----

static uint8_t xbuf[XFER_RECORD];

// Run an xfer command on volume 0, with name (11 bytes, FCB form) as its
// input if given.
static uint8_t xcmd(uint8_t cmd, const char *name) {
	if (name)
		memcpy(xbuf, name, XFER_NAME);
	return xfer_command(cmd, 0, xbuf);
}

static uint32_t fat_size(const char *path) {
	FILINFO info;
	return f_stat(path, &info) == FR_OK ? (uint32_t)info.fsize : UINT32_MAX;
}

// Z80 code for the port test, assembled into the boot ROM's page.
static struct {
	uint8_t code[256];
	unsigned n;
} z80;

static void emit(unsigned n, ...) {
	va_list ap;
	va_start(ap, n);
	for (unsigned i = 0; i < n; ++i)
		z80.code[z80.n++] = (uint8_t)va_arg(ap, int);
	va_end(ap);
}

static void out10(uint8_t v) {
	emit(4, 0x3e, v, 0xd3, 10); // LD A,v; OUT (10),A
}

static void setdma(uint16_t addr) {
	out10(XFER_SETDMA);
	out10((uint8_t)addr);
	out10((uint8_t)(addr >> 8));
}

// The command, then its status stored at status_at.
static void command(uint8_t cmd, uint16_t status_at) {
	out10(cmd);
	emit(5, 0xdb, 10, 0x32, status_at & 0xff, status_at >> 8); // IN A,(10); LD (nn),A
}

static void test_xfer_port(void) {
	extern uint8_t bnk0[65536], bnks[][48 * 1024];
	const uint16_t name = 0x8000, rec = 0x8100, back = 0x8200, banked = 0x8300, st = 0xf000;
	machine_init(&host_platform);
	z80.n = 0;
	emit(1, 0xf3); // DI
	setdma(name);
	command(XFER_CREATE, st + 0);
	setdma(rec);
	command(XFER_WRITE, st + 1);
	command(XFER_CLOSE, st + 2);
	setdma(name);
	command(XFER_OPEN, st + 3);
	setdma(back);
	command(XFER_READ, st + 4);
	command(XFER_READ, st + 5);
	// Again into bank 1: the DMA goes where the CPU sees memory.
	setdma(name);
	command(XFER_OPEN, st + 6);
	setdma(banked);
	emit(4, 0x3e, 1, 0xd3, 64); // LD A,1; OUT (64),A
	command(XFER_READ, st + 7);
	emit(4, 0x3e, 0, 0xd3, 64);
	command(XFER_CLOSE, st + 8);
	setdma(name);
	command(XFER_FIND_FIRST, st + 9);
	emit(1, 0x76); // HALT
	CHECK(z80.n <= sizeof z80.code);
	memcpy(bnk0 + 0xff00, z80.code, z80.n);
	memcpy(bnk0 + name, "PORT    BIN", XFER_NAME);
	for (unsigned i = 0; i < XFER_RECORD; ++i)
		bnk0[rec + i] = (uint8_t)(i ^ 0x5a);
	memset(bnk0 + st, 0xff, 16);
	machine_reset(); // from the code above
	unsigned slices = 0;
	while (machine_run(10000) == MACHINE_RUNNING && ++slices < 100)
		;
	CHECK(strstr(machine_stop_reason(), "HALT with interrupts off") != NULL);
	static const uint8_t want[] = {XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_EOF,
		XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK};
	for (unsigned i = 0; i < sizeof want; ++i)
		CHECK_EQ(bnk0[st + i], want[i]);
	CHECK(memcmp(bnk0 + back, bnk0 + rec, XFER_RECORD) == 0);
	CHECK(memcmp(bnks[0] + banked, bnk0 + rec, XFER_RECORD) == 0);
	// The listing entry: the name and the size (128).
	CHECK(memcmp(bnk0 + name, "PORT    BIN", XFER_NAME) == 0);
	CHECK(bnk0[name + 11] == 128 && bnk0[name + 12] == 0 && bnk0[name + 13] == 0 && bnk0[name + 14] == 0);
	CHECK_EQ(fat_size("/XFER/PORT.BIN"), XFER_RECORD);
	// A reset closes what the program left open.
	CHECK_EQ(xcmd(XFER_OPEN, "PORT    BIN"), XFER_ST_OK);
	machine_reset();
	CHECK_EQ(xcmd(XFER_READ, NULL), XFER_ST_BAD);
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
	RUN(test_worms);
	RUN(test_bye_halts);
	RUN(test_no_disk_boot_rom);
	RUN(test_monitor_fresh_card);
	RUN(test_monitor_autoboot);
	RUN(test_monitor_no_firmware_update);
	RUN(test_firmware_panel);
	RUN(test_blank_disk_chunks);
	RUN(test_xfer_port);
	drives_unmount_all();
	f_unmount("");
	diskio_ram_free();
	return tests_done();
}
