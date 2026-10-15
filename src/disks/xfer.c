#include "xfer.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "machine.h"

#define FOLDER "XFER"
#define CPM_EOF 0x1a

static struct {
	DIR dir;
	bool dir_open;
	uint8_t dir_volume, pattern[XFER_NAME];
	FIL file;
	bool file_open, writing;
	uint8_t file_volume, file_name[XFER_NAME];
} x;

static uint8_t status_of(FRESULT r) {
	switch (r) {
	case FR_OK:
		return XFER_ST_OK;
	case FR_NO_FILE:
	case FR_NO_PATH:
		return XFER_ST_NOT_FOUND;
	case FR_EXIST:
		return XFER_ST_EXISTS;
	case FR_NOT_ENABLED:
	case FR_NOT_READY:
	case FR_INVALID_DRIVE:
	case FR_NO_FILESYSTEM:
		return XFER_ST_NO_VOLUME;
	case FR_INVALID_NAME:
		return XFER_ST_BAD;
	default:
		return XFER_ST_IO_ERROR;
	}
}

// A character CP/M takes in a name (and FAT in a short one): letters,
// digits and !#$%&'()-@^`{}~, as bit masks (strchr on the list compiles to
// over a kilobyte of compares once inlined into the name loops).
static bool name_char(uint8_t c) {
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
		return true;
	}
	if (c >= 0x20 && c < 0x40) {
		return 0x23fau >> (c - 0x20) & 1; // ! # $ % & ' ( ) -
	}
	if (c >= 0x40 && c < 0x80) {
		return 0x6800000140000001ull >> (c - 0x40) & 1; // @ ^ ` { } ~
	}
	return false;
}

// An FCB name (or pattern, with ?) from the Z80, upper-cased and without
// CP/M's attribute bits; false if it is not one.
static bool fcb_name(const uint8_t *in, uint8_t out[XFER_NAME], bool pattern) {
	for (unsigned i = 0; i < XFER_NAME; ++i) {
		uint8_t c = in[i] & 0x7f;
		if (c >= 'a' && c <= 'z') {
			c = (uint8_t)(c - 'a' + 'A');
		}
		if (!(name_char(c) || c == ' ' || (pattern && c == '?'))) {
			return false;
		}
		out[i] = c;
	}
	// Blanks only pad: none inside the name or the type.
	for (unsigned i = 1; i < XFER_NAME; ++i) {
		if (i != 8 && out[i] != ' ' && out[i - 1] == ' ') {
			return false;
		}
	}
	return out[0] != ' ';
}

// "/XFER" or "/XFER/NAME.EXT" on the volume.
static void path_of(char *path, size_t size, uint8_t volume, const uint8_t *name) {
	int n = volume ? snprintf(path, size, "%u:/" FOLDER, volume) : snprintf(path, size, "/" FOLDER);
	if (!name || n < 0 || (size_t)n >= size) {
		return;
	}
	char *p = path + n;
	*p++ = '/';
	for (unsigned i = 0; i < 8 && name[i] != ' '; ++i) {
		*p++ = (char)name[i];
	}
	if (name[8] != ' ') {
		*p++ = '.';
		for (unsigned i = 8; i < XFER_NAME && name[i] != ' '; ++i) {
			*p++ = (char)name[i];
		}
	}
	*p = '\0';
}

// A FAT short name as an FCB name; false if CP/M could not take it.
static bool fat_name(const char *in, uint8_t out[XFER_NAME]) {
	memset(out, ' ', XFER_NAME);
	unsigned n = 0, end = 8; // where the next character goes, and the part's end
	for (const char *p = in; *p; ++p) {
		if (*p == '.' && end == 8) {
			n = 8;
			end = XFER_NAME;
			continue;
		}
		if (n >= end || !name_char((uint8_t)*p)) {
			return false;
		}
		out[n++] = (uint8_t)*p;
	}
	return out[0] != ' ';
}

static bool matches(const uint8_t name[XFER_NAME], const uint8_t pattern[XFER_NAME]) {
	for (unsigned i = 0; i < XFER_NAME; ++i) {
		if (pattern[i] != '?' && pattern[i] != name[i]) {
			return false;
		}
	}
	return true;
}

static void close_dir(void) {
	if (x.dir_open) {
		f_closedir(&x.dir);
	}
	x.dir_open = false;
}

static uint8_t close_file(void) {
	FRESULT r = FR_OK;
	if (x.file_open) {
		r = f_close(&x.file);
	}
	x.file_open = false;
	return status_of(r);
}

void xfer_reset(void) {
	close_dir();
	close_file();
}

void xfer_volume_gone(uint8_t volume) {
	if (x.dir_open && x.dir_volume == volume) {
		close_dir();
	}
	if (x.file_open && x.file_volume == volume) {
		close_file();
	}
}

static uint8_t find_next(uint8_t *buf) {
	if (!x.dir_open) {
		return XFER_ST_NOT_FOUND;
	}
	FILINFO info;
	for (;;) {
		FRESULT r = f_readdir(&x.dir, &info);
		if (r != FR_OK || !info.fname[0]) {
			close_dir();
			return r == FR_OK ? XFER_ST_NOT_FOUND : status_of(r);
		}
		uint8_t name[XFER_NAME];
		if (info.fattrib & (AM_DIR | AM_HID | AM_SYS) || !fat_name(info.fname, name) || !matches(name, x.pattern)) {
			continue;
		}
		memcpy(buf, name, XFER_NAME);
		uint32_t size = (uint32_t)info.fsize;
		for (unsigned i = 0; i < 4; ++i) {
			buf[XFER_NAME + i] = (uint8_t)(size >> 8 * i);
		}
		return XFER_ST_OK;
	}
}

// Cut the file being written at the first ^Z of its last record.
static uint8_t cut_at_eof(uint8_t *buf) {
	FSIZE_t size = f_size(&x.file);
	if (!size) {
		return XFER_ST_OK;
	}
	FSIZE_t start = (size - 1) / XFER_RECORD * XFER_RECORD;
	UINT got;
	FRESULT r = f_lseek(&x.file, start);
	if (r == FR_OK) {
		r = f_read(&x.file, buf, (UINT)(size - start), &got);
	}
	if (r != FR_OK) {
		return status_of(r);
	}
	for (UINT i = 0; i < got; ++i) {
		if (buf[i] == CPM_EOF) {
			r = f_lseek(&x.file, start + i);
			if (r == FR_OK) {
				r = f_truncate(&x.file);
			}
			return status_of(r);
		}
	}
	return XFER_ST_OK;
}

uint8_t xfer_command(uint8_t cmd, uint8_t volume, uint8_t *buf) {
	uint8_t name[XFER_NAME];
	char path[32];
	FRESULT r;
	switch (cmd) {
	case XFER_RESET:
		xfer_reset();
		return XFER_ST_OK;
	case XFER_FIND_FIRST:
		close_dir();
		if (!fcb_name(buf, x.pattern, true)) {
			return XFER_ST_BAD;
		}
		path_of(path, sizeof path, volume, NULL);
		r = f_opendir(&x.dir, path);
		if (r != FR_OK) {
			return status_of(r);
		}
		x.dir_open = true;
		x.dir_volume = volume;
		return find_next(buf);
	case XFER_FIND_NEXT:
		return find_next(buf);
	case XFER_OPEN:
	case XFER_CREATE:
		if (!fcb_name(buf, name, false)) {
			return XFER_ST_BAD;
		}
		close_file();
		if (cmd == XFER_CREATE) {
			path_of(path, sizeof path, volume, NULL);
			r = f_mkdir(path);
			if (r != FR_OK && r != FR_EXIST) {
				return status_of(r);
			}
		}
		path_of(path, sizeof path, volume, name);
		r = f_open(&x.file, path, cmd == XFER_OPEN ? FA_READ : FA_CREATE_NEW | FA_WRITE | FA_READ);
		if (r != FR_OK) {
			return status_of(r);
		}
		x.file_open = true;
		x.writing = cmd == XFER_CREATE;
		x.file_volume = volume;
		memcpy(x.file_name, name, XFER_NAME);
		return XFER_ST_OK;
	case XFER_READ: {
		if (!x.file_open || x.writing) {
			return XFER_ST_BAD;
		}
		UINT got;
		r = f_read(&x.file, buf, XFER_RECORD, &got);
		if (r != FR_OK) {
			return status_of(r);
		}
		if (!got) {
			return XFER_ST_EOF;
		}
		memset(buf + got, CPM_EOF, XFER_RECORD - got);
		return XFER_ST_OK;
	}
	case XFER_WRITE: {
		if (!x.file_open || !x.writing) {
			return XFER_ST_BAD;
		}
		UINT put;
		r = f_write(&x.file, buf, XFER_RECORD, &put);
		if (r != FR_OK) {
			return status_of(r);
		}
		return put == XFER_RECORD ? XFER_ST_OK : XFER_ST_IO_ERROR; // the volume is full
	}
	case XFER_CLOSE:
		return close_file();
	case XFER_CLOSE_TEXT: {
		uint8_t st = x.file_open && x.writing ? cut_at_eof(buf) : XFER_ST_OK;
		uint8_t closed = close_file();
		return st != XFER_ST_OK ? st : closed;
	}
	case XFER_DELETE:
		if (!fcb_name(buf, name, false)) {
			return XFER_ST_BAD;
		}
		if (x.file_open && x.file_volume == volume && memcmp(x.file_name, name, XFER_NAME) == 0) {
			close_file();
		}
		path_of(path, sizeof path, volume, name);
		return status_of(f_unlink(path));
	default:
		return XFER_ST_BAD;
	}
}
