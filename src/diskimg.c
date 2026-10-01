#include "diskimg.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "ff.h"
#include "lz.h"

typedef struct {
	FIL *file;
	uint32_t crc, done, total;
	bool failed;
	diskimg_progress_t progress;
	void *ctx;
} sink_t;

static bool write_chunk(void *ctx, const uint8_t *data, size_t len) {
	sink_t *s = ctx;
	UINT written;
	s->crc = crc32_update(s->crc, data, len);
	if (f_write(s->file, data, (UINT)len, &written) != FR_OK || written != len) {
		s->failed = true;
		return false;
	}
	s->done += (uint32_t)len;
	if (s->progress)
		s->progress(s->ctx, s->done, s->total);
	return true;
}

const diskimg_t *diskimg_find(const char *name) {
	for (unsigned i = 0; i < diskimg_count; ++i) {
		const char *a = diskimg_list[i].name, *b = name;
		while (*a && toupper((unsigned char)*b) == *a)
			++a, ++b;
		if (!*a && !*b)
			return &diskimg_list[i];
	}
	return NULL;
}

diskimg_result_t diskimg_install(const diskimg_t *img, bool overwrite, uint8_t *window,
	diskimg_progress_t progress, void *ctx) {
	char path[32];
	snprintf(path, sizeof path, "/DISKS80/%s.DSK", img->name);
	FRESULT r = f_mkdir("/DISKS80");
	if (r != FR_OK && r != FR_EXIST)
		return DISKIMG_FAILED;

	static FIL file; // static: FIL holds a sector buffer, too big for the stack
	r = f_open(&file, path, FA_WRITE | (overwrite ? FA_CREATE_ALWAYS : FA_CREATE_NEW));
	if (r == FR_EXIST)
		return DISKIMG_EXISTS;
	if (r != FR_OK)
		return DISKIMG_FAILED;
	sink_t s = {.file = &file, .total = img->size, .progress = progress, .ctx = ctx};
	bool ok = lz_decode(img->data, img->data_len, img->size, window, write_chunk, &s) && !s.failed &&
		s.crc == img->crc;
	ok = f_close(&file) == FR_OK && ok;
	if (!ok) {
		f_unlink(path);
		return DISKIMG_FAILED;
	}
	return DISKIMG_WRITTEN;
}
