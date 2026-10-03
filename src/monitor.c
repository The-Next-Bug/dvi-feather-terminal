#include "monitor.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diskimg.h"
#include "drives.h"
#include "ff.h"
#include "lz.h"
#include "machine.h"
#include "scratch.h"

#define LIST_COLUMNS 5

typedef enum {
	MODE_MENU,
	MODE_DRIVE,     // "Mount on drive (A-H)?"
	MODE_NAME,      // the disk name for that drive
	MODE_NEW_NAME,  // N: the new disk's name
	MODE_NEW_FORMAT, // N: its format
	MODE_OVERWRITE, // "Replace the existing images (Y/N)?"
} monitor_mode_t;

static const monitor_platform_t *plat;
static FATFS fs;
static FRESULT mount_result = FR_NOT_READY;
static monitor_mode_t mode;
static uint8_t esc; // 0, 1 after ESC, 2 inside ESC [ / ESC O
static unsigned drive_asked;
static char line[DRIVES_NAME_MAX + 1];
static size_t line_len;
static bool can_resume;
static bool autobooting;
static uint32_t autoboot_at;

// Text as it is, any length.
static void put(const char *text) {
	plat->write(text);
}

// Formatted text, up to a line or so (longer is cut short: put long text).
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
	char text[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	put(text);
}

// Show what was written so far: the main loop does not run during long
// operations (writing disk images).
static void flush(void) {
	if (plat->flush)
		plat->flush();
}

// Progress of a long write: a dot per progress_step bytes.
#define PROGRESS_STEP (256u * 1024)
static unsigned dots;
static uint32_t progress_step;

static void progress_start(uint32_t step) {
	dots = 0;
	progress_step = step;
	flush();
}

static void progress(void *ctx, uint32_t done, uint32_t total) {
	(void)ctx;
	(void)total;
	if (dots < done / progress_step) {
		for (; dots < done / progress_step; ++dots)
			put(".");
		flush();
	}
}

// ---- the card and the drive assignments ----

static const char *fs_name(void) {
	switch (fs.fs_type) {
	case FS_FAT12:
		return "FAT12";
	case FS_FAT16:
		return "FAT16";
	case FS_FAT32:
		return "FAT32";
	default:
		return "FAT";
	}
}

static void mount_card(void) {
	drives_unmount_all();
	f_unmount("");
	mount_result = f_mount(&fs, "", 1);
	if (mount_result == FR_OK)
		drives_load_config();
}

// ---- the menu ----

static void show_card(void) {
	const char *info = plat->card_info();
	if (!info)
		say("SD card:  none found (SPI0: GP4 MISO, GP5 CS, GP6 SCK, GP7 MOSI)\r\n");
	else if (mount_result != FR_OK)
		say("SD card:  %s, no FAT file system (error %d)\r\n", info, (int)mount_result);
	else
		say("SD card:  %s, %s\r\n", info, fs_name());
}

static void show_drives(void) {
	for (unsigned d = 0; d < DRIVES; ++d) {
		const char *name = drives_name(d);
		char text[24];
		if (name)
			snprintf(text, sizeof text, "%s (%s)", name, drives_geometry(drives_format(d))->short_name);
		else
			snprintf(text, sizeof text, "-");
		say("%s %c: %-15s", d % 4 ? "" : d ? "\r\n        " : "Drives: ", 'A' + d, text);
	}
	say("\r\n");
}

static void show_menu(void) {
	say("\r\nZ80 CP/M machine (z80pack Z80 core)\r\n");
	show_card();
	show_drives();
	put("\r\n  Return  boot from drive A        M  mount a disk on a drive\r\n");
	if (can_resume)
		put("  R       resume the running CPU\r\n");
	put("  L       list the disks            N  new blank disk (8\", 4 MB, 8 MB)\r\n");
	put("  I       install the built-in disks\r\n");
	put("  S       CPU speed test            C  check the card again\r\n");
	put("  F11 resets the machine; F12 SET-UP (Drives, Mode, firmware update)\r\n> ");
}

static void list_disks(void) {
	if (mount_result != FR_OK) {
		say("No card.\r\n");
		return;
	}
	static char names[DRIVES_LIST_MAX][DRIVES_NAME_MAX + 1];
	unsigned n = drives_list(names, DRIVES_LIST_MAX);
	for (unsigned i = 0; i < n; ++i) {
		const drives_geometry_t *g = drives_geometry(drives_image_format(names[i]));
		say("%-9s%-4s%s", names[i], g ? g->short_name : "?", (i + 1) % LIST_COLUMNS ? " " : "\r\n");
	}
	if (n % LIST_COLUMNS)
		say("\r\n");
	if (!n)
		say("No disk images in /DISKS80: I installs the built-in ones, N makes a blank one.\r\n");
	else
		say("(FD 8\" floppy, 4MB/8MB hard disks, ? not a disk image)\r\n");
}

static_assert(LZ_WINDOW <= SCRATCH_SIZE, "the LZ window is the scratch buffer");

static void install(bool overwrite) {
	if (mount_result != FR_OK) {
		say("No card.\r\n");
		return;
	}
	// The LZ window.
	uint8_t *window = scratch_acquire();
	if (!window) {
		say("Busy.\r\n");
		return;
	}
	drives_flush();
	for (unsigned i = 0; i < diskimg_count; ++i) {
		const diskimg_t *img = &diskimg_list[i];
		// A mounted image is closed while it is replaced.
		int mounted_on = -1;
		for (unsigned d = 0; d < DRIVES; ++d)
			if (overwrite && drives_name(d) && strcmp(drives_name(d), img->name) == 0)
				mounted_on = (int)d;
		if (mounted_on >= 0)
			drives_unmount((unsigned)mounted_on);
		say("%-9s", img->name);
		progress_start(PROGRESS_STEP);
		diskimg_result_t r = diskimg_install(img, overwrite, window, progress, NULL);
		if (dots)
			put(" ");
		put(r == DISKIMG_WRITTEN ? "written\r\n" : r == DISKIMG_EXISTS ? "already there\r\n" : "FAILED\r\n");
		if (mounted_on >= 0)
			drives_mount((unsigned)mounted_on, img->name);
	}
	scratch_release(window);
	// Empty drives get the boot and test disks.
	if (!drives_name(0) || !drives_name(1)) {
		if (!drives_name(0))
			drives_mount(0, "CPM22");
		if (!drives_name(1))
			drives_mount(1, "Z80TESTS");
		drives_save_config();
	}
	can_resume = false; // the disks changed under it
}

static bool any_installed(void) {
	for (unsigned i = 0; i < diskimg_count; ++i) {
		char path[32];
		FILINFO info;
		snprintf(path, sizeof path, "/DISKS80/%s.DSK", diskimg_list[i].name);
		if (f_stat(path, &info) == FR_OK)
			return true;
	}
	return false;
}

static monitor_action_t boot(void) {
	if (!drives_name(0)) {
		say("No disk in drive A: M mounts one.\r\n> ");
		return MONITOR_NONE;
	}
	say("\r\nBooting from A: %s\r\n", drives_name(0));
	machine_reset();
	can_resume = true;
	return MONITOR_RUN;
}

static void speed_test(void) {
	say("Running JP loops for a second... ");
	uint32_t khz = machine_benchmark_khz(1000);
	say("%u.%02u MHz\r\n", (unsigned)(khz / 1000), (unsigned)(khz % 1000 / 10));
}

static monitor_action_t menu_key(uint8_t c) {
	switch (toupper(c)) {
	case '\r':
	case '\n':
		return boot();
	case 'R':
		if (!can_resume)
			break;
		say("R\r\n");
		return MONITOR_RUN;
	case 'M':
		say("M\r\nMount on drive (A-H)? ");
		mode = MODE_DRIVE;
		return MONITOR_NONE;
	case 'N':
		if (mount_result != FR_OK)
			break;
		say("N\r\nName of the new disk? ");
		line_len = 0;
		line[0] = '\0';
		mode = MODE_NEW_NAME;
		return MONITOR_NONE;
	case 'L':
		say("L\r\n");
		list_disks();
		break;
	case 'I':
		say("I\r\n");
		if (mount_result == FR_OK && any_installed()) {
			say("Replace the images already on the card, losing changes to them (Y/N)? ");
			mode = MODE_OVERWRITE;
			return MONITOR_NONE;
		}
		install(false);
		break;
	case 'S':
		say("S\r\n");
		speed_test();
		break;
	case 'C':
		say("C\r\n");
		mount_card();
		can_resume = false;
		break;
	default:
		return MONITOR_NONE;
	}
	show_menu();
	return MONITOR_NONE;
}

static void name_done(bool cancelled) {
	mode = MODE_MENU;
	if (cancelled) {
		say("\r\n");
	} else if (!line_len) {
		drives_unmount(drive_asked);
		drives_save_config();
		say("\r\n%c: is empty\r\n", 'A' + drive_asked);
	} else {
		static const char *const why[] = {
			[DRIVES_NOT_FOUND] = "no such disk (L lists them)",
			[DRIVES_BAD_IMAGE] = "not a disk image (wrong size)",
			[DRIVES_IN_USE] = "mounted on another drive",
			[DRIVES_EXISTS] = "exists",
			[DRIVES_IO_ERROR] = "card error",
		};
		drives_result_t r = drives_mount(drive_asked, line);
		if (r == DRIVES_OK) {
			drives_save_config();
			say("\r\n");
		} else {
			say("\r\n%s: %s\r\n", line, why[r]);
		}
	}
	show_menu();
}

static void line_done(bool cancelled) {
	if (mode == MODE_NAME) {
		name_done(cancelled);
	} else if (cancelled || !line_len) {
		mode = MODE_MENU;
		say("\r\n");
		show_menu();
	} else {
		mode = MODE_NEW_FORMAT;
		say("\r\nFormat: 1 8\" floppy (250 KB), 2 4 MB, 3 8 MB hard disk? ");
	}
}

static void create_disk(drives_format_t f) {
	mode = MODE_MENU;
	const drives_geometry_t *g = drives_geometry(f);
	uint8_t *buf = scratch_acquire(); // what is written, a chunk at a time
	if (!buf) {
		say("%s\r\nBusy.\r\n", g->name);
		show_menu();
		return;
	}
	say("%s\r\nWriting %s.DSK (%lu KB)... ", g->name, line, (unsigned long)(g->size / 1024));
	static const char *const why[] = {
		[DRIVES_NOT_FOUND] = "bad name",
		[DRIVES_EXISTS] = "there is a disk of that name",
		[DRIVES_IO_ERROR] = "card error",
	};
	progress_start(PROGRESS_STEP);
	drives_result_t r = drives_create(line, f, buf, SCRATCH_SIZE, progress, NULL);
	scratch_release(buf);
	say("%s%s\r\n", dots ? " " : "", r == DRIVES_OK ? "done: M mounts it" : why[r] ? why[r] : "failed");
	show_menu();
}

static monitor_action_t line_key(uint8_t c) {
	if (c == '\r' || c == '\n') {
		line_done(false);
	} else if (c == 0x7f || c == 0x08) {
		if (line_len) {
			line[--line_len] = '\0';
			say("\b \b");
		}
	} else if (c == 0x03) { // ^C
		line_done(true);
	} else if (isalnum(c) && line_len < DRIVES_NAME_MAX) {
		line[line_len++] = (char)toupper(c);
		line[line_len] = '\0';
		say("%c", toupper(c));
	}
	return MONITOR_NONE;
}

monitor_action_t monitor_key(uint8_t c, uint32_t now_ms) {
	(void)now_ms;
	if (autobooting) {
		autobooting = false;
		show_menu();
		return MONITOR_NONE;
	}
	if (esc == 1) {
		esc = c == '[' || c == 'O' ? 2 : 0;
		if (esc)
			return MONITOR_NONE;
	} else if (esc == 2) {
		if (c >= 0x40 && c <= 0x7e)
			esc = 0;
		return MONITOR_NONE;
	}
	if (c == 0x1b) {
		esc = 1;
		if (mode != MODE_MENU) { // Escape cancels a question
			mode = MODE_MENU;
			say("\r\n");
			show_menu();
		}
		return MONITOR_NONE;
	}

	switch (mode) {
	case MODE_MENU:
		return menu_key(c);
	case MODE_DRIVE: {
		unsigned d = (unsigned)(toupper(c) - 'A');
		if (d < DRIVES) {
			drive_asked = d;
			line_len = 0;
			line[0] = '\0';
			mode = MODE_NAME;
			say("%c\r\nDisk for %c: (Return alone: none)? ", 'A' + d, 'A' + d);
		}
		return MONITOR_NONE;
	}
	case MODE_NAME:
	case MODE_NEW_NAME:
		return line_key(c);
	case MODE_NEW_FORMAT:
		if (c >= '1' && c <= '3')
			create_disk((drives_format_t)(DRIVES_FLOPPY + (c - '1')));
		return MONITOR_NONE;
	case MODE_OVERWRITE:
		if (toupper(c) == 'Y' || toupper(c) == 'N') {
			say("%c\r\n", toupper(c));
			mode = MODE_MENU;
			install(toupper(c) == 'Y');
			show_menu();
		}
		return MONITOR_NONE;
	}
	return MONITOR_NONE;
}

monitor_action_t monitor_tick(uint32_t now_ms) {
	if (autobooting && (int32_t)(now_ms - autoboot_at) >= 0) {
		autobooting = false;
		return boot();
	}
	return MONITOR_NONE;
}

void monitor_start(const monitor_platform_t *platform, bool autoboot, uint32_t now_ms) {
	plat = platform;
	mode = MODE_MENU;
	esc = 0;
	can_resume = false;
	mount_card();
	if (autoboot && drives_name(0)) {
		say("\r\nZ80 CP/M machine\r\n");
		show_card();
		show_drives();
		say("Booting from A: in %u s, any key for the menu\r\n", MONITOR_AUTOBOOT_MS / 1000);
		autobooting = true;
		autoboot_at = now_ms + MONITOR_AUTOBOOT_MS;
		return;
	}
	autobooting = false;
	show_menu();
	if (mount_result == FR_OK && !drives_name(0))
		say("\r\nNo boot disk: I installs CP/M 2.2 on the card.\r\n> ");
}

void monitor_enter(const char *reason, bool resume) {
	drives_flush();
	mode = MODE_MENU;
	esc = 0;
	autobooting = false;
	can_resume = resume;
	if (reason)
		say("\r\n\r\n*** %s\r\n", reason);
	show_menu();
}
