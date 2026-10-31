// The drives' configuration on the card (drives.h): the images in
// /DISKS80, DRIVES.CFG and the named drive sets in /DISKS80/SETS. Split
// from drives.c, whose sector I/O runs from SRAM: this runs only for the
// monitor and SET-UP's Drives panel, so it stays in flash (cold code).

#include "drives.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"

#define CONFIG_PATH "/DISKS80/DRIVES.CFG"
#define SETS_DIR "/DISKS80/SETS"

static int compare_names(const void *a, const void *b) {
	return strcmp(a, b);
}

// The base names of the files matching pattern in folder (sorted).
static unsigned list(const char *folder, const char *pattern, char names[][DRIVES_NAME_MAX + 1], unsigned max) {
	DIR dir;
	FILINFO info;
	unsigned n = 0;
	FRESULT r = f_findfirst(&dir, &info, folder, pattern);
	for (; r == FR_OK && info.fname[0] && n < max; r = f_findnext(&dir, &info)) {
		char *dot = strchr(info.fname, '.');
		if (dot) {
			*dot = '\0';
		}
		if (strlen(info.fname) <= DRIVES_NAME_MAX) {
			strcpy(names[n++], info.fname);
		}
	}
	f_closedir(&dir);
	qsort(names, n, sizeof names[0], compare_names);
	return n;
}

unsigned drives_list(char names[][DRIVES_NAME_MAX + 1], unsigned max) {
	unsigned n = list("/DISKS80", "*.DSK", names, max);
	if (!drives_ramdisk_format() || max == 0) {
		return n;
	}
	// The RAM disk in place of a RAMDISK.DSK, else in its place in the
	// order (the last image making room when the list is full).
	unsigned at = 0;
	while (at < n && strcmp(names[at], DRIVES_RAMDISK) < 0) {
		++at;
	}
	if (at < n && strcmp(names[at], DRIVES_RAMDISK) == 0) {
		return n;
	}
	if (n == max) {
		--n;
		if (at > n) {
			at = n;
		}
	}
	memmove(names[at + 1], names[at], (n - at) * sizeof names[0]);
	strcpy(names[at], DRIVES_RAMDISK);
	return n + 1;
}

// DRIVES.CFG and the sets: a line a drive, A=NAME (A= for none).
static bool write_drives(const char *path, const char images[DRIVES][DRIVES_NAME_MAX + 1]) {
	FIL f;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		return false;
	}
	char text[DRIVES * (DRIVES_NAME_MAX + 4) + 1] = "";
	for (unsigned d = 0; d < DRIVES; ++d) {
		size_t n = strlen(text);
		snprintf(text + n, sizeof text - n, "%c=%s\r\n", 'A' + d, images[d]);
	}
	UINT written;
	bool ok = f_write(&f, text, (UINT)strlen(text), &written) == FR_OK && written == strlen(text);
	return f_close(&f) == FR_OK && ok;
}

// Lines in another order, lower case or with blanks round the name are
// read too (the files may be edited by hand); names that are not image
// names are left out.
static bool read_drives(const char *path, char images[DRIVES][DRIVES_NAME_MAX + 1]) {
	memset(images, 0, DRIVES * (DRIVES_NAME_MAX + 1));
	FIL f;
	if (f_open(&f, path, FA_READ) != FR_OK) {
		return false;
	}
	char text[DRIVES * (DRIVES_NAME_MAX + 8) + 64];
	UINT n = 0;
	f_read(&f, text, sizeof text - 1, &n);
	f_close(&f);
	text[n] = '\0';
	for (char *p = strtok(text, "\r\n"); p; p = strtok(NULL, "\r\n")) {
		while (*p == ' ' || *p == '\t') {
			++p;
		}
		unsigned d = (unsigned)(toupper((unsigned char)*p++) - 'A');
		while (*p == ' ' || *p == '\t') {
			++p;
		}
		if (d >= DRIVES || *p != '=') {
			continue;
		}
		char *name = p + 1;
		while (*name == ' ' || *name == '\t') {
			++name;
		}
		char *end = name + strlen(name);
		while (end > name && (end[-1] == ' ' || end[-1] == '\t')) {
			*--end = '\0';
		}
		if (*name && !drives_normalize(name, images[d])) {
			images[d][0] = '\0';
		}
	}
	return true;
}

void drives_save_config(void) {
	char images[DRIVES][DRIVES_NAME_MAX + 1];
	for (unsigned d = 0; d < DRIVES; ++d) {
		strcpy(images[d], drives_name(d) ? drives_name(d) : "");
	}
	write_drives(CONFIG_PATH, images);
}

bool drives_load_config(void) {
	drives_unmount_all();
	char images[DRIVES][DRIVES_NAME_MAX + 1];
	if (!read_drives(CONFIG_PATH, images)) {
		drives_mount(0, "CPM22");
		drives_mount(1, "Z80TESTS");
		return false;
	}
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (images[d][0]) {
			drives_mount(d, images[d]);
		}
	}
	return true;
}

bool drives_set_name_ok(const char *set) {
	size_t n = strlen(set);
	if (n == 0 || n > DRIVES_NAME_MAX) {
		return false;
	}
	for (size_t i = 0; i < n; ++i) {
		if (!isupper((unsigned char)set[i]) && !isdigit((unsigned char)set[i])) {
			return false;
		}
	}
	return true;
}

static bool set_path(char *path, size_t size, const char *set) {
	if (!drives_set_name_ok(set)) {
		return false;
	}
	snprintf(path, size, SETS_DIR "/%s.SET", set);
	return true;
}

unsigned drives_sets(char names[][DRIVES_NAME_MAX + 1], unsigned max) {
	return list(SETS_DIR, "*.SET", names, max);
}

bool drives_set_read(const char *set, char images[DRIVES][DRIVES_NAME_MAX + 1]) {
	char path[40];
	return set_path(path, sizeof path, set) && read_drives(path, images);
}

bool drives_set_write(const char *set, const char images[DRIVES][DRIVES_NAME_MAX + 1]) {
	char path[40];
	if (!set_path(path, sizeof path, set)) {
		return false;
	}
	FRESULT r = f_mkdir(SETS_DIR);
	if (r != FR_OK && r != FR_EXIST) {
		return false;
	}
	return write_drives(path, images);
}

bool drives_set_remove(const char *set) {
	char path[40];
	return set_path(path, sizeof path, set) && f_unlink(path) == FR_OK;
}

// ---- removing and renaming images (bd ajr) ----

// The drive image upper is mounted on, or DRIVES.
static unsigned mounted_on(const char *upper) {
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (drives_name(d) && strcmp(drives_name(d), upper) == 0) {
			return d;
		}
	}
	return DRIVES;
}

// upper's image, its path set; NOT_FOUND / RESERVED / OK.
static drives_result_t image_file(const char *name, char upper[DRIVES_NAME_MAX + 1], char *path, size_t size) {
	if (!drives_normalize(name, upper)) {
		return DRIVES_NOT_FOUND;
	}
	if (strcmp(upper, DRIVES_RAMDISK) == 0) {
		return DRIVES_RESERVED;
	}
	snprintf(path, size, "/DISKS80/%s.DSK", upper);
	return DRIVES_OK;
}

drives_result_t drives_remove(const char *name) {
	char upper[DRIVES_NAME_MAX + 1], path[32];
	FILINFO info;
	drives_result_t r = image_file(name, upper, path, sizeof path);
	if (r != DRIVES_OK) {
		return r;
	}
	if (f_stat(path, &info) != FR_OK) {
		return DRIVES_NOT_FOUND;
	}
	unsigned d = mounted_on(upper);
	if (d < DRIVES) {
		drives_unmount(d);
		drives_save_config();
	}
	return f_unlink(path) == FR_OK ? DRIVES_OK : DRIVES_IO_ERROR;
}

drives_result_t drives_rename(const char *from, const char *to) {
	char old_upper[DRIVES_NAME_MAX + 1], new_upper[DRIVES_NAME_MAX + 1], old_path[32], new_path[32];
	FILINFO info;
	drives_result_t r = image_file(from, old_upper, old_path, sizeof old_path);
	if (r != DRIVES_OK) {
		return r;
	}
	r = image_file(to, new_upper, new_path, sizeof new_path);
	if (r == DRIVES_RESERVED) {
		return DRIVES_EXISTS; // the RAM disk's name is taken
	}
	if (r != DRIVES_OK) {
		return r;
	}
	if (f_stat(old_path, &info) != FR_OK) {
		return DRIVES_NOT_FOUND;
	}
	if (strcmp(old_upper, new_upper) == 0) {
		return DRIVES_OK;
	}
	if (f_stat(new_path, &info) == FR_OK) {
		return DRIVES_EXISTS;
	}
	unsigned d = mounted_on(old_upper);
	if (d < DRIVES) {
		drives_unmount(d); // open files are not renamed
	}
	FRESULT fr = f_rename(old_path, new_path);
	if (d < DRIVES) {
		drives_mount(d, fr == FR_OK ? new_upper : old_upper);
		drives_save_config();
	}
	if (fr != FR_OK) {
		return DRIVES_IO_ERROR;
	}
	char sets[DRIVES_SETS_MAX][DRIVES_NAME_MAX + 1];
	unsigned n = drives_sets(sets, DRIVES_SETS_MAX);
	for (unsigned i = 0; i < n; ++i) {
		char images[DRIVES][DRIVES_NAME_MAX + 1];
		bool changed = false;
		if (!drives_set_read(sets[i], images)) {
			continue;
		}
		for (unsigned k = 0; k < DRIVES; ++k) {
			if (strcmp(images[k], old_upper) == 0) {
				strcpy(images[k], new_upper);
				changed = true;
			}
		}
		if (changed) {
			drives_set_write(sets[i], images);
		}
	}
	return DRIVES_OK;
}
