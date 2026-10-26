// The Z80 machine end to end: a FAT32 volume (in RAM), the embedded disk
// images installed on it, CP/M 2.2 booted from them.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "datetime.h"
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
#include "palette.h"
#include "xfer.h"
#include "scratch.h"
#include "setup.h"
#include "term.h"
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
		if (a != DRIVEPANEL_NONE) {
			last = a;
		}
	}
	return last;
}

// SET-UP's Drives panel: shows the mounts, steps through the images
// (skipping those on other drives), mounts the choices and saves them.
static void test_drive_panel(void) {
	static drivepanel_t p;
	drivepanel_open(&p);
	// On the card: CPM22 CPM31 CPM32 CPM3GEN EMPTY F83 HD4 HD8 SHORT
	// Z80TESTS (sorted).
	CHECK_EQ(p.n_images, 10);
	CHECK(strcmp(p.images[p.choice[0]], "CPM22") == 0);
	CHECK(strcmp(p.images[p.choice[4]], "HD8") == 0);
	CHECK_EQ(p.choice[2], -1);

	screen_t s;
	screen_init(&s, 80, 0x3f, 0);
	drivepanel_draw(&p, &s, 0, 0x3f, 0, 0x0c);
	CHECK_EQ(screen_glyph(&s, 0, 0), GLYPH_DEC_GRAPHICS('l')); // in a box
	char row[81] = "";
	for (unsigned c = 0; c < 80; ++c) {
		row[c] = (char)screen_glyph(&s, c, 6); // under the title and the Set row
	}
	CHECK(strstr(row, "E:") && strstr(row, "HD8") && strstr(row, "8 MB disk"));

	// C: none -> CPM22 is on A, CPM31 is free: the first step takes it.
	CHECK_EQ(panel_keys(&p, "\x1b[B\x1b[B"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.drive, 2);
	CHECK_EQ(panel_keys(&p, " "), DRIVEPANEL_REDRAW);
	CHECK(p.choice[2] >= 0 && strcmp(p.images[p.choice[2]], "CPM31") == 0);
	// B: Z80TESTS -> back one: SHORT; back again skips HD8 (E) and HD4
	// (F): F83, then EMPTY. Forward three times: Z80TESTS.
	CHECK_EQ(panel_keys(&p, "\x1b[A\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK(strcmp(p.images[p.choice[1]], "SHORT") == 0);
	CHECK_EQ(panel_keys(&p, "\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK(p.choice[1] >= 0 && strcmp(p.images[p.choice[1]], "F83") == 0);
	CHECK_EQ(panel_keys(&p, "\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK(p.choice[1] >= 0 && strcmp(p.images[p.choice[1]], "EMPTY") == 0);
	CHECK_EQ(panel_keys(&p, "\x1b[C\x1b[C\x1b[C"), DRIVEPANEL_REDRAW);
	CHECK(p.choice[1] >= 0 && strcmp(p.images[p.choice[1]], "Z80TESTS") == 0);

	CHECK_EQ(panel_keys(&p, "\r"), DRIVEPANEL_APPLY);
	bool failed;
	CHECK_EQ(drivepanel_apply(&p, &failed), 1);
	CHECK(!failed);
	CHECK(strcmp(drives_name(2), "CPM31") == 0);

	// Saved: a reload gives the same.
	CHECK(drives_load_config());
	CHECK(strcmp(drives_name(2), "CPM31") == 0);
	CHECK(strcmp(drives_name(4), "HD8") == 0);

	// A bad image fails to mount.
	drivepanel_open(&p);
	p.drive = 7;
	p.choice[7] = 8; // SHORT
	CHECK_EQ(drivepanel_apply(&p, &failed), 1);
	CHECK(failed);
	CHECK(drives_name(7) == NULL);

	// Escape goes back.
	drivepanel_open(&p);
	CHECK_EQ(panel_keys(&p, "\x1b"), DRIVEPANEL_NONE);
	CHECK_EQ(drivepanel_tick(&p, DRIVEPANEL_ESC_TIMEOUT_MS), DRIVEPANEL_CANCEL);
}

static bool panel_row_has(const drivepanel_t *p, unsigned row, const char *text) {
	static screen_t s;
	screen_init(&s, 80, 0x3f, 0);
	drivepanel_draw(p, &s, 0, 0x3f, 0, 0x0c);
	char line[81] = "";
	for (unsigned c = 0; c < 80; ++c) {
		line[c] = (char)screen_glyph(&s, c, row);
	}
	bool found = strstr(line, text) != NULL;
	if (!found) {
		fprintf(stderr, "row %u: no \"%s\" in \"%s\"\n", row, text, line);
	}
	return found;
}

static void write_file(const char *path, const char *text);

// Named drive sets in the Drives panel (bd f8w.6): saved from the drives,
// picked on the Set row, removed; files in /DISKS80/SETS as DRIVES.CFG.
static void test_drive_sets(void) {
	static drivepanel_t p;
	char before[DRIVES][DRIVES_NAME_MAX + 1];
	for (unsigned d = 0; d < DRIVES; ++d) {
		strcpy(before[d], drives_name(d) ? drives_name(d) : "");
	}
	drivepanel_open(&p);
	CHECK_EQ(p.n_sets, 0);
	CHECK(panel_row_has(&p, 1, "no sets on the card yet"));
	CHECK_EQ(panel_keys(&p, "\x1b[A"), DRIVEPANEL_REDRAW); // up from A: to the Set row
	CHECK(p.on_set);
	CHECK_EQ(panel_keys(&p, "\x1b[C"), DRIVEPANEL_REDRAW);
	CHECK(p.message && strstr(p.message, "No sets"));

	// S: the drives as a set, its name typed (lower case taken as upper).
	CHECK_EQ(panel_keys(&p, "s"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_NAME);
	CHECK_EQ(panel_keys(&p, "my-2.2x\x7f"), DRIVEPANEL_REDRAW); // - and . are not taken
	CHECK(strcmp(p.name, "MY22") == 0);
	CHECK(panel_row_has(&p, DRIVEPANEL_ROWS - 4, "Save these drives as set: MY22_"));
	CHECK_EQ(panel_keys(&p, "\r"), DRIVEPANEL_SAVE_SET);
	drivepanel_save_set(&p);
	CHECK(p.message && strcmp(p.message, "Saved set MY22") == 0);
	CHECK_EQ(p.n_sets, 1);
	CHECK_EQ(p.set, 0);
	CHECK(panel_row_has(&p, 1, "MY22"));
	char images[DRIVES][DRIVES_NAME_MAX + 1];
	CHECK(drives_set_read("MY22", images));
	for (unsigned d = 0; d < DRIVES; ++d) {
		CHECK(strcmp(images[d], before[d]) == 0);
	}

	// One written on a PC: any order, lower case, blanks, an image that is
	// not on the card.
	write_file("/DISKS80/SETS/OTHER.SET", "b = cpm32\nA=CPM31\r\nC=NOSUCH\r\nH=\r\n");
	drivepanel_open(&p);
	CHECK_EQ(p.n_sets, 2);
	CHECK(strcmp(p.sets[1], "OTHER") == 0);
	CHECK_EQ(p.set, 0); // the drives are MY22
	CHECK_EQ(panel_keys(&p, "\x1b[A "), DRIVEPANEL_REDRAW); // Space steps too
	CHECK_EQ(p.set, 1);
	CHECK(strcmp(p.images[p.choice[0]], "CPM31") == 0);
	CHECK(strcmp(p.images[p.choice[1]], "CPM32") == 0);
	for (unsigned d = 2; d < DRIVES; ++d) {
		CHECK_EQ(p.choice[d], -1);
	}
	CHECK(p.message && strcmp(p.message, "1 of OTHER's disks is not on the card") == 0);
	// Changing a drive leaves the set.
	CHECK_EQ(panel_keys(&p, "\x1b[B\x1b[B\x1b[B "), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.set, -1);
	CHECK(panel_row_has(&p, 1, "the drives below are no set"));
	CHECK_EQ(panel_keys(&p, "\x1b[D"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.set, 1);
	CHECK_EQ(panel_keys(&p, "\r"), DRIVEPANEL_APPLY);
	bool failed;
	CHECK(drivepanel_apply(&p, &failed) > 0);
	CHECK(!failed);
	CHECK(strcmp(drives_name(0), "CPM31") == 0);
	CHECK(drives_name(2) == NULL);
	drivepanel_open(&p);
	CHECK_EQ(p.set, 1); // as mounted

	// R asks first; only Y removes. Delete (ESC [ 3 ~) asks too.
	CHECK_EQ(panel_keys(&p, "r"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_REMOVE);
	CHECK(panel_row_has(&p, DRIVEPANEL_ROWS - 4, "Remove set OTHER from the card? (Y/N)"));
	CHECK_EQ(panel_keys(&p, "n"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_NONE);
	CHECK_EQ(panel_keys(&p, "\x1b[3~"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_REMOVE);
	CHECK_EQ(panel_keys(&p, "Y"), DRIVEPANEL_REMOVE_SET);
	drivepanel_remove_set(&p);
	CHECK(p.message && strcmp(p.message, "Removed set OTHER") == 0);
	CHECK_EQ(p.n_sets, 1);
	CHECK_EQ(p.set, -1);
	CHECK(!fat_file_exists("/DISKS80/SETS/OTHER.SET"));
	CHECK_EQ(panel_keys(&p, "r"), DRIVEPANEL_REDRAW); // no set shown
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_NONE);

	// A name is needed; Escape closes the prompt, not the panel.
	CHECK_EQ(panel_keys(&p, "S\r"), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_NAME);
	CHECK(p.message && strstr(p.message, "1-8 letters and digits"));
	CHECK_EQ(panel_keys(&p, "\x1b"), DRIVEPANEL_REDRAW); // the message goes
	CHECK_EQ(drivepanel_tick(&p, DRIVEPANEL_ESC_TIMEOUT_MS), DRIVEPANEL_REDRAW);
	CHECK_EQ(p.prompt, DRIVEPANEL_PROMPT_NONE);
	// Saving over a set replaces it.
	CHECK_EQ(panel_keys(&p, "Smy22\r"), DRIVEPANEL_SAVE_SET);
	drivepanel_save_set(&p);
	CHECK(p.message && strcmp(p.message, "Saved set MY22 (replaced)") == 0);
	CHECK(drives_set_read("MY22", images));
	CHECK(strcmp(images[0], "CPM31") == 0);
	CHECK(!drives_set_name_ok("my22") && !drives_set_name_ok("TOOLONGNM") && !drives_set_name_ok(""));

	// Back as it was.
	CHECK(drives_set_remove("MY22"));
	drives_unmount_all();
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (before[d][0]) {
			CHECK_EQ(drives_mount(d, before[d]), DRIVES_OK);
		}
	}
	drives_save_config();
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

// WORMS (src/machine/cpm/worms.asm) on the CPM22 disk: color worms, the options,
// the screen size from a cursor position report, any key back to CP/M.
static void test_worms(void) {
	// No size report from this console: 24 x 80; a worm per color.
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

	// Monochrome: no color codes.
	clear_screen();
	CHECK(type("WORMS -D0 -M\r"));
	CHECK_EQ(run_until("\x1b[23;", 10), MACHINE_RUNNING);
	for (int i = 0; i < 20; ++i) {
		machine_run(1000);
	}
	for (char sgr[] = "\x1b[91m"; sgr[3] <= '6'; ++sgr[3]) {
		CHECK(strstr(screen, sgr) == NULL);
	}
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// -C: 256-color rainbows, each worm from its own place on the wheel
	// (worm 0 from red, worm 1 seven steps on), stepping on per head.
	clear_screen();
	CHECK(type("WORMS -D 0 -N 2 -C\r"));
	CHECK_EQ(run_until("\x1b[38;5;154m", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "\x1b[38;5;202m") != NULL);
	for (int i = 0; i < 20; ++i) {
		machine_run(1000);
	}
	CHECK(strstr(screen, "\x1b[38;5;208m") != NULL);
	CHECK(strstr(screen, "\x1b[91m") == NULL);
	CHECK(type("x"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("WORMS -D0 -C -M\r")); // -M still wins
	CHECK_EQ(run_until("\x1b[23;", 10), MACHINE_RUNNING);
	for (int i = 0; i < 20; ++i) {
		machine_run(1000);
	}
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
	while (*text) {
		host_platform.console_out((uint8_t)*text++);
	}
}

static const char *card_info(void) {
	return "RAM, 40 MB";
}

static unsigned flushes;
static unsigned busies; // monitor_platform_t.busy: the hang check fed during long writes (bd fhu.3)

static void flush(void) {
	++flushes;
}

static void busy(void) {
	++busies;
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
	for (uint32_t done = 0; done <= 128 * 1024; done += 512) {
		progress(ctx, done, 128 * 1024);
	}
	bool ok = firmware_first(path) == 'S';
	snprintf(info, n, "%s", ok ? "" : "flash verify failed");
	return ok;
}

static void firmware_commit(void) {
	++commits;
}

static const monitor_platform_t monitor_platform = {.write = console_text, .card_info = card_info, .flush = flush,
	.busy = busy};

// The whole menu was shown, up to its prompt (lpt.8: it was cut short).
static bool menu_shown(void) {
	return strstr(screen, "C  check the card again\r\n") && strstr(screen, "F12 SET-UP (Drives, Mode, firmware update)\r\n> ") &&
		screen_len >= 2 && strcmp(screen + screen_len - 2, "> ") == 0;
}

static monitor_action_t keys(const char *s) {
	monitor_action_t a = MONITOR_NONE;
	while (*s) {
		a = monitor_key((uint8_t)*s++, 0);
	}
	return a;
}

// A card with no file system: the monitor points at F, which asks for
// FORMAT typed out, formats it to suit its size and mounts it; anything
// else typed, or Esc, leaves the card alone.
static void test_monitor_format(void) {
	drives_unmount_all();
	f_unmount("");
	diskio_ram_init(VOLUME_SECTORS); // all zeros
	clear_screen();
	machine_init(&host_platform);
	monitor_start(&monitor_platform, false, 0);
	CHECK(strstr(screen, "RAM, 40 MB, no FAT file system (error 13): F formats it") != NULL);
	CHECK(strstr(screen, "F       format the SD card (erases it)") != NULL);
	CHECK(menu_shown());

	clear_screen();
	CHECK_EQ(keys("f"), MONITOR_NONE);
	CHECK(strstr(screen, "Format the SD card (RAM, 40 MB) as FAT12/16?") != NULL);
	CHECK(strstr(screen, "Type FORMAT and Return") != NULL);
	CHECK_EQ(keys("forma\r"), MONITOR_NONE);
	CHECK(strstr(screen, "Not formatted.") != NULL);
	CHECK(menu_shown());
	CHECK(strstr(screen, "no FAT file system") != NULL);
	clear_screen();
	CHECK_EQ(keys("fformat\x1b"), MONITOR_NONE);
	CHECK(strstr(screen, "Formatting") == NULL);
	CHECK(menu_shown());

	// The scratch buffer held elsewhere: nothing written.
	uint8_t *held = scratch_acquire();
	clear_screen();
	CHECK_EQ(keys("fformat\r"), MONITOR_NONE);
	CHECK(strstr(screen, "Busy.") != NULL);
	CHECK(strstr(screen, "no FAT file system") != NULL);
	scratch_release(held);

	clear_screen();
	flushes = 0;
	CHECK_EQ(keys("fformat\r"), MONITOR_NONE);
	CHECK(strstr(screen, "FORMAT\r\nFormatting (a large card takes a while)... done\r\n") != NULL);
	CHECK(flushes >= 1);
	CHECK(strstr(screen, "I installs the built-in disks.") != NULL);
	CHECK(strstr(screen, "SD card:  RAM, 40 MB, FAT16") != NULL);
	CHECK(menu_shown());

	// Formatted again with disks on it: they are gone, and so is drive A.
	CHECK_EQ(keys("i"), MONITOR_NONE);
	CHECK(drives_name(0) != NULL);
	clear_screen();
	CHECK_EQ(keys("fFORMAT\r"), MONITOR_NONE);
	CHECK(strstr(screen, "done") != NULL);
	CHECK(drives_name(0) == NULL);
	CHECK(!fat_file_exists("/DISKS80/CPM22.DSK"));
	CHECK(strstr(screen, "R       resume") == NULL);
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
	busies = 0;
	CHECK_EQ(keys("i"), MONITOR_NONE);
	CHECK(strstr(screen, "CPM22    written") != NULL);
	CHECK(busies >= 2 * (8160 * 1024 / 4096)); // CPM32 and F83 alone, 4 KB chunks at most
	CHECK(strstr(screen, "Set CPM22 written") != NULL);
	CHECK(strstr(screen, "Set CPM3  written") != NULL);
	char images[DRIVES][DRIVES_NAME_MAX + 1];
	CHECK(drives_set_read("CPM3", images));
	CHECK(strcmp(images[0], "CPM31") == 0 && strcmp(images[1], "CPM32") == 0 && images[2][0] == '\0');
	CHECK(drives_set_read("CPM22", images));
	CHECK(strcmp(images[0], "CPM22") == 0 && strcmp(images[1], "Z80TESTS") == 0);
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
	busies = 0;
	CHECK_EQ(keys("nbig\r3"), MONITOR_NONE);
	CHECK(strstr(screen, "Writing BIG.DSK (8160 KB)... ............................... done") != NULL);
	CHECK(flushes >= 31);
	CHECK(busies >= 8160 * 1024 / 4096); // every 4 KB chunk
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
	CHECK(strstr(screen, "Set CPM3  already there") != NULL);

	// A changed or removed starter set: kept as it is, or written again,
	// even when the disks are replaced.
	CHECK(drives_set_read("CPM3", images));
	strcpy(images[2], "EMPTY");
	CHECK(drives_set_write("CPM3", images));
	CHECK(drives_set_remove("CPM22"));
	clear_screen();
	CHECK_EQ(keys("iy"), MONITOR_NONE);
	CHECK(strstr(screen, "Set CPM22 written") != NULL);
	CHECK(strstr(screen, "Set CPM3  already there") != NULL);
	CHECK(drives_set_read("CPM3", images));
	CHECK(strcmp(images[2], "EMPTY") == 0);

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
static bool panel_card, panel_stick;
static unsigned panel_shows, panel_unmounts, panel_commit_unmounts;

static bool panel_mount(char *info, size_t n) {
	if (!panel_card) {
		snprintf(info, n, "no SD card found");
	}
	return panel_card;
}

static bool panel_stick_mounted(void) {
	return panel_stick;
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
	for (unsigned c = 0; c < 80; ++c) {
		line[c] = (char)screen_glyph(&panel_shown, c, row);
	}
	line[80] = 0;
	return strstr(line, text) != NULL;
}

static const fwpanel_platform_t panel_platform = {.mount = panel_mount, .stick = panel_stick_mounted,
	.check = firmware_check,
	.stage = firmware_stage, .unmount = panel_unmount, .commit = firmware_commit, .show = panel_show};

static bool panel_row(const fwpanel_t *p, unsigned row, const char *text) {
	static screen_t s;
	screen_init(&s, 80, 7, 0);
	fwpanel_draw(p, &s, 0, 7, 0, 4);
	char line[81];
	for (unsigned c = 0; c < 80; ++c) {
		line[c] = (char)screen_glyph(&s, c, row);
	}
	line[80] = 0;
	return strstr(line, text) != NULL;
}

static void test_firmware_panel(void) {
	static fwpanel_t p;
	FILINFO info;
	stages = commits = panel_unmounts = panel_commit_unmounts = 0;

	// No card (and no stick): says so, any key goes back, nothing to let go.
	panel_card = panel_stick = false;
	fwpanel_open(&p);
	CHECK(panel_row(&p, 0, "SET-UP  Firmware update"));
	CHECK(panel_row(&p, 2, "Looking for FIRMWARE.UF2"));
	CHECK_EQ(fwpanel_input(&p, 'y'), FWPANEL_NONE); // busy
	CHECK_EQ(fwpanel_run(&p, &panel_platform), FWPANEL_REDRAW);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 2, "No SD card or USB stick to update from:"));
	CHECK(panel_row(&p, 3, "no SD card found"));
	CHECK_EQ(fwpanel_input(&p, 'x'), FWPANEL_CANCEL);
	fwpanel_close(&p, &panel_platform);
	CHECK_EQ(panel_unmounts, 0);

	// No file: says where it looked; leaving lets the card go.
	panel_card = true;
	f_unlink(FWPANEL_FILE);
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 2, "No FIRMWARE.UF2 to install:"));
	CHECK(panel_row(&p, 3, "none in the root of the SD card (and no USB stick)"));
	fwpanel_close(&p, &panel_platform);
	CHECK_EQ(panel_unmounts, 1);

	// A file the check refuses.
	write_file(FWPANEL_FILE, "BAD");
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 2, "FIRMWARE.UF2 on the SD card cannot be installed:"));
	CHECK(panel_row(&p, 3, "not a UF2 file"));
	fwpanel_close(&p, &panel_platform);

	// Declined with N, or Escape.
	write_file(FWPANEL_FILE, "GOOD");
	for (const char *k = "N\x1b"; *k; ++k) {
		fwpanel_open(&p);
		fwpanel_run(&p, &panel_platform);
		CHECK_EQ(p.state, FWPANEL_ASK);
		CHECK(panel_row(&p, 2, "Install it from the SD card and restart (Y/N)?"));
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
	CHECK(shown_row(4, "Afterwards, if USB devices do not work, power the board off and on."));
	CHECK_EQ(commits, 1);
	CHECK_EQ(panel_commit_unmounts, 1);
	CHECK_EQ(f_stat(FWPANEL_FILE, &info), FR_NO_FILE);
	CHECK_EQ(f_stat(FWPANEL_FILE_OLD, &info), FR_OK);
	CHECK_EQ(info.fsize, 6);
	f_unlink(FWPANEL_FILE_OLD);
}

// The firmware panel with a USB stick as volume 1 (qur.27): the file on the
// stick alone, on neither, on both (S or U picks), installed from the stick.
static void test_firmware_panel_stick(void) {
	static fwpanel_t p;
	static FATFS usb_fs;
	static uint8_t work[4096];
	FILINFO info;
	diskio_ram_init_drive(1, 2048);
	const MKFS_PARM opt = {.fmt = FM_FAT | FM_SFD};
	CHECK_EQ(f_mkfs("1:", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&usb_fs, "1:", 1), FR_OK);
	stages = commits = panel_unmounts = panel_commit_unmounts = 0;
	panel_stick = true;
	f_unlink(FWPANEL_FILE);

	// On neither.
	panel_card = true;
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_FAILED);
	CHECK(panel_row(&p, 3, "none in the root of the SD card or the USB stick"));
	fwpanel_close(&p, &panel_platform);
	panel_card = false;
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK(panel_row(&p, 3, "none in the root of the USB stick (and no SD card)"));
	fwpanel_close(&p, &panel_platform);

	// On the stick alone, no card: checked and offered from the stick.
	panel_unmounts = 0;
	write_file(fwpanel_path(FWPANEL_USB), "BAD");
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK(panel_row(&p, 2, "FIRMWARE.UF2 on the USB stick cannot be installed:"));
	fwpanel_close(&p, &panel_platform);
	write_file(fwpanel_path(FWPANEL_USB), "GOOD");
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_ASK);
	CHECK_EQ(p.source, FWPANEL_USB);
	CHECK(panel_row(&p, 2, "Install it from the USB stick and restart (Y/N)?"));
	CHECK_EQ(fwpanel_input(&p, 'n'), FWPANEL_CANCEL);
	fwpanel_close(&p, &panel_platform);
	CHECK_EQ(panel_unmounts, 0); // no card was mounted

	// On both: S or U picks (other keys do nothing), N or Escape goes back.
	panel_card = true;
	write_file(FWPANEL_FILE, "GOOD");
	for (const char *k = "nN\x1b"; *k; ++k) {
		fwpanel_open(&p);
		fwpanel_run(&p, &panel_platform);
		CHECK_EQ(p.state, FWPANEL_PICK);
		CHECK(panel_row(&p, 2, "FIRMWARE.UF2 is on both the SD card and the USB stick."));
		CHECK(panel_row(&p, 6, "S SD card   U USB stick"));
		CHECK_EQ(fwpanel_input(&p, 'y'), FWPANEL_NONE);
		CHECK_EQ(fwpanel_input(&p, (uint8_t)*k), FWPANEL_CANCEL);
		fwpanel_close(&p, &panel_platform);
	}
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(fwpanel_input(&p, 's'), FWPANEL_WORK);
	CHECK_EQ(fwpanel_input(&p, 'u'), FWPANEL_NONE); // busy checking
	CHECK_EQ(fwpanel_run(&p, &panel_platform), FWPANEL_REDRAW);
	CHECK_EQ(p.state, FWPANEL_ASK);
	CHECK(panel_row(&p, 2, "from the SD card"));
	fwpanel_close(&p, &panel_platform);

	// Installed from the stick: renamed there, the card's file left alone.
	write_file(fwpanel_path(FWPANEL_USB), "STAGES");
	fwpanel_open(&p);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(fwpanel_input(&p, 'U'), FWPANEL_WORK);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(p.state, FWPANEL_ASK);
	CHECK(panel_row(&p, 2, "from the USB stick"));
	CHECK_EQ(fwpanel_input(&p, 'y'), FWPANEL_WORK);
	fwpanel_run(&p, &panel_platform);
	CHECK_EQ(stages, 1);
	CHECK_EQ(commits, 1);
	CHECK_EQ(panel_commit_unmounts, 1);
	CHECK_EQ(f_stat("1:" FWPANEL_FILE, &info), FR_NO_FILE);
	CHECK_EQ(f_stat("1:" FWPANEL_FILE_OLD, &info), FR_OK);
	CHECK_EQ(info.fsize, 6);
	CHECK_EQ(f_stat(FWPANEL_FILE, &info), FR_OK);
	CHECK_EQ(f_stat(FWPANEL_FILE_OLD, &info), FR_NO_FILE);

	f_unlink(FWPANEL_FILE);
	panel_stick = false;
	CHECK_EQ(f_unmount("1:"), FR_OK);
	diskio_ram_free_drive(1);
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
	if (name) {
		memcpy(xbuf, name, XFER_NAME);
	}
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
	for (unsigned i = 0; i < n; ++i) {
		z80.code[z80.n++] = (uint8_t)va_arg(ap, int);
	}
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
	extern uint8_t bnk0[65536];
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
	for (unsigned i = 0; i < XFER_RECORD; ++i) {
		bnk0[rec + i] = (uint8_t)(i ^ 0x5a);
	}
	memset(bnk0 + st, 0xff, 16);
	machine_reset(); // from the code above
	unsigned slices = 0;
	while (machine_run(10000) == MACHINE_RUNNING && ++slices < 100) {
	}
	CHECK(strstr(machine_stop_reason(), "HALT with interrupts off") != NULL);
	static const uint8_t want[] = {XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_EOF,
		XFER_ST_OK, XFER_ST_OK, XFER_ST_OK, XFER_ST_OK};
	for (unsigned i = 0; i < sizeof want; ++i) {
		CHECK_EQ(bnk0[st + i], want[i]);
	}
	CHECK(memcmp(bnk0 + back, bnk0 + rec, XFER_RECORD) == 0);
	CHECK(memcmp(machine_bank(1) + banked, bnk0 + rec, XFER_RECORD) == 0);
	// The listing entry: the name and the size (128).
	CHECK(memcmp(bnk0 + name, "PORT    BIN", XFER_NAME) == 0);
	CHECK(bnk0[name + 11] == 128 && bnk0[name + 12] == 0 && bnk0[name + 13] == 0 && bnk0[name + 14] == 0);
	CHECK_EQ(fat_size("/XFER/PORT.BIN"), XFER_RECORD);
	// A reset closes what the program left open.
	CHECK_EQ(xcmd(XFER_OPEN, "PORT    BIN"), XFER_ST_OK);
	machine_reset();
	CHECK_EQ(xcmd(XFER_READ, NULL), XFER_ST_BAD);
}

// A FAT file of the given bytes.
static void put_fat_file(const char *path, const void *data, unsigned size) {
	FIL f;
	UINT put;
	CHECK_EQ(f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE), FR_OK);
	CHECK_EQ(f_write(&f, data, size, &put), FR_OK);
	CHECK_EQ(put, size);
	f_close(&f);
}

// The FAT file at path, if it holds exactly size bytes equal to data
// (then 0x1A padding up to pad_to, if larger).
static bool fat_file_is(const char *path, const void *data, unsigned size, unsigned pad_to) {
	static uint8_t got[32768];
	FIL f;
	UINT n;
	if (f_open(&f, path, FA_READ) != FR_OK) {
		return false;
	}
	bool ok = f_read(&f, got, sizeof got, &n) == FR_OK && n == (pad_to > size ? pad_to : size) &&
		memcmp(got, data, size) == 0;
	for (unsigned i = size; ok && i < n; ++i) {
		ok = got[i] == 0x1a;
	}
	f_close(&f);
	return ok;
}

// Run a CP/M command line to the next prompt; true if its output has want.
static bool cpm(const char *line, const char *want) {
	clear_screen();
	CHECK(type(line));
	CHECK(type("\r"));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);
	bool found = strstr(screen, want) != NULL;
	if (!found) {
		fprintf(stderr, "%s: no \"%s\" in:\n%s\n", line, want, screen);
	}
	return found;
}

// XLS and XPIP (src/machine/cpm) under CP/M, against /XFER on the volume.
static void test_xfer_tools(void) {
	static char text[300];
	static uint8_t binary[1000];
	for (unsigned i = 0; i < sizeof text; ++i) {
		text[i] = i % 30 == 29 ? '\n' : i % 30 == 28 ? '\r' : (char)('a' + i % 26);
	}
	for (unsigned i = 0; i < sizeof binary; ++i) {
		binary[i] = (uint8_t)(i * 13 + i / 256); // 0x1A included
	}
	drives_unmount_all();
	xfer_reset();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("EMPTY"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "EMPTY"), DRIVES_OK);
	// /XFER with just these (test_xfer_port left a file there).
	f_unlink("/XFER/PORT.BIN");
	f_mkdir("/XFER");
	put_fat_file("/XFER/HELLO.TXT", text, sizeof text);
	put_fat_file("/XFER/DATA.BIN", binary, sizeof binary);
	put_fat_file("/XFER/A_B.TXT", "x", 1); // not a CP/M name
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// On the disk; z80pack's XDIR is not.
	CHECK(cpm("DIR X*.COM", "XLS      COM : XPIP     COM"));
	CHECK(cpm("DIR XDIR.COM", "NO FILE"));

	// Listing.
	CHECK(cpm("XLS", "DATA.BIN           1000"));
	CHECK(strstr(screen, "HELLO.TXT           300") != NULL);
	CHECK(strstr(screen, "2 files") != NULL);
	CHECK(strstr(screen, "A_B") == NULL);
	CHECK(cpm("XLS SD:*.TXT", "1 file\r\n"));
	CHECK(cpm("XLS NONE.*", "No files"));
	CHECK(cpm("XLS B:", "Usage: XLS"));
	CHECK(cpm("XLS USB:", "no USB stick"));

	// Into CP/M: names kept, the last record padded.
	CHECK(cpm("XPIP B:=SD:*.*", "2 copied"));
	CHECK(strstr(screen, "HELLO.TXT\r\n") != NULL && strstr(screen, "DATA.BIN\r\n") != NULL);
	CHECK(cpm("DIR B:", "HELLO    TXT"));
	CHECK(cpm("TYPE B:HELLO.TXT", "abcdefghijklmnopqrstuvwxyzab\r\n"));
	// Back out: whole records, or cut at the ^Z with [T]; renamed.
	CHECK(cpm("XPIP SD:BACK.BIN=B:DATA.BIN", "1 copied"));
	CHECK(fat_file_is("/XFER/BACK.BIN", binary, sizeof binary, 1024));
	CHECK(cpm("XPIP SD:HELLO2.TXT=B:HELLO.TXT[T]", "1 copied"));
	CHECK(fat_file_is("/XFER/HELLO2.TXT", text, sizeof text, 0));
	CHECK(cpm("xpip sd:hello3.txt=b:hello.txt [ t ]", "1 copied")); // lower case, blanks
	CHECK(fat_file_is("/XFER/HELLO3.TXT", text, sizeof text, 0));
	// A wildcard export replaces what is there, as PIP does.
	CHECK(cpm("XPIP SD:=B:*.TXT", "1 copied"));
	CHECK(fat_file_is("/XFER/HELLO.TXT", text, sizeof text, 384));
	// One file in under a new name, on the current drive.
	CHECK(cpm("XPIP COPY.TXT=SD:HELLO2.TXT", "1 copied"));
	CHECK(cpm("TYPE COPY.TXT", "abcdefghijklmnopqrstuvwxyzab\r\n"));
	// And again: replaced, not duplicated.
	CHECK(cpm("XPIP COPY.TXT=SD:HELLO2.TXT", "1 copied"));
	CHECK(cpm("DIR COPY.TXT", "COPY     TXT"));
	CHECK(strstr(strstr(screen, "COPY     TXT") + 1, "COPY     TXT") == NULL);

	// Mistakes.
	CHECK(cpm("XPIP B:=SD:NOSUCH.TXT", "No file"));
	CHECK(cpm("XPIP SD:=B:NOSUCH.TXT", "No file"));
	CHECK(cpm("XPIP B:X.TXT=SD:*.TXT", "Usage: XPIP")); // many files to one name
	CHECK(cpm("XPIP A:=B:*.*", "Usage: XPIP"));         // no SD: or USB: side
	CHECK(cpm("XPIP SD:=SD:*.*", "Usage: XPIP"));
	CHECK(cpm("XPIP B:=SD:*.TXT[Q]", "Usage: XPIP"));
	CHECK(cpm("XPIP", "Usage: XPIP"));
	CHECK(cpm("XPIP B:=USB:*.*", "no USB stick"));
	CHECK(cpm("XPIP SD:BAD_NAME.TXT=B:HELLO.TXT", "bad name"));

	// XLS pauses after every 22 lines: any key goes on, Q stops.
	for (unsigned i = 0; i < 30; ++i) {
		char path[32];
		snprintf(path, sizeof path, "/XFER/P%02u.LST", i);
		put_fat_file(path, "x", 1);
	}
	clear_screen();
	CHECK(type("XLS *.LST\r"));
	CHECK_EQ(run_until("-- more", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "P21.LST") != NULL);
	CHECK(strstr(screen, "P22.LST") == NULL);
	clear_screen();
	CHECK(type(" "));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "P22.LST") != NULL && strstr(screen, "P29.LST") != NULL);
	CHECK(strstr(screen, "30 files") != NULL);
	CHECK(strstr(screen, "-- more") == NULL); // 8 lines: no second pause
	clear_screen();
	CHECK(type("XLS *.LST\r"));
	CHECK_EQ(run_until("-- more", 30), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("q"));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "P22.LST") == NULL && strstr(screen, "files") == NULL);
	clear_screen(); // the listing it left was closed: a new one works
	CHECK(type("XLS *.LST\r"));
	CHECK_EQ(run_until("-- more", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "P00.LST") != NULL);
	CHECK(type("q"));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);

	// XERA: one file; wildcards ask first (N leaves them, Y erases).
	CHECK(cpm("DIR XERA.COM", "XERA     COM"));
	CHECK(cpm("XERA SD:P05.LST", "P05.LST\r\n1 erased"));
	CHECK(!fat_file_exists("/XFER/P05.LST"));
	clear_screen();
	CHECK(type("XERA P0?.LST\r"));
	CHECK_EQ(run_until("(Y/N)? ", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "9 match") != NULL); // P00-P09 but P05
	CHECK(type("n"));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);
	CHECK(fat_file_exists("/XFER/P01.LST"));
	clear_screen();
	CHECK(type("XERA P0?.LST\r"));
	CHECK_EQ(run_until("(Y/N)? ", 30), MACHINE_RUNNING);
	CHECK(type("y"));
	CHECK_EQ(run_until("\r\nA>", 30), MACHINE_RUNNING);
	CHECK(strstr(screen, "P09.LST\r\n9 erased") != NULL);
	CHECK(!fat_file_exists("/XFER/P01.LST") && !fat_file_exists("/XFER/P09.LST"));
	CHECK(fat_file_exists("/XFER/P10.LST"));
	CHECK(cpm("XERA NOSUCH.TXT", "No file"));
	CHECK(cpm("XERA", "Usage: XERA"));            // no name: no erasing everything
	CHECK(cpm("XERA B:HELLO.TXT", "Usage: XERA")); // CP/M files: ERA
	CHECK(cpm("XERA USB:*.*", "no USB stick"));

	// USB: a stick as volume 1 (here the RAM disk's second drive).
	static FATFS usb_fs;
	static uint8_t work[4096];
	diskio_ram_init_drive(1, 2048);
	const MKFS_PARM opt = {.fmt = FM_FAT | FM_SFD};
	CHECK_EQ(f_mkfs("1:", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&usb_fs, "1:", 1), FR_OK);
	CHECK(cpm("XLS USB:", "No files"));
	CHECK(cpm("XPIP USB:=B:*.TXT[T]", "1 copied"));
	CHECK(fat_file_is("1:/XFER/HELLO.TXT", text, sizeof text, 0));
	CHECK(cpm("XLS USB:", "HELLO.TXT           300"));
	CHECK(strstr(screen, "1 file\r\n") != NULL);
	CHECK(cpm("XPIP B:FROMUSB.TXT=USB:HELLO.TXT", "1 copied"));
	CHECK(cpm("TYPE B:FROMUSB.TXT", "abcdefghijklmnopqrstuvwxyzab\r\n"));
	CHECK(cpm("XPIP USB:=SD:*.*", "Usage: XPIP")); // card to stick: not a CP/M copy
	CHECK(cpm("XERA USB:HELLO.TXT", "1 erased"));
	CHECK(!fat_file_exists("1:/XFER/HELLO.TXT"));
	CHECK(fat_file_exists("/XFER/HELLO.TXT")); // not the card's
	CHECK(cpm("XLS USB:", "No files"));
	// The stick goes.
	CHECK_EQ(f_unmount("1:"), FR_OK);
	diskio_ram_free_drive(1);
	CHECK(cpm("XLS USB:", "no USB stick"));
	CHECK(cpm("XLS HELLO.TXT", "HELLO.TXT")); // the card is as it was
	drives_unmount_all();
}

// Port 12, the hardware information device (bd 1ls.9), in raw Z80 bytes
// as test_xfer_port: the ID, an item read past its end, a record, an
// unknown item; then without the platform's hwinfo, 0xFF as from no device.
static void hwinfo_program(uint16_t at) {
	extern uint8_t bnk0[65536];
	z80.n = 0;
	emit(1, 0xf3); // DI
	static const uint8_t items[] = {HWINFO_ID, HWINFO_BOARD, HWINFO_RTC, 99};
	for (unsigned i = 0; i < sizeof items; ++i) {
		uint16_t a = (uint16_t)(at + i * 16);
		emit(4, 0x3e, items[i], 0xd3, 12); // LD A,item; OUT (12),A
		emit(5, 0x21, a & 0xff, a >> 8, 0x06, 16); // LD HL,a; LD B,16
		emit(6, 0xdb, 12, 0x77, 0x23, 0x10, 0xfa); // IN A,(12); LD (HL),A; INC HL; DJNZ back
	}
	emit(1, 0x76); // HALT
	CHECK(z80.n <= sizeof z80.code);
	memcpy(bnk0 + 0xff00, z80.code, z80.n);
	memset(bnk0 + at, 0x55, 64);
	machine_reset();
	unsigned slices = 0;
	while (machine_run(10000) == MACHINE_RUNNING && ++slices < 100) {
	}
	CHECK(strstr(machine_stop_reason(), "HALT with interrupts off") != NULL);
}

static void test_hwinfo_port(void) {
	extern uint8_t bnk0[65536];
	const uint16_t at = 0xf000;
	machine_init(&host_platform);
	hwinfo_program(at);
	static const uint8_t id[16] = {'H', 'W', HWINFO_VERSION};
	CHECK(memcmp(bnk0 + at, id, 16) == 0);
	static const uint8_t board[16] = "host-board";
	CHECK(memcmp(bnk0 + at + 16, board, 16) == 0); // its 0, then 0s
	CHECK(memcmp(bnk0 + at + 32, "\x01Real-time clock", 16) == 0); // the first 16 bytes
	static const uint8_t none[16];
	CHECK(memcmp(bnk0 + at + 48, none, 16) == 0);

	machine_platform_t old = host_platform;
	old.hwinfo = NULL;
	machine_init(&old);
	hwinfo_program(at);
	for (unsigned i = 0; i < 64; ++i) {
		CHECK_EQ(bnk0[at + i], 0xff);
	}
	machine_init(&host_platform);

	// Records are cut to HWINFO_MAX, the name and the detail still ending
	// in 0.
	uint8_t buf[HWINFO_MAX];
	char longer[200];
	memset(longer, 'x', sizeof longer - 1);
	longer[sizeof longer - 1] = 0;
	CHECK_EQ(hwinfo_record(buf, HWINFO_ST_PRESENT, "Name", longer), HWINFO_MAX);
	CHECK(buf[0] == HWINFO_ST_PRESENT && memcmp(buf + 1, "Name", 5) == 0);
	CHECK(buf[6] == 'x' && buf[HWINFO_MAX - 2] == 'x' && buf[HWINFO_MAX - 1] == 0);
	CHECK_EQ(hwinfo_record(buf, HWINFO_ST_PRESENT, longer, "detail"), HWINFO_MAX);
	CHECK(buf[HWINFO_MAX - 3] == 'x' && buf[HWINFO_MAX - 2] == 0 && buf[HWINFO_MAX - 1] == 0); // the detail cut to nothing
	CHECK_EQ(hwinfo_text(buf, longer), HWINFO_MAX);
	CHECK(buf[HWINFO_MAX - 2] == 'x' && buf[HWINFO_MAX - 1] == 0);
}

// ---- the clock (ports 65/66; bd 1ls.6.1) ----

// Run raw Z80 bytes from FF00H to their HALT.
static void run_program(void) {
	extern uint8_t bnk0[65536];
	emit(1, 0x76); // HALT
	CHECK(z80.n <= sizeof z80.code);
	memcpy(bnk0 + 0xff00, z80.code, z80.n);
	machine_reset();
	unsigned slices = 0;
	while (machine_run(10000) == MACHINE_RUNNING && ++slices < 100) {
	}
	CHECK(strstr(machine_stop_reason(), "HALT with interrupts off") != NULL);
}

static void clock_write(uint8_t reg, uint8_t value) {
	emit(8, 0x3e, reg, 0xd3, 65, 0x3e, value, 0xd3, 66); // LD A,reg; OUT (65),A; LD A,v; OUT (66),A
}

// Registers 0-7 into memory at `at`.
static void clock_read_all(uint16_t at) {
	for (unsigned r = 0; r < 8; ++r) {
		uint16_t a = (uint16_t)(at + r);
		emit(7, 0x3e, r, 0xd3, 65, 0xdb, 66, 0x32); // LD A,r; OUT (65),A; IN A,(66); LD (a),A
		emit(2, a & 0xff, a >> 8);
	}
}

static void test_clock_ports(void) {
	extern uint8_t bnk0[65536];
	const uint16_t at = 0xf000;
	machine_init(&host_platform);
	unsigned sets = host_time_sets;
	// 22.10.2026 (day 17827 = 45A3H) 13:45:30, as CP/M 3's BIOS writes it:
	// the day number, then hours, minutes and seconds in BCD.
	z80.n = 0;
	emit(1, 0xf3); // DI
	clock_write(3, 0xa3);
	clock_write(4, 0x45);
	clock_write(2, 0x13);
	clock_write(1, 0x45);
	clock_write(0, 0x30);
	// Ignored: out of range, not BCD, the date registers.
	clock_write(2, 0x24);
	clock_write(1, 0x5a);
	clock_write(6, 0x01);
	clock_read_all(at);
	emit(4, 0x3e, 255, 0xd3, 65); // binary
	clock_read_all((uint16_t)(at + 8));
	emit(4, 0x3e, 255, 0xd3, 65); // BCD again
	run_program();
	CHECK_EQ(host_time_sets - sets, 4); // the write to 3 only waits for 4's
	CHECK_EQ((host_time_offset_us + host_clock_us()) / 1000000u, 17827u * DATETIME_DAY + 13 * 3600 + 45 * 60 + 30);
	// The year is 126 (- 1900), in BCD only its last two digits, as rtc80.c's.
	static const uint8_t bcd[8] = {0x30, 0x45, 0x13, 0xa3, 0x45, 0x22, 0x09, 0x26};
	static const uint8_t binary[8] = {30, 45, 13, 0xa3, 0x45, 22, 9, 126};
	CHECK(memcmp(bnk0 + at, bcd, 8) == 0);
	CHECK(memcmp(bnk0 + at + 8, binary, 8) == 0);

	// A day number past the count's end, and day 0, are ignored.
	z80.n = 0;
	emit(1, 0xf3);
	clock_write(3, 0x2f); // 49711 = C22FH
	clock_write(4, 0xc2);
	clock_write(3, 0x00);
	clock_write(4, 0x00);
	run_program();
	CHECK_EQ(host_time_sets - sets, 4);

	// Without time_set the clock cannot be set; without time_get it reads
	// 1.1.2026 plus the time since power-on.
	machine_platform_t old = host_platform;
	old.time_get = NULL;
	old.time_set = NULL;
	machine_init(&old);
	z80.n = 0;
	emit(1, 0xf3);
	clock_write(4, 0x45);
	clock_read_all(at);
	run_program();
	CHECK_EQ(host_time_sets - sets, 4);
	// (The host's clock counts from its boot.)
	unsigned days = bnk0[at + 3] | bnk0[at + 4] << 8;
	unsigned want = (unsigned)((DATETIME_DEFAULT_DAYS + host_clock_us() / 1000000u / DATETIME_DAY) & 0xffff);
	CHECK(days == want || days + 1 == want);
	machine_init(&host_platform);
}

// HWINFO.COM (src/machine/cpm/hwinfo.asm, bd 1ls.10) under CP/M 2.2 and 3,
// with the host's records (machine_host.c), and on a machine without port 12.
static void hwinfo_report_checks(void) {
	CHECK(strstr(screen, "Firmware         Host test firmware\r\n") != NULL);
	CHECK(strstr(screen, "Board            host-board\r\n") != NULL);
	CHECK(strstr(screen, "CPU              yes      RP2350A at 400 MHz, 4 MB flash\r\n") != NULL);
	CHECK(strstr(screen, "PSRAM            missing  none answered on CS1 (GP8)\r\n") != NULL);
	CHECK(strstr(screen, "USB stick        no       none\r\n") != NULL);
	CHECK(strstr(screen, "Real-time clock  yes      DS3231 on I2C1: GP10 SDA, GP11 SCL\r\n") != NULL);
	CHECK(strstr(screen, "RGB status LED   planned  GP21: not supported yet\r\n") != NULL);
	CHECK(strstr(screen, "Future thing     ?        a status HWINFO does not know\r\n") != NULL);
	// Wrapped at 79 columns, the rest under the detail column.
	CHECK(strstr(screen, "SD card          yes      SDHC/SDXC, 29.7 GB; SPI0: GP4 MISO, GP5 CS, GP6 SCK,\r\n"
		"                          GP7 MOSI\r\n") != NULL);
	// In item order.
	const char *cpu = strstr(screen, "CPU "), *rtc = strstr(screen, "Real-time"), *rgb = strstr(screen, "RGB status"),
		*future = strstr(screen, "Future");
	CHECK(cpu && rtc && rgb && future && cpu < rtc && rtc < rgb && rgb < future);
}

static void test_hwinfo_tool(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("HWINFO", "Real-time clock"));
	hwinfo_report_checks();
	CHECK(strstr(screen, "No hardware information") == NULL);

	machine_platform_t old = host_platform;
	old.hwinfo = NULL;
	clear_screen();
	machine_init(&old);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("HWINFO", "No hardware information here"));
	CHECK(strstr(screen, "Firmware") == NULL);
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// A page at a time: 23 lines, the command line's first, then
	// -- More -- on the 24th; a key clears it for the next 23.
	host_hwinfo_extra = 50;
	clear_screen();
	CHECK(type("HWINFO\r"));
	CHECK_EQ(run_until("-- More --", 10), MACHINE_RUNNING);
	unsigned lines = 0;
	for (const char *p = screen; (p = strstr(p, "\r\n")) != NULL; p += 2) {
		++lines;
	}
	CHECK_EQ(lines, 23); // the command's echo and 22 of HWINFO's
	CHECK(strstr(screen, "Extra 11 ") != NULL && strstr(screen, "Extra 12 ") == NULL);
	clear_screen();
	CHECK(type(" "));
	CHECK_EQ(run_until("-- More --", 10), MACHINE_RUNNING);
	CHECK(strncmp(screen, "\r          \r", 12) == 0); // the prompt erased
	lines = 0;
	for (const char *p = screen; (p = strstr(p, "\r\n")) != NULL; p += 2) {
		++lines;
	}
	CHECK_EQ(lines, 23);
	CHECK(strstr(screen, "Extra 12 ") != NULL && strstr(screen, "Extra 34 ") != NULL && strstr(screen, "Extra 35 ") == NULL);
	// Q stops it.
	clear_screen();
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Extra 35") == NULL && strstr(screen, "Future") == NULL);
	// The last page ends without one.
	clear_screen();
	CHECK(type("HWINFO\r"));
	CHECK_EQ(run_until("-- More --", 10), MACHINE_RUNNING);
	CHECK(type("\r"));
	CHECK_EQ(run_until("-- More --", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("\r"));
	CHECK_EQ(run_until("\r\nA>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Future thing") != NULL && strstr(screen, "-- More --") == NULL);
	host_hwinfo_extra = 0;
	drives_unmount_all();
}

// ---- the AY-3-8910 (D8h/D0h) and the tick (68/69; bd 1ls.18.4) ----

// In raw Z80 bytes as test_hwinfo_port: registers written and read back
// with the bits they keep, a register the chip does
// not have, the tick, our stereo register (MACHINE_AY_STEREO); reset silences it; without the platform's hooks the
// ports read 0xFF, as with no chip.
static void ay_program(void) {
	z80.n = 0;
	emit(1, 0xf3); // DI
	emit(8, 0x3e, 7, 0xd3, 0xd8, 0x3e, 0x3e, 0xd3, 0xd0); // RC2014: register 7 = 3EH
	emit(5, 0xdb, 0xd0, 0x32, 0x00, 0xf0); // IN A,(D0H); LD (F000H),A
	emit(8, 0x3e, 1, 0xd3, 0xd8, 0x3e, 0xff, 0xd3, 0xd0); // register 1 = FFH
	emit(5, 0xdb, 0xd0, 0x32, 0x01, 0xf0);
	emit(8, 0x3e, 8, 0xd3, 0xd8, 0x3e, 0xff, 0xd3, 0xd0); // register 8 = FFH
	emit(5, 0xdb, 0xd0, 0x32, 0x02, 0xf0);
	emit(4, 0x3e, 20, 0xd3, 0xd8); // register 20
	emit(5, 0xdb, 0xd0, 0x32, 0x03, 0xf0);
	emit(10, 0xdb, 68, 0x32, 0x04, 0xf0, 0xdb, 69, 0x32, 0x05, 0xf0); // the tick, low then high
	emit(8, 0x3e, 16, 0xd3, 0xd8, 0x3e, 0x07, 0xd3, 0xd0); // the stereo register = 7: ACB
	emit(5, 0xdb, 0xd0, 0x32, 0x06, 0xf0);
	run_program();
}

static void test_ay_ports(void) {
	extern uint8_t bnk0[65536];
	machine_init(&host_platform);
	host_ay_clear();
	ay_program();
	CHECK_EQ(bnk0[0xf000], 0x3e);
	CHECK_EQ(bnk0[0xf001], 0x0f); // 4 bits kept
	CHECK_EQ(bnk0[0xf002], 0x1f);
	CHECK_EQ(bnk0[0xf003], 0xff);
	CHECK_EQ(bnk0[0xf006], 0x33); // ACB picked and in use
	CHECK_EQ(host_ay_writes, 17 + 4); // the reset's 16 and the stereo register, then the program's
	CHECK(host_ay_log[16][0] == MACHINE_AY_STEREO && host_ay_log[16][1] == 0);
	CHECK(host_ay_log[17][0] == 7 && host_ay_log[17][1] == 0x3e);
	CHECK(host_ay_log[18][0] == 1 && host_ay_log[18][1] == 0xff);
	CHECK(host_ay_log[19][0] == 8 && host_ay_log[19][1] == 0xff);
	CHECK(host_ay_log[20][0] == MACHINE_AY_STEREO && host_ay_log[20][1] == 7);
	uint16_t tick = (uint16_t)(bnk0[0xf004] | bnk0[0xf005] << 8);
	CHECK((uint16_t)(host_clock_us() / 1000 - tick) < 1000);
	machine_reset();
	CHECK_EQ(ay_read(&host_ay, 8), 0);
	CHECK_EQ(ay_read(&host_ay, 7), 0);
	CHECK_EQ(ay_read(&host_ay, MACHINE_AY_STEREO), 0x10); // the setting's again: mono

	machine_platform_t none = host_platform;
	none.sound_write = NULL;
	none.sound_read = NULL;
	machine_init(&none);
	host_ay_clear();
	ay_program();
	CHECK_EQ(bnk0[0xf000], 0xff);
	CHECK_EQ(bnk0[0xf002], 0xff);
	CHECK_EQ(bnk0[0xf006], 0xff); // no stereo either
	CHECK_EQ(host_ay_writes, 0);
	machine_init(&host_platform);
}

// Channel ch's tone periods as written since host_ay_clear: one when its
// coarse register is written, 0s (QUIET) left out.
static unsigned ay_periods(unsigned ch, uint16_t *out, unsigned max) {
	unsigned n = 0, fine = 0;
	for (size_t i = 0; i < host_ay_writes && i < HOST_AY_LOG; ++i) {
		unsigned reg = host_ay_log[i][0], v = host_ay_log[i][1];
		if (reg == 2 * ch) {
			fine = v;
		} else if (reg == 2 * ch + 1 && (fine | v << 8) != 0 && n < max) {
			out[n++] = (uint16_t)(fine | v << 8);
		}
	}
	return n;
}

// A chip without our stereo register (older firmware): it reads 0xFF.
static uint8_t old_sound_read(uint8_t reg) {
	return reg == MACHINE_AY_STEREO ? 0xff : host_platform.sound_read(reg);
}

// AYTEST (src/machine/cpm/aytest.asm) under CP/M 2.2: the chip found on
// RC2014's ports, the scale and the chords' periods and how long they
// take, the noise sweep, a key stopping the envelopes with the chip
// silenced, the stereo layout stepped by S and A, B, C in turn, put back
// at the end; without the stereo register no S; without the chip it says
// so.
static void test_aytest_tool(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("AYTEST\r"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "the chip on ports D8h/D0h (RC2014)") != NULL);
	CHECK(strstr(screen, "S Stereo: mono (SET-UP's)\r\nChoice: ") != NULL);

	static const uint16_t scale[] = {440, 392, 349, 330, 294, 262, 233, 220};
	uint16_t got[16];
	host_ay_clear();
	clear_screen();
	uint64_t start = host_clock_us();
	CHECK(type("1"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(host_clock_us() - start >= 8 * 300000); // 300 ms a note
	CHECK(strstr(screen, "Scale on A: C D E F G A B C \r\n") != NULL);
	CHECK_EQ(ay_periods(0, got, 16), 8);
	CHECK(memcmp(got, scale, sizeof scale) == 0);

	static const uint16_t chord_b[] = {349, 262, 233, 175}, chord_c[] = {294, 220, 196, 147};
	host_ay_clear();
	clear_screen();
	CHECK(type("2"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Chords on A, B, C: C F G C \r\n") != NULL);
	CHECK_EQ(ay_periods(1, got, 16), 4);
	CHECK(memcmp(got, chord_b, sizeof chord_b) == 0);
	CHECK_EQ(ay_periods(2, got, 16), 4);
	CHECK(memcmp(got, chord_c, sizeof chord_c) == 0);

	host_ay_clear();
	CHECK(type("3"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	unsigned want = 31, bad = 0;
	for (size_t i = 0; i < host_ay_writes; ++i) {
		if (host_ay_log[i][0] == 6 && host_ay_log[i][1] != 0) {
			bad += host_ay_log[i][1] != want--;
		}
	}
	CHECK_EQ(bad, 0);
	CHECK_EQ(want, 0); // 31 down to 1

	// The envelopes take 16 s: a key after the third shape stops them.
	host_ay_clear();
	clear_screen();
	CHECK(type("4"));
	CHECK_EQ(run_until("Shape  2", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Shape  0  \\_______\r\n") != NULL && strstr(screen, "Shape  1") != NULL);
	CHECK(type(" "));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, " -- stopped") != NULL && strstr(screen, "Shape 15") == NULL);
	CHECK_EQ(ay_read(&host_ay, 8), 0); // silenced
	CHECK_EQ(ay_read(&host_ay, 7), 0x3f);

	// S: mono picked here, then ABC; A, B and C in turn where ABC puts them.
	clear_screen();
	CHECK(type("s"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "S Stereo: mono (picked here)") != NULL);
	CHECK_EQ(ay_read(&host_ay, MACHINE_AY_STEREO), 0x11);
	clear_screen();
	CHECK(type("S"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "S Stereo: ABC (A left, B center, C right) (picked here)") != NULL);
	CHECK_EQ(ay_stereo(&host_ay), AY_STEREO_ABC);
	host_ay_clear();
	clear_screen();
	start = host_clock_us();
	CHECK(type("6"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(host_clock_us() - start >= 3 * 1000000); // a second each
	CHECK(strstr(screen, "A, B, C in turn: A left  B center  C right  \r\n") != NULL);
	for (unsigned ch = 0; ch < 3; ++ch) {
		CHECK_EQ(ay_periods(ch, got, 16), 1);
		CHECK_EQ(got[0], 262);
	}
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK_EQ(ay_read(&host_ay, MACHINE_AY_STEREO), 0x10); // SET-UP's again, as found

	// Older firmware: no S line, A, B, C in turn as mono.
	machine_platform_t old = host_platform;
	old.sound_read = old_sound_read;
	machine_init(&old);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("AYTEST\r"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "S Stereo") == NULL);
	clear_screen();
	CHECK(type("s6"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "A, B, C in turn: A both  B both  C both  \r\n") != NULL);
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	machine_platform_t none = host_platform;
	none.sound_write = NULL;
	none.sound_read = NULL;
	clear_screen();
	machine_init(&none);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("AYTEST", "No AY-3-8910 on ports D8h/D0h."));
	machine_init(&host_platform);
	drives_unmount_all();
}

// A PSG file's register writes (register, value) in order, at most max,
// and in frames[i] the frame write i is in; the count.
static size_t psg_writes(const char *name, uint8_t (*out)[2], unsigned *frames, size_t max) {
	char path[512];
	snprintf(path, sizeof path, "%s/%s", TUNES_DIR, name);
	FILE *f = fopen(path, "rb");
	CHECK(f != NULL);
	if (!f) {
		return 0;
	}
	static uint8_t data[65536];
	size_t len = fread(data, 1, sizeof data, f);
	fclose(f);
	CHECK(len > 16 && memcmp(data, "PSG\x1a", 4) == 0);
	size_t n = 0;
	unsigned frame = 0;
	for (size_t i = 16; i < len && data[i] != 0xfd && n < max;) {
		if (data[i] == 0xff) {
			++frame;
			++i;
		} else if (data[i] == 0xfe) {
			frame += 4u * data[i + 1];
			i += 2;
		} else {
			out[n][0] = data[i];
			out[n][1] = data[i + 1];
			frames[n++] = frame;
			i += 2;
		}
	}
	return n;
}

// AYPLAY (src/machine/cpm/ayplay.asm, bd 1ls.18.10) under CP/M 2.2: the
// usage lists the tunes; JACQUES in ABC plays the file's writes in order
// at 50 frames a second, shows the time, stops on Q with the chip
// silenced and the layout put back; a missing file, a bad option.
static void test_ayplay_tool(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(cpm("AYPLAY", "PSG files here:"));
	CHECK(strstr(screen, "  KOROBEIN.PSG\r\n") != NULL && strstr(screen, "  JACQUES.PSG\r\n") != NULL
		&& strstr(screen, "  ODE.PSG\r\n") != NULL);

	static uint8_t want[8192][2];
	static unsigned frames[8192];
	size_t n = psg_writes("jacques.psg", want, frames, 8192);
	CHECK(n > 100);
	clear_screen();
	host_ay_clear();
	CHECK(type("AYPLAY JACQUES ABC\r"));
	CHECK_EQ(run_until("(Q stops)", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "Playing JACQUES.PSG in ABC stereo (Q stops)") != NULL);
	CHECK_EQ(ay_stereo(&host_ay), AY_STEREO_ABC);
	uint64_t start = host_clock_us();
	CHECK_EQ(run_until("  0:02", 10), MACHINE_RUNNING);
	uint64_t took = host_clock_us() - start;
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, " -- stopped") != NULL);
	// The probe's 2, the layout, QUIET's 15; at the end QUIET and the
	// layout put back.
	const size_t first = 2 + 1 + 15;
	CHECK(host_ay_writes > first + 16 && host_ay_writes < HOST_AY_LOG);
	size_t played = host_ay_writes - first - 16;
	CHECK(played > 20 && played < n);
	size_t bad = 0;
	for (size_t i = 0; i < played && i < n; ++i) {
		bad += host_ay_log[first + i][0] != want[i][0] || host_ay_log[first + i][1] != want[i][1];
	}
	CHECK_EQ(bad, 0);
	// Two seconds shown after 100 frames, in about 2 s: the writes up to
	// there played (the stop comes a little later), none much past it.
	CHECK(frames[played] >= 100 && frames[played - 1] < 125);
	CHECK(took > 1500000 && took < 2600000);
	CHECK(host_ay_log[host_ay_writes - 1][0] == MACHINE_AY_STEREO && host_ay_log[host_ay_writes - 1][1] == 0);
	CHECK_EQ(ay_read(&host_ay, 8), 0);
	CHECK_EQ(ay_read(&host_ay, 7), 0x3f);

	CHECK(cpm("AYPLAY NOPE", "No such file."));
	CHECK(cpm("AYPLAY ODE LOUD", "Unknown option."));
	CHECK(cpm("AYPLAY AYPLAY.COM", "Not a PSG file."));
	machine_init(&host_platform);
	drives_unmount_all();
}

// ---- CP/M 3 (picosim's banked system: CPM31, CPM32; bd f8w) ----

// Booted by mounting its disks on A: and B:, no setting: the boot ROM
// loads whatever A: holds. Its PROFILE.SUB ends with VT100DYN's banner.
static void cpm3_boot(void) {
	clear_screen();
	machine_reset();
	CHECK_EQ(run_until("RSX loaded and initialized.", 30), MACHINE_RUNNING);
	const char *banner = strstr(screen, "RSX loaded and initialized.");
	if (!banner || !strstr(banner, "A>")) { // the prompt may have come with it
		CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	}
}

static void test_boot_cpm3(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM31"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("CPM32"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM31"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "CPM32"), DRIVES_OK);
	cpm3_boot();
	CHECK(strstr(screen, "CP/M V3.0 Loader") != NULL);
	CHECK(strstr(screen, "61K TPA") != NULL);
	CHECK(strstr(screen, "Banked BIOS") != NULL);
	// PROFILE.SUB's VT100DYN loads, without its help, and no SETDEF
	// runs (bd f8w.9):
	CHECK(strstr(screen, "RSX loaded and initialized.") != NULL);
	CHECK(strstr(screen, "DEVICE CONSOLE") == NULL);
	CHECK(strstr(screen, "Drive Search Path") == NULL);
	// the BIOS sets SETDEF's defaults.
	CHECK(cpm("SETDEF", "2nd Drive            - A:"));
	CHECK(strstr(screen, "3rd Drive            - B:") != NULL);
	CHECK(strstr(screen, "Search Order         - COM, SUB") != NULL);
	CHECK(strstr(screen, "Temporary Drive      - B:") != NULL);
	CHECK(strstr(screen, "Program Name Display - Off") != NULL);
	CHECK(cpm("TYPE README.TXT [NO PAGE]", "Temporary drive     B:"));
	// CPM32 is an 8 MB disk.
	CHECK(cpm("SHOW B:[DRIVE]", " 8,160: Kilobyte Drive  Capacity"));
	CHECK(cpm("DATE", "/20"));
	CHECK(cpm("DIR B:X*.COM", "XPIP     COM"));
	// The user number shows in the prompt.
	clear_screen();
	CHECK(type("USER 3\r"));
	CHECK_EQ(run_until("3A>", 10), MACHINE_RUNNING);
	CHECK(cpm("USER 0", "")); // back to A>
	// ^C: a warm boot reloads CCP.COM.
	clear_screen();
	CHECK(type("\x03"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIRSYS CCP.COM", "CCP      COM")); // a system file
	CHECK(cpm("SETDEF", "Temporary Drive      - B:")); // kept
	// MEM: programs in bank 1, the system in bank 0, bank 2 free.
	CHECK(cpm("MEM", "CP/M 3.1"));
	// The TPA ends below VT100DYN's RSX, under the BDOS.
	CHECK(strstr(screen, "TPA     0100H-F205H: 61702 bytes (60K)") != NULL);
	CHECK(strstr(screen, "Memory  160K: bank 0 64K, banks 1-2 48K each (C000H-FFFFH common)") != NULL);
	CHECK(strstr(screen, "Bank 0  the system: banked BDOS and BIOS, disk buffers") != NULL);
	CHECK(strstr(screen, "Bank 1  the TPA (programs)") != NULL);
	CHECK(strstr(screen, "Bank 2  not used by CP/M") != NULL);
	// MEM T tests only bank 2: 0 and 1 are CP/M 3's.
	CHECK(cpm("MEM T", "Bank 2  ok"));
	CHECK(strstr(screen, "Bank 1  ok") == NULL);

	// README.TXT's way of changing a default: a SETDEF line put in
	// PROFILE.SUB with ED, run at the next cold boot.
	clear_screen();
	CHECK(type("ED PROFILE.SUB\r"));
	CHECK_EQ(run_until(": *", 10), MACHINE_RUNNING); // ED numbers lines
	const struct {
		const char *keys, *prompt;
	} ed[] = {
		{"#A\r", ": *"},
		{"BI\r", ":  "}, // inserting
		{"SETDEF [TEMPORARY=A]\r", ":  "},
		{"\x1a", ": *"},
		{"E\r", "A>"},
	};
	for (unsigned i = 0; i < sizeof ed / sizeof ed[0]; ++i) {
		clear_screen();
		CHECK(type(ed[i].keys));
		CHECK_EQ(run_until(ed[i].prompt, 10), MACHINE_RUNNING);
	}
	cpm3_boot();
	CHECK(strstr(screen, "Temporary Drive      - A:") != NULL);
	CHECK(cpm("SETDEF", "Temporary Drive      - A:"));
	CHECK(strstr(screen, "3rd Drive            - B:") != NULL); // the rest as before
}

// DATE SET under CP/M 3 sets the machine's clock through our BIOS's TIME
// (C = 0FFH: ports 65/66), and DATE reads it back (bd 1ls.6.1).
static void test_cpm3_date(void) {
	drives_unmount_all();
	CHECK_EQ(drives_mount(0, "CPM31"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "CPM32"), DRIVES_OK);
	cpm3_boot();
	unsigned sets = host_time_sets;
	static const char *const steps[][2] = {
		{"DATE SET\r", "(MM/DD/YY): "},
		{"10/22/26\r", "(HH:MM:SS): "},
		{"13:45:00\r", "Press any key to set time"},
		{" ", "A>"},
	};
	for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
		clear_screen();
		CHECK(type(steps[i][0]));
		CHECK_EQ(run_until(steps[i][1], 10), MACHINE_RUNNING);
	}
	CHECK(host_time_sets > sets);
	uint32_t now = (uint32_t)((host_time_offset_us + host_clock_us()) / 1000000u);
	uint32_t want = 17827u * DATETIME_DAY + 13 * 3600 + 45 * 60; // 22.10.2026 13:45:00
	CHECK(now >= want && now < want + 5);
	CHECK(cpm("DATE", "Thu 10/22/2026 13:45:0"));
	drives_unmount_all();
}

// XLS, XPIP and XERA under CP/M 3, run from B: through its search path;
// a file over 16 KB (several extents) in and out.
static void test_cpm3_tools(void) {
	static uint8_t big[20000];
	for (unsigned i = 0; i < sizeof big; ++i) {
		big[i] = (uint8_t)(i * 7 + i / 251);
	}
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("EMPTY"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM31"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "CPM32"), DRIVES_OK);
	CHECK_EQ(drives_mount(2, "EMPTY"), DRIVES_OK);
	put_fat_file("/XFER/BIG.BIN", big, sizeof big);
	cpm3_boot();
	CHECK(cpm("XLS BIG.BIN", "BIG.BIN           20000"));
	CHECK(cpm("XPIP C:=SD:BIG.BIN", "1 copied"));
	CHECK(cpm("DIR C:", "BIG      BIN"));
	CHECK(cpm("XPIP SD:BACK3.BIN=C:BIG.BIN", "1 copied"));
	CHECK(fat_file_is("/XFER/BACK3.BIN", big, sizeof big, 157 * 128));
	CHECK(cpm("XERA SD:BACK3.BIN", "1 erased"));
	CHECK(!fat_file_exists("/XFER/BACK3.BIN"));
	f_unlink("/XFER/BIG.BIN");
	CHECK(cpm("HWINFO", "Real-time clock")); // from CPM31, A:
	hwinfo_report_checks();
	clear_screen();
	CHECK(type("AYTEST\r"));
	CHECK_EQ(run_until("Choice: ", 10), MACHINE_RUNNING);
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	drives_unmount_all();
}

// Our banked BIOS (src/machine/cpm/cpm3, bd f8w.4): drives A-H, each of the
// formats port 8 reports; what it writes, CP/M 2.2 reads (the same file
// system layout).
static void test_cpm3_drives(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("EMPTY"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("Z80TESTS"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_create("C3HD8", DRIVES_HD8, window, sizeof window, NULL, NULL), DRIVES_OK);
	CHECK_EQ(drives_create("C3HD4", DRIVES_HD4, window, sizeof window, NULL, NULL), DRIVES_OK);
	CHECK_EQ(drives_mount(0, "CPM31"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "CPM32"), DRIVES_OK);
	CHECK_EQ(drives_mount(4, "C3HD8"), DRIVES_OK);
	CHECK_EQ(drives_mount(5, "C3HD4"), DRIVES_OK);
	CHECK_EQ(drives_mount(7, "EMPTY"), DRIVES_OK);
	cpm3_boot();
	CHECK(strstr(screen, "Banked BIOS V1.3, drives A-H") != NULL);
	CHECK(strstr(screen, "61K TPA") != NULL);
	CHECK(cpm("DIR E:", "No File"));
	CHECK(cpm("SHOW E:[DRIVE]", " 8,160: Kilobyte Drive  Capacity"));
	CHECK(cpm("SHOW F:[DRIVE]", " 4,080: Kilobyte Drive  Capacity"));
	CHECK(cpm("SHOW H:[DRIVE]", "   243: Kilobyte Drive  Capacity"));
	// Files to and from every format (PIP leaves out system files such as
	// A:'s; XLS is ours, an ordinary file).
	CHECK(cpm("PIP E:MYXLS.COM=B:XLS.COM[V]", ""));
	CHECK(cpm("PIP F:=E:MYXLS.COM[V]", ""));
	CHECK(cpm("PIP H:=F:MYXLS.COM[V]", ""));
	CHECK(cpm("DIR H:MYXLS.COM", "MYXLS    COM"));
	CHECK(strstr(screen, "Verify") == NULL);
	// A program found on E: through the search path.
	CHECK(cpm("SETDEF *,E:", "2nd Drive            - E:"));
	CHECK(cpm("MYXLS NOSUCH.*", "No files"));
	// G: has nothing mounted: a select error.
	CHECK(cpm("DIR G:", "CP/M Error On G: Invalid Drive"));
	// Another image on F: while the system runs: the BIOS sees the new
	// format and the BDOS logs the drive in again.
	CHECK_EQ(drives_mount(5, "Z80TESTS"), DRIVES_OK);
	CHECK(cpm("DIR F:TERMTEST.COM", "TERMTEST COM"));
	CHECK_EQ(drives_mount(5, "C3HD4"), DRIVES_OK);
	CHECK(cpm("DIR F:MYXLS.COM", "MYXLS    COM"));
	// ^C keeps all of it.
	clear_screen();
	CHECK(type("\x03"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIR E:MYXLS.COM", "MYXLS    COM"));
	CHECK(cpm("ERA H:MYXLS.COM", ""));
	// Another image of the same format. Logged in with an empty image,
	// the BDOS must not write to C3HD8 by the empty one's allocation, over
	// MYXLS's blocks (between commands it logs the drive in again; in the
	// middle of a program, port 8's mount count tells the BIOS).
	CHECK_EQ(drives_create("C3HD8B", DRIVES_HD8, window, sizeof window, NULL, NULL), DRIVES_OK);
	CHECK_EQ(drives_mount(4, "C3HD8B"), DRIVES_OK);
	CHECK(cpm("DIR E:", "No File"));
	CHECK_EQ(drives_mount(4, "C3HD8"), DRIVES_OK);
	static uint8_t junk[8192]; // XPIP: no disk reset first, as PIP does
	memset(junk, 0x76, sizeof junk); // HALT
	put_fat_file("/XFER/JUNK.BIN", junk, sizeof junk);
	CHECK(cpm("B:XPIP E:=SD:JUNK.BIN", "1 copied"));
	CHECK(cpm("DIR E:", "E: MYXLS    COM : JUNK     BIN"));
	CHECK(cpm("E:MYXLS NOSUCH.*", "No files")); // still XLS
	CHECK(cpm("ERA E:JUNK.BIN", ""));
	f_unlink("/XFER/JUNK.BIN");
	CHECK_EQ(f_unlink("/DISKS80/C3HD8B.DSK"), FR_OK);
	// CP/M 2.2 reads the hard disks CP/M 3 wrote.
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	clear_screen();
	machine_reset();
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIR E:", "E: MYXLS    COM"));
	CHECK(cpm("DIR F:", "F: MYXLS    COM"));
	// MEM under CP/M 2.2: everything in bank 0.
	CHECK(cpm("MEM", "CP/M 2.2"));
	CHECK(strstr(screen, "TPA     0100H-E405H: 58118 bytes (56K)") != NULL);
	CHECK(strstr(screen, "Bank 0  the TPA (programs)") != NULL);
	CHECK(strstr(screen, "Bank 1  not used by CP/M") != NULL);
	CHECK(cpm("MEM T", "Bank 1  ok"));
	CHECK(strstr(screen, "Bank 2  ok") != NULL);
	drives_unmount_all();
	CHECK_EQ(f_unlink("/DISKS80/C3HD8.DSK"), FR_OK);
	CHECK_EQ(f_unlink("/DISKS80/C3HD4.DSK"), FR_OK);
}

// Banks given to the machine (as the PSRAM build's, machine_set_banks):
// MEM sees 15, MEM T fills and checks them all, and its last pass (the
// complement of the address and bank pattern) is in that memory.
static void test_mem_banks(void) {
	static uint8_t psram[MACHINE_MAX_BANKS * MACHINE_BANK_SIZE];
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	machine_set_banks(psram, 99); // at most 15
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(machine_banks(), MACHINE_MAX_BANKS);
	CHECK(machine_bank(1) == psram && machine_bank(15) == psram + 14 * MACHINE_BANK_SIZE);
	CHECK(machine_bank(0) == NULL && machine_bank(16) == NULL);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("MEM", "Memory  784K: bank 0 64K, banks 1-15 48K each"));
	CHECK(strstr(screen, "Bank 15  not used by CP/M") != NULL);
	CHECK(cpm("MEM T", "Bank 15  ok"));
	CHECK(strstr(screen, "Testing the free banks") != NULL);
	CHECK(strstr(screen, "Bank 1  ok") != NULL);
	CHECK(strstr(screen, "wrong") == NULL);
	bool pattern = true;
	for (unsigned b = 1; b <= MACHINE_MAX_BANKS; ++b) {
		for (unsigned a = 0; a < MACHINE_BANK_SIZE; a += 97) {
			pattern &= machine_bank(b)[a] == (uint8_t)~((a & 0xff) ^ (a >> 8) ^ b);
		}
	}
	CHECK(pattern);
	// Back to the SRAM banks for the tests after this one.
	machine_set_banks(NULL, 0);
	machine_init(&host_platform);
	CHECK_EQ(machine_banks(), 2);
	drives_unmount_all();
}

// The disk controller refuses a hard-disk image to a system that has not
// asked port 8 about the drive since the reset (bd f8w.7); floppies are
// always served.
static void test_hd_guard(void) {
	extern uint8_t bnk0[65536];
	const uint16_t cmd = 0x8000, dma = 0x8100, st = 0xf000;
	drives_unmount_all();
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	drives_result_t made = drives_create("GUARD", DRIVES_HD8, window, sizeof window, NULL, NULL);
	CHECK(made == DRIVES_OK || made == DRIVES_EXISTS);
	CHECK_EQ(drives_mount(2, "GUARD"), DRIVES_OK);
	uint8_t before[DRIVES_SECTOR], after[DRIVES_SECTOR];
	CHECK_EQ(drives_read(2, 1, 1, before), 0);
	machine_init(&host_platform);
	z80.n = 0;
	emit(1, 0xf3);                                     // DI
	emit(4, 0x3e, 0x10, 0xd3, 4);                      // LD A,10H; OUT (4),A: the command block
	emit(4, 0x3e, cmd & 0xff, 0xd3, 4);                // at cmd
	emit(4, 0x3e, cmd >> 8, 0xd3, 4);
	static const uint8_t steps[][2] = {
		{0x22, 0}, {0x42, 1}, // read, write C: (not asked about)
		{0x20, 2},            // read A: (a floppy)
		{0x00, 3},            // ask port 8 about C: (and IN again, below)
		{0x22, 4}, {0x42, 5}, // read, write C:
	};
	for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
		if (steps[i][0]) {
			emit(4, 0x3e, steps[i][0], 0xd3, 4); // LD A,cmd; OUT (4),A
		} else {
			emit(4, 0x3e, 2, 0xd3, 8); // LD A,2; OUT (8),A
		}
		emit(2, 0xdb, steps[i][0] ? 4 : 8);   // IN A,(4) or (8)
		uint16_t at = (uint16_t)(st + steps[i][1]);
		emit(3, 0x32, at & 0xff, at >> 8);    // LD (at),A
		if (!steps[i][0]) {
			emit(2, 0xdb, 8);                       // IN A,(8): the mount count
			emit(3, 0x32, (st + 8) & 0xff, (st + 8) >> 8); // LD (st+8),A
		}
	}
	emit(1, 0x76); // HALT
	CHECK(z80.n <= sizeof z80.code);
	for (int run = 0; run < 2; ++run) { // the second after a reset: asked again
		memcpy(bnk0 + 0xff00, z80.code, z80.n);
		const uint8_t block[4] = {1, 1, dma & 0xff, dma >> 8}; // track 1, sector 1
		memcpy(bnk0 + cmd, block, sizeof block);
		memset(bnk0 + dma, 0x5a, DRIVES_SECTOR);
		memset(bnk0 + st, 0xff, 16);
		machine_reset();
		unsigned slices = 0;
		while (machine_run(10000) == MACHINE_RUNNING && ++slices < 100) {
		}
		CHECK(strstr(machine_stop_reason(), "HALT with interrupts off") != NULL);
		CHECK_EQ(bnk0[st + 0], 2); // refused: no disk
		CHECK_EQ(bnk0[st + 1], 2);
		CHECK_EQ(bnk0[st + 2], 0); // the floppy is read
		CHECK_EQ(bnk0[st + 3], 3); // C: is an 8 MB hard disk
		CHECK_EQ(bnk0[st + 8], drives_mounts(2));
		CHECK_EQ(bnk0[st + 4], 0); // now served
		CHECK_EQ(bnk0[st + 5], 0);
		CHECK_EQ(drives_read(2, 1, 1, after), 0);
		CHECK(memcmp(before, after, sizeof before) == 0);
		CHECK(memcmp(bnk0 + dma, before, sizeof before) == 0);
		CHECK_EQ(drives_mount(2, "GUARD"), DRIVES_OK); // the count moves on
	}
	drives_unmount_all();
}

// ---- TERMTEST (src/machine/cpm/termtest.asm on Z80TESTS) through the terminal ----

static screen_t tscreen;
static term_t tterm;

static void console_to_term(uint8_t c) {
	term_write(&tterm, &c, 1);
}

static void term_replies_to_keys(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	for (size_t i = 0; i < len; ++i) {
		machine_key(data[i]);
	}
}

// The text of a terminal row, trailing blanks dropped.
static const char *trow(unsigned row) {
	static char text[SCREEN_MAX_COLS + 1];
	unsigned len = 0;
	for (unsigned col = 0; col < tscreen.cols; ++col) {
		text[col] = (char)screen_glyph(&tscreen, col, row);
		if (text[col] != ' ') {
			len = col + 1;
		}
	}
	text[len] = '\0';
	return text;
}

static bool trow_check(unsigned row, const char *want, bool whole, int line) {
	const char *got = trow(row);
	bool ok = whole ? strcmp(got, want) == 0 : strstr(got, want) != NULL;
	if (!ok) {
		fprintf(stderr, "%s:%d: row %u \"%s\", want %s\"%s\"\n", __FILE__, line, row, got,
			whole ? "" : "it to contain ", want);
	}
	return ok;
}
#define TROW_IS(row, want) CHECK(trow_check(row, want, true, __LINE__))
#define TROW_HAS(row, want) CHECK(trow_check(row, want, false, __LINE__))
#define TGLYPH(col, row, want) CHECK_EQ(screen_glyph(&tscreen, col, row), want)

static void next_page(void) {
	CHECK(type(" "));
}

static bool page_shown(void) {
	return run_until("back to the menu", 30) == MACHINE_RUNNING;
}

// TERMTEST's pages for personality p (0 VT100, 1 VT52, 2 ADM-3A) on a
// terminal of that type.
static void termtest_pages(unsigned p) {
	static const char *const names[] = {"-- VT100", "-- VT52", "-- ADM-3A"};
	screen_init(&tscreen, 80, PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&tterm, &tscreen, 0, 24);
	term_set_reply(&tterm, term_replies_to_keys, NULL);
	if (p == 2) {
		term_set_defaults(&tterm, &(term_defaults_t){.personality = TERM_ADM3A});
	}
	CHECK(type("B:TERMTEST\r"));
	CHECK_EQ(run_until("Choice? ", 30), MACHINE_RUNNING);
	CHECK(type(p == 0 ? "1" : p == 1 ? "2" : "3"));

	// Cursor addressing.
	CHECK(page_shown());
	TROW_HAS(0, "Cursor addressing");
	TROW_HAS(0, names[p]);
	CHECK_EQ(tterm.vt100.vt52, p == 1);
	TGLYPH(10, 2, '+');
	TGLYPH(69, 2, '+');
	TGLYPH(10, 21, '+');
	TGLYPH(69, 21, '+');
	TGLYPH(11, 2, '-');
	TGLYPH(10, 3, '|');
	TGLYPH(69, 20, '|');
	TROW_HAS(11, "center");
	TROW_IS(1, "top left (row 2)");
	TGLYPH(68, 22, 'b');
	TGLYPH(79, 22, 't');
	next_page();

	// Moves: OK on every ??.
	CHECK(page_shown());
	TGLYPH(0, 0, 'O');
	TGLYPH(1, 0, 'K');
	for (unsigned row = 2; row <= 11; ++row) {
		TGLYPH(30, row, 'O');
		TGLYPH(31, row, 'K');
	}
	for (unsigned row = 0; row < 23; ++row) {
		CHECK(strstr(trow(row), "??") == NULL);
	}
	next_page();

	// Erasing.
	CHECK(page_shown());
	TGLYPH(29, 3, 'X');
	TGLYPH(30, 3, ' ');
	TGLYPH(59, 7, 'X');
	TGLYPH(59, 8, 'X');
	TGLYPH(29, 9, 'X');
	TGLYPH(30, 9, ' ');
	TROW_IS(10, "");
	TROW_IS(20, "");
	next_page();

	// Insert and delete line.
	CHECK(page_shown());
	if (p == 1) {
		TROW_HAS(2, "neither");
	} else {
		TROW_IS(4, "line 04");
		TROW_IS(5, "");
		TROW_IS(6, "line 05");
		TROW_IS(9, "line 08");
		TROW_IS(10, "line 11");
		TROW_IS(20, "line 21");
		TROW_IS(21, "");
	}
	next_page();

	// Scrolling.
	CHECK(page_shown());
	if (p == 2) {
		TROW_IS(1, "line 02");
		TROW_IS(21, "line 22");
		TROW_IS(22, "");
	} else {
		TROW_IS(1, "line 01");
		TROW_IS(22, "line 22");
	}
	next_page();

	// Wrapping.
	CHECK(page_shown());
	TGLYPH(75, 4, 'A');
	TGLYPH(79, 4, 'E');
	TROW_IS(5, "FGHIJ <- row 6");
	if (p == 2) {
		TROW_IS(10, "");
		TROW_HAS(11, "After a blank row");
	} else {
		TROW_HAS(10, "follows at once");
	}
	next_page();

	// Line graphics.
	CHECK(page_shown());
	if (p == 2) {
		TROW_HAS(2, "has none");
	} else {
		TGLYPH(10, 4, GLYPH_DEC_GRAPHICS('l'));
		TGLYPH(17, 4, GLYPH_DEC_GRAPHICS('k'));
		TGLYPH(10, 5, GLYPH_DEC_GRAPHICS('x'));
		TGLYPH(12, 5, 'b');
		TGLYPH(17, 6, GLYPH_DEC_GRAPHICS('j'));
		TROW_HAS(8, "lqk");
	}
	next_page();

	// Replies.
	CHECK(page_shown());
	if (p == 0) {
		TROW_HAS(6, "): ESC [ 5 ; 1 1 R");
		TROW_HAS(9, "): ESC [ ? 1 ; 2 c");
	} else if (p == 1) {
		TROW_HAS(4, "): ESC / Z");
	} else {
		TROW_HAS(2, "sends none");
	}
	next_page();

	// Keys, then ^C back to the menu, and Q.
	CHECK_EQ(run_until("Ctrl-C ends", 30), MACHINE_RUNNING);
	CHECK(type("\x1b[A"));
	CHECK_EQ(run_until("1B 5B 41", 30), MACHINE_RUNNING);
	CHECK(type("x"));
	CHECK_EQ(run_until("= 78", 30), MACHINE_RUNNING);
	TROW_IS(2, "ESC [ A   = 1B 5B 41");
	TROW_IS(3, "x   = 78");
	CHECK(type("\x03"));
	CHECK_EQ(run_until("Choice? ", 30), MACHINE_RUNNING);
	CHECK(!tterm.vt100.vt52); // ESC < at the end
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
}

static void test_termtest(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("Z80TESTS"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "Z80TESTS"), DRIVES_OK);
	host_cpr_reply = NULL;
	screen_init(&tscreen, 80, PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&tterm, &tscreen, 0, 24);
	host_console_hook = console_to_term;
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIR B:TERMTEST.COM", "TERMTEST COM"));
	for (unsigned p = 0; p < 3; ++p) {
		termtest_pages(p);
	}

	// Esc on a page goes back to the menu.
	CHECK(type("B:TERMTEST\r"));
	CHECK_EQ(run_until("Choice? ", 30), MACHINE_RUNNING);
	CHECK(type("1"));
	CHECK(page_shown());
	CHECK(type("\x1b"));
	CHECK_EQ(run_until("Choice? ", 30), MACHINE_RUNNING);
	CHECK(type("q"));
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
	host_console_hook = NULL;
}

// ---- ALTSCR (src/machine/cpm/altscr.asm on Z80TESTS): the alternate screen ----

// As main.c's: the main screen at up to 132 columns, or it and SET-UP's
// rows at 80.
#define ALT_STASH (SCREEN_STASH_BYTES(24, 132) > SCREEN_STASH_BYTES(24 + SETUP_ROWS, 80) \
	? SCREEN_STASH_BYTES(24, 132) : SCREEN_STASH_BYTES(24 + SETUP_ROWS, 80))
static uint8_t alt_buf[ALT_STASH];
static screen_stash_t alt_stash;

// The main screen as ALTSCR draws it, with "<1049" after the X once the
// first step is done.
static void altscr_main_shown(bool label) {
	for (unsigned r = 1; r <= 22; ++r) {
		char want[80];
		snprintf(want, sizeof want, "Main screen row %u  the quick brown fox jumps over the lazy dog", r);
		if (r != 12) {
			TROW_IS(r - 1, want);
		} else {
			TROW_HAS(r - 1, "Main screen row 12  the quick brown");
		}
	}
	TGLYPH(39, 11, 'X');
	if (label) {
		TROW_HAS(11, "X<1049");
		uint8_t fg, bg;
		screen_colors(&tscreen, 40, 11, &fg, &bg);
		CHECK_EQ(fg, 9); // bold red: the colors ?1049 saved came back
		CHECK(screen_attr(&tscreen, 40, 11) & SCREEN_ATTR_BOLD);
	}
	CHECK(screen_attr(&tscreen, 0, 0) & SCREEN_ATTR_BOLD); // row 1's style
	CHECK(screen_attr(&tscreen, 0, 1) & SCREEN_ATTR_UNDERLINE);
}

static void altscr_alt_shown(const char *title) {
	CHECK(tterm.alt_screen);
	TROW_IS(0, title);
	TROW_IS(18, "scrolled 1");
	TROW_IS(23, "scrolled 6");
	TROW_IS(10, "");
}

static void test_altscr(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("Z80TESTS"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "Z80TESTS"), DRIVES_OK);
	host_cpr_reply = NULL;
	screen_init(&tscreen, 80, PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&tterm, &tscreen, 0, 24);
	screen_stash_init(&alt_stash, alt_buf, sizeof alt_buf);
	term_set_stash(&tterm, &alt_stash);
	host_console_hook = console_to_term;
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIR B:ALTSCR.COM", "ALTSCR   COM"));

	CHECK(type("B:ALTSCR\r"));
	CHECK_EQ(run_until("back with ESC [ ? 1049 l", 30), MACHINE_RUNNING);
	altscr_alt_shown("ALTERNATE SCREEN (ESC [ ? 1049 h)");
	CHECK(type(" "));
	CHECK_EQ(run_until("Any key: next step.", 30), MACHINE_RUNNING);
	CHECK(!tterm.alt_screen);
	altscr_main_shown(true);
	TROW_HAS(22, "Back from ?1049");

	static const char *const modes[] = {"1047", "47"};
	for (unsigned m = 0; m < 2; ++m) {
		char text[64];
		CHECK(type(" "));
		snprintf(text, sizeof text, "back with ESC [ ? %s l", modes[m]);
		CHECK_EQ(run_until(text, 30), MACHINE_RUNNING);
		snprintf(text, sizeof text, "ALTERNATE SCREEN (ESC [ ? %s h)", modes[m]);
		altscr_alt_shown(text);
		CHECK(type(" "));
		CHECK_EQ(run_until("Any key: next step.", 30), MACHINE_RUNNING);
		CHECK(!tterm.alt_screen);
		altscr_main_shown(true);
		snprintf(text, sizeof text, "Back from ?%s:", modes[m]);
		TROW_HAS(22, text);
	}

	// SET-UP over the alternate screen at 80 columns, as the terminal does it:
	// its rows fit beside the main screen and come back.
	CHECK(type(" "));
	CHECK_EQ(run_until("Then any key", 30), MACHINE_RUNNING);
	altscr_alt_shown("ALTERNATE SCREEN with SET-UP over it");
	const unsigned setup_top = 24 - SETUP_ROWS;
	CHECK(screen_stash_push(&alt_stash, &tscreen, 'S', setup_top, SETUP_ROWS));
	for (unsigned r = setup_top; r < 24; ++r) {
		screen_clear_line(&tscreen, r, PALETTE_TEXT, PALETTE_BACKGROUND);
	}
	CHECK(screen_stash_pop(&alt_stash, &tscreen, 'S', PALETTE_TEXT, PALETTE_BACKGROUND));
	altscr_alt_shown("ALTERNATE SCREEN with SET-UP over it");
	CHECK(type(" "));
	CHECK_EQ(run_until("Back from SET-UP", 30), MACHINE_RUNNING);
	altscr_main_shown(true);
	CHECK(type(" "));
	CHECK_EQ(run_until("All steps done", 30), MACHINE_RUNNING);
	CHECK(type(" "));
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
	CHECK(!tterm.alt_screen);
	CHECK_EQ(alt_stash.n, 0);

	// Ctrl-C on the alternate screen leaves it.
	CHECK(type("B:ALTSCR\r"));
	CHECK_EQ(run_until("back with ESC [ ? 1049 l", 30), MACHINE_RUNNING);
	CHECK(tterm.alt_screen);
	CHECK(type("\x03"));
	CHECK_EQ(run_until("A>", 30), MACHINE_RUNNING);
	CHECK(!tterm.alt_screen);
	CHECK_EQ(alt_stash.n, 0);
	host_console_hook = NULL;
}

// ---- AYTRACK (src/machine/cpm/aytrack.asm, bd 1ls.18.11) through the terminal ----

// Run the machine for about seconds of wall time.
static void run_for(double seconds) {
	uint64_t end = host_clock_us() + (uint64_t)(seconds * 1e6);
	while (host_clock_us() < end) {
		machine_run(10000);
	}
}

// Channel ch's tone periods as the chip had them after each write since
// host_ay_clear (fine or coarse; a fine one followed by its coarse counts
// once), repeats left out.
static unsigned ay_tones(unsigned ch, uint16_t *out, unsigned max) {
	unsigned n = 0;
	uint16_t period = 0;
	for (size_t i = 0; i < host_ay_writes && i < HOST_AY_LOG; ++i) {
		unsigned reg = host_ay_log[i][0], v = host_ay_log[i][1];
		if (reg == 2 * ch) {
			period = (uint16_t)((period & 0xf00) | v);
			if (i + 1 < host_ay_writes && i + 1 < HOST_AY_LOG && host_ay_log[i + 1][0] == 2 * ch + 1) {
				continue; // its coarse next: one change
			}
		} else if (reg == 2 * ch + 1) {
			period = (uint16_t)((period & 0xff) | (v & 0x0f) << 8);
		} else {
			continue;
		}
		if (period != 0 && (n == 0 || out[n - 1] != period) && n < max) {
			out[n++] = period;
		}
	}
	return n;
}

// AYTRACK under CP/M 2.2 on a real term_t: a new song; three notes on A
// with the piano keys; the order list (insert, a pattern); the
// instruments' table; Return plays the pattern (the notes' periods on the
// chip), Return stops; the octave, a note off, a volume edited, Space
// playing the song; ^S saves, ^E exports, ^Q Y quits; the song loads
// again; AYPLAY plays the export.
static void test_aytrack_tool(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	host_cpr_reply = NULL;
	screen_init(&tscreen, 80, PALETTE_TEXT, PALETTE_BACKGROUND);
	term_init(&tterm, &tscreen, 0, 24);
	host_console_hook = console_to_term;
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("AYTRACK TEST\r"));
	CHECK_EQ(run_until("New song", 10), MACHINE_RUNNING);
	run_for(0.5); // drawn, then the message again
	TROW_IS(0, " AYTRACK  TEST.AYT  Pattern 0  Speed 06  Octave 4  Inst 0  Stopped");
	TROW_HAS(1, " Row  A      B      C");
	TROW_HAS(2, "  00  ... .  ... .  ... .");
	TROW_HAS(2, " 00 0");                          // the order list
	TROW_HAS(2, ">0 FEDCBBAAA9998888 T  00");      // instrument 0
	TROW_HAS(6, " 4 A631000000000000 N  01");      // the hi-hat
	TROW_HAS(12, "C  C# D  D# E  F  F# G  G# A  A# B"); // the keys' notes
	TROW_HAS(13, "Z  S  X  D  C  V  G  B  H  N  J  M");
	TROW_HAS(14, "Q  2  W  3  E  R  5  T  6  Y  7  U");

	clear_screen();
	CHECK(type("zcb"));
	CHECK_EQ(run_until("\x1b[6;", 10), MACHINE_RUNNING); // the cursor's row 3 drawn
	run_for(2); // each note previewed for half a second
	TROW_HAS(2, "  00  C-4 0  ... .  ... .");
	TROW_HAS(3, "  01  E-4 0");
	TROW_HAS(4, "  02  G-4 0");

	// The orders: Tab, I (a copy after it), 1 its pattern.
	clear_screen();
	CHECK(type("\ti1"));
	run_for(0.5);
	TROW_HAS(2, " 00 0");
	TROW_HAS(3, " 01 1");
	TROW_HAS(0, "Pattern 1");
	CHECK(type("\x1b[A")); // back to order 0: pattern 0 on screen
	run_for(0.3);
	TROW_HAS(0, "Pattern 0");
	TROW_HAS(2, "  00  C-4 0");
	CHECK(type("\t\t")); // the instruments, then the pattern again
	run_for(0.3);

	host_ay_clear();
	CHECK(type("\r"));
	CHECK_EQ(run_until("Playing the pattern", 10), MACHINE_RUNNING);
	run_for(1);
	static const uint16_t notes[] = {440, 349, 293};
	uint16_t got[16];
	CHECK(ay_tones(0, got, 16) >= 3);
	CHECK(memcmp(got, notes, sizeof notes) == 0);
	CHECK(type("\r"));
	CHECK_EQ(run_until("Stopped", 10), MACHINE_RUNNING);
	run_for(0.2);
	CHECK_EQ(ay_read(&host_ay, 8), 0);

	// ] a higher octave; = a note off (row 3); the instruments: a volume
	// typed into instrument 0's first step; Space plays the song, its
	// order marked; Space stops.
	CHECK(type("]"));
	run_for(0.2);
	TROW_HAS(0, "Octave 5");
	CHECK(type("["));
	CHECK(type("=")); // the cursor is on row 3, after the notes
	run_for(0.3);
	TROW_HAS(5, "  03  === .");
	CHECK(type("\t\t7"));
	run_for(1);
	TROW_HAS(2, ">0 7EDCBBAAA9998888 T  00");
	CHECK(type("\t"));
	CHECK(type(" "));
	CHECK_EQ(run_until("Playing the song", 10), MACHINE_RUNNING);
	run_for(0.3);
	TROW_HAS(2, ">00 0");
	CHECK(type(" "));
	CHECK_EQ(run_until("Stopped", 10), MACHINE_RUNNING);

	clear_screen();
	CHECK(type("\x13"));
	CHECK_EQ(run_until("Saved TEST.AYT", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("\x05"));
	CHECK_EQ(run_until("Exported TEST.PSG", 30), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("\x11"));
	CHECK_EQ(run_until("Y/N", 10), MACHINE_RUNNING);
	CHECK(type("y"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	CHECK(cpm("DIR TEST.*", "TEST     AYT"));
	CHECK(strstr(screen, "TEST     PSG") != NULL);

	clear_screen();
	CHECK(type("AYTRACK TEST\r"));
	CHECK_EQ(run_until("Loaded", 10), MACHINE_RUNNING);
	run_for(0.5);
	TROW_HAS(2, "  00  C-4 0");
	TROW_HAS(3, " 01 1");
	CHECK(type("\x11y"));
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);

	// The export: pattern 0, then the empty pattern 1, 32 rows each at
	// speed 6: 7.7 s; its first notes as the tracker played them.
	host_ay_clear();
	clear_screen();
	CHECK(type("AYPLAY TEST\r"));
	CHECK_EQ(run_until("Playing TEST.PSG", 10), MACHINE_RUNNING);
	CHECK_EQ(run_until("A>", 15), MACHINE_RUNNING);
	CHECK(strstr(screen, "0:07") != NULL);
	// (AYPLAY's probe writes register 1 first: the notes are found after.)
	unsigned n = ay_tones(0, got, 16), at = 0;
	while (at + 3 <= n && memcmp(got + at, notes, sizeof notes) != 0) {
		++at;
	}
	CHECK(at + 3 <= n);
	host_console_hook = NULL;
	machine_init(&host_platform);
	drives_unmount_all();
}

// ---- F83 (lib/f83, bd xdu) ----

// Type each line and wait for its text.
static void f83_lines(const char *const lines[][2], unsigned n) {
	for (unsigned i = 0; i < n; ++i) {
		clear_screen();
		CHECK(type(lines[i][0]));
		CHECK_EQ(run_until(lines[i][1], 10), MACHINE_RUNNING);
	}
}

// Laxen and Perry's F83 from the F83 disk under CP/M 2.2: it starts on a
// block file, interprets, compiles, lists a block and leaves with BYE;
// VIEW finds the source (VIEWDRV.BLK's patch included) on the current
// drive. Under CP/M 3, found through the search path from A:, VIEW reads
// B:, the drive it was loaded from (VIEWDRV.BLK).
static void test_f83(void) {
	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM22"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(diskimg_install(diskimg_find("F83"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM22"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "F83"), DRIVES_OK);
	clear_screen();
	machine_init(&host_platform);
	CHECK_EQ(run_until("A>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("B:\r"));
	CHECK_EQ(run_until("B>", 10), MACHINE_RUNNING);
	clear_screen();
	CHECK(type("F83 CLOCK.BLK\r"));
	CHECK_EQ(run_until("Version 2.1.0", 10), MACHINE_RUNNING);
	CHECK(strstr(screen, "8080 Forth 83 Model") != NULL);
	static const char *const cpm22[][2] = {
		{"2 3 + .\r", "5  ok"},
		{": SQUARE DUP * ;\r", " ok"},
		{"12 SQUARE .\r", "144  ok"},
		{"1 LIST\r", "System Support 1   Load screen"},
		{"VIEW DUP\r", "CODE DUP"},
		{"VIEW VIEW-DRIVE\r", ": VIEW-DRIVE"},
		{"BYE\r", "B>"},
	};
	f83_lines(cpm22, sizeof cpm22 / sizeof cpm22[0]);
	CHECK(strstr(screen, "Open error") == NULL);

	// AY.BLK (bd 1ls.18.7): the words load; the chip found, the notes'
	// periods, the stereo register, SCALE's writes.
	clear_screen();
	CHECK(type("F83 AY.BLK\r"));
	CHECK_EQ(run_until("Version 2.1.0", 10), MACHINE_RUNNING);
	static const char *const ay[][2] = {
		{"1 LOAD\r", "AY words loaded: SCALE CHORDS SIREN DRUM PAN TUNE"},
		{"AY? .\r", "-1  ok"},
		{"440 HZ>PERIOD .\r", "261  ok"},
		{"48 NOTE>PERIOD . 57 NOTE>PERIOD . 24 NOTE>PERIOD .\r", "440 261 1761  ok"},
		{"2 STEREO 16 AY@ .\r", "34  ok"},
		{"0 STEREO 16 AY@ .\r", "16  ok"},
		{"VIEW TONE\r", ": TONE"},
		{"10 LIST\r", "NOTE>PERIOD   note n's period"}, // screen 3's shadow
	};
	f83_lines(ay, sizeof ay / sizeof ay[0]);
	CHECK_EQ(ay_stereo(&host_ay), AY_STEREO_MONO);
	host_ay_clear();
	static const char *const scale[][2] = {{"SCALE\r", "SCALE  ok"}}; // not VIEW's " ok"
	f83_lines(scale, 1);
	static const uint16_t want[] = {440, 392, 349, 329, 293, 261, 233, 220}; // C4 to C5
	uint16_t got[16];
	CHECK_EQ(ay_periods(0, got, 16), 8);
	CHECK(memcmp(got, want, sizeof want) == 0);
	CHECK_EQ(ay_read(&host_ay, 8), 0); // QUIET at the end
	static const char *const bye[][2] = {
		{"' CHORDS ' SIREN ' DRUM ' PAN ' TUNE 2DROP 2DROP DROP\r", " ok"},
		{"BYE\r", "B>"},
	};
	f83_lines(bye, sizeof bye / sizeof bye[0]);
	CHECK(strstr(screen, "Open error") == NULL);

	drives_unmount_all();
	CHECK_EQ(diskimg_install(diskimg_find("CPM31"), true, window, NULL, NULL), DISKIMG_WRITTEN);
	CHECK_EQ(drives_mount(0, "CPM31"), DRIVES_OK);
	CHECK_EQ(drives_mount(1, "F83"), DRIVES_OK);
	cpm3_boot();
	static const char *const cpm3[][2] = {
		{"F83\r", "Version 2.1.0"},
		{"VIEW DUP\r", "Scr # 19         B:KERNEL80.BLK"},
		{"BYE\r", "A>"},
	};
	f83_lines(cpm3, sizeof cpm3 / sizeof cpm3[0]);
	CHECK(strstr(screen, "Open error") == NULL);
	drives_unmount_all();
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
	RUN(test_drive_sets);
	RUN(test_benchmark);
	RUN(test_worms);
	RUN(test_bye_halts);
	RUN(test_no_disk_boot_rom);
	RUN(test_monitor_format);
	RUN(test_monitor_fresh_card);
	RUN(test_monitor_autoboot);
	RUN(test_monitor_no_firmware_update);
	RUN(test_firmware_panel);
	RUN(test_firmware_panel_stick);
	RUN(test_blank_disk_chunks);
	RUN(test_xfer_port);
	RUN(test_xfer_tools);
	RUN(test_hwinfo_port);
	RUN(test_clock_ports);
	RUN(test_hwinfo_tool);
	RUN(test_ay_ports);
	RUN(test_aytest_tool);
	RUN(test_ayplay_tool);
	RUN(test_boot_cpm3);
	RUN(test_cpm3_tools);
	RUN(test_cpm3_date);
	RUN(test_cpm3_drives);
	RUN(test_hd_guard);
	RUN(test_mem_banks);
	RUN(test_termtest);
	RUN(test_altscr);
	RUN(test_aytrack_tool);
	RUN(test_f83);
	drives_unmount_all();
	f_unmount("");
	diskio_ram_free();
	return tests_done();
}
