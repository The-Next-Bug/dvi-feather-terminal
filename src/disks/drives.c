#include "drives.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"

// FDC status codes (lib/z80pack/iodevices/sd-fdc.h).
#define FDC_STAT_OK 0
#define FDC_STAT_DISK 1
#define FDC_STAT_NODISK 2
#define FDC_STAT_SEEK 3
#define FDC_STAT_READ 4
#define FDC_STAT_WRITE 5
#define FDC_STAT_TRACK 8
#define FDC_STAT_SEC 9

#define FLUSH_DELAY_MS 250
#define BLANK 0xe5 // an erased CP/M directory and data

static const drives_geometry_t geometries[DRIVES_FORMATS] = {
	[DRIVES_FLOPPY] = {"8\" floppy", "FD", 77, 26, 77u * 26 * DRIVES_SECTOR},
	[DRIVES_HD4] = {"4 MB disk", "4MB", 255, 128, 255u * 128 * DRIVES_SECTOR},
	[DRIVES_HD8] = {"8 MB disk", "8MB", 256, 255, 256u * 255 * DRIVES_SECTOR},
};

static struct {
	FIL file;               // unused for the RAM disk
	drives_format_t format; // DRIVES_NONE: not mounted
	bool ram;               // the RAM disk
	bool dirty;
	uint8_t mounts; // images mounted on it (mod 256)
	char name[DRIVES_NAME_MAX + 1];
} drive[DRIVES];

static struct {
	uint8_t *memory;
	drives_format_t format; // DRIVES_NONE: none
} ramdisk;

static uint32_t last_write_ms;
static uint8_t activity; // bit per drive

const drives_geometry_t *drives_geometry(drives_format_t f) {
	return f > DRIVES_NONE && f < DRIVES_FORMATS ? &geometries[f] : NULL;
}

drives_format_t drives_format_of_size(uint32_t size) {
	for (drives_format_t f = DRIVES_FLOPPY; f < DRIVES_FORMATS; ++f) {
		if (geometries[f].size == size) {
			return f;
		}
	}
	return DRIVES_NONE;
}

drives_format_t drives_format_fit(size_t size) {
	for (drives_format_t f = DRIVES_FORMATS - 1; f > DRIVES_NONE; --f) {
		if (geometries[f].size <= size) {
			return f;
		}
	}
	return DRIVES_NONE;
}

void drives_set_ramdisk(uint8_t *memory, drives_format_t f) {
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (drive[d].ram) {
			drives_unmount(d);
		}
	}
	ramdisk.format = memory && drives_geometry(f) ? f : DRIVES_NONE;
	ramdisk.memory = ramdisk.format ? memory : NULL;
	if (ramdisk.memory) {
		memset(ramdisk.memory, BLANK, geometries[f].size);
	}
}

drives_format_t drives_ramdisk_format(void) {
	return ramdisk.format;
}

bool drives_normalize(const char *in, char out[DRIVES_NAME_MAX + 1]) {
	size_t n = strlen(in);
	if (n == 0 || n > DRIVES_NAME_MAX) {
		return false;
	}
	for (size_t i = 0; i <= n; ++i) {
		out[i] = (char)toupper((unsigned char)in[i]);
	}
	return strpbrk(out, "./\\:*? ") == NULL;
}

static void image_path(char *path, size_t size, const char *upper) {
	snprintf(path, size, "/DISKS80/%.8s.DSK", upper);
}

drives_result_t drives_mount(unsigned d, const char *name) {
	char upper[DRIVES_NAME_MAX + 1];
	if (d >= DRIVES || !drives_normalize(name, upper)) {
		return DRIVES_NOT_FOUND;
	}
	for (unsigned i = 0; i < DRIVES; ++i) {
		if (i != d && drive[i].format && strcmp(drive[i].name, upper) == 0) {
			return DRIVES_IN_USE;
		}
	}
	drives_unmount(d);

	if (strcmp(upper, DRIVES_RAMDISK) == 0) {
		if (!ramdisk.format) {
			return DRIVES_NOT_FOUND;
		}
		drive[d].format = ramdisk.format;
		drive[d].ram = true;
		drive[d].dirty = false;
		++drive[d].mounts;
		strcpy(drive[d].name, upper);
		return DRIVES_OK;
	}
	char path[32];
	image_path(path, sizeof path, upper);
	FRESULT r = f_open(&drive[d].file, path, FA_READ | FA_WRITE);
	if (r == FR_DENIED || r == FR_WRITE_PROTECTED) { // read-only file or card
		r = f_open(&drive[d].file, path, FA_READ);
	}
	if (r == FR_NO_FILE || r == FR_NO_PATH || r == FR_INVALID_NAME) {
		return DRIVES_NOT_FOUND;
	}
	if (r != FR_OK) {
		return DRIVES_IO_ERROR;
	}
	drives_format_t f = drives_format_of_size((uint32_t)f_size(&drive[d].file));
	if (f == DRIVES_NONE) {
		f_close(&drive[d].file);
		return DRIVES_BAD_IMAGE;
	}
	drive[d].format = f;
	drive[d].dirty = false;
	++drive[d].mounts;
	strcpy(drive[d].name, upper);
	return DRIVES_OK;
}

void drives_unmount(unsigned d) {
	if (d >= DRIVES || !drive[d].format) {
		return;
	}
	if (!drive[d].ram) {
		f_close(&drive[d].file); // flushes
	}
	drive[d].format = DRIVES_NONE;
	drive[d].ram = false;
	drive[d].dirty = false;
}

void drives_unmount_all(void) {
	for (unsigned d = 0; d < DRIVES; ++d) {
		drives_unmount(d);
	}
}

const char *drives_name(unsigned d) {
	return d < DRIVES && drive[d].format ? drive[d].name : NULL;
}

drives_format_t drives_format(unsigned d) {
	return d < DRIVES ? drive[d].format : DRIVES_NONE;
}

uint8_t drives_mounts(unsigned d) {
	return d < DRIVES ? drive[d].mounts : 0;
}

drives_result_t drives_create(const char *name, drives_format_t f, uint8_t *buf, size_t buf_size,
	drives_progress_t progress, void *ctx) {
	char upper[DRIVES_NAME_MAX + 1], path[32];
	const drives_geometry_t *g = drives_geometry(f);
	if (!g || !drives_normalize(name, upper)) {
		return DRIVES_NOT_FOUND;
	}
	if (strcmp(upper, DRIVES_RAMDISK) == 0) {
		return DRIVES_EXISTS;
	}
	FRESULT r = f_mkdir("/DISKS80");
	if (r != FR_OK && r != FR_EXIST) {
		return DRIVES_IO_ERROR;
	}
	image_path(path, sizeof path, upper);
	static FIL file; // FIL holds a sector buffer: not on the small stack
	r = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
	if (r == FR_EXIST) {
		return DRIVES_EXISTS;
	}
	if (r != FR_OK) {
		return DRIVES_IO_ERROR;
	}
	memset(buf, BLANK, buf_size);
	bool ok = true;
	for (uint32_t done = 0; ok && done < g->size;) {
		UINT n = g->size - done < buf_size ? (UINT)(g->size - done) : (UINT)buf_size, written;
		ok = f_write(&file, buf, n, &written) == FR_OK && written == n;
		done += n;
		if (progress) {
			progress(ctx, done, g->size);
		}
	}
	ok = f_close(&file) == FR_OK && ok;
	if (!ok) {
		f_unlink(path);
		return DRIVES_IO_ERROR;
	}
	return DRIVES_OK;
}

drives_format_t drives_image_format(const char *name) {
	char upper[DRIVES_NAME_MAX + 1], path[32];
	FILINFO info;
	if (!drives_normalize(name, upper)) {
		return DRIVES_NONE;
	}
	if (strcmp(upper, DRIVES_RAMDISK) == 0) {
		return ramdisk.format;
	}
	image_path(path, sizeof path, upper);
	return f_stat(path, &info) == FR_OK ? drives_format_of_size((uint32_t)info.fsize) : DRIVES_NONE;
}

// Position drive d at the sector (the RAM disk: *pos, its offset), or
// return why not.
static uint8_t seek(unsigned d, unsigned track, unsigned sector, uint32_t *pos) {
	if (d >= DRIVES) {
		return FDC_STAT_DISK;
	}
	if (!drive[d].format) {
		return FDC_STAT_NODISK;
	}
	const drives_geometry_t *g = &geometries[drive[d].format];
	if (track >= g->tracks) {
		return FDC_STAT_TRACK;
	}
	if (sector < 1 || sector > g->sectors) {
		return FDC_STAT_SEC;
	}
	activity |= (uint8_t)(1u << d);
	*pos = ((uint32_t)track * g->sectors + sector - 1) * DRIVES_SECTOR;
	if (drive[d].ram) {
		return FDC_STAT_OK;
	}
	return f_lseek(&drive[d].file, *pos) == FR_OK ? FDC_STAT_OK : FDC_STAT_SEEK;
}

uint8_t drives_read(unsigned d, unsigned track, unsigned sector, uint8_t buf[DRIVES_SECTOR]) {
	uint32_t pos;
	uint8_t st = seek(d, track, sector, &pos);
	if (st != FDC_STAT_OK) {
		return st;
	}
	if (drive[d].ram) {
		memcpy(buf, ramdisk.memory + pos, DRIVES_SECTOR);
		return FDC_STAT_OK;
	}
	UINT n;
	return f_read(&drive[d].file, buf, DRIVES_SECTOR, &n) == FR_OK && n == DRIVES_SECTOR ? FDC_STAT_OK
		: FDC_STAT_READ;
}

uint8_t drives_write(unsigned d, unsigned track, unsigned sector, const uint8_t buf[DRIVES_SECTOR]) {
	uint32_t pos;
	uint8_t st = seek(d, track, sector, &pos);
	if (st != FDC_STAT_OK) {
		return st;
	}
	if (drive[d].ram) {
		memcpy(ramdisk.memory + pos, buf, DRIVES_SECTOR);
		return FDC_STAT_OK;
	}
	UINT n;
	if (f_write(&drive[d].file, buf, DRIVES_SECTOR, &n) != FR_OK || n != DRIVES_SECTOR) {
		return FDC_STAT_WRITE;
	}
	drive[d].dirty = true;
	return FDC_STAT_OK;
}

void drives_poll(uint32_t now_ms) {
	bool dirty = false;
	for (unsigned d = 0; d < DRIVES; ++d) {
		dirty |= drive[d].dirty;
	}
	if (!dirty) {
		last_write_ms = now_ms; // the delay counts from the first write seen
	} else if (now_ms - last_write_ms >= FLUSH_DELAY_MS) {
		drives_flush();
	}
}

bool drives_flush(void) {
	bool ok = true;
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (drive[d].dirty) {
			ok = f_sync(&drive[d].file) == FR_OK && ok;
			drive[d].dirty = false;
		}
	}
	return ok;
}

uint8_t drives_activity(void) {
	uint8_t a = activity;
	activity = 0;
	return a;
}
