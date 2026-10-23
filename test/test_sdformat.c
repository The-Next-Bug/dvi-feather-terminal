// sdformat on cards of any size: a sparse disk (only the sectors that are
// not all zeros are kept), standing in for the SD card as FatFs's drive 0.

#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "sdformat.h"
#include "test.h"

#define GIB (1024u * 1024 * 1024 / 512) // in sectors

static struct {
	uint32_t lba;
	uint8_t data[512];
} *kept;
static size_t n_kept, max_kept;
static uint32_t disk_sectors;
static uint64_t sectors_written;

static uint8_t *find(uint32_t lba) {
	for (size_t i = 0; i < n_kept; ++i) {
		if (kept[i].lba == lba) {
			return kept[i].data;
		}
	}
	return NULL;
}

static void disk_new(uint32_t sectors) {
	free(kept);
	kept = NULL;
	n_kept = max_kept = 0;
	disk_sectors = sectors;
	sectors_written = 0;
}

DSTATUS disk_status(BYTE pdrv) {
	return pdrv == 0 && disk_sectors ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || sector + count > disk_sectors) {
		return RES_PARERR;
	}
	for (UINT i = 0; i < count; ++i) {
		const uint8_t *d = find((uint32_t)(sector + i));
		if (d) {
			memcpy(buff + i * 512, d, 512);
		} else {
			memset(buff + i * 512, 0, 512);
		}
	}
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || sector + count > disk_sectors) {
		return RES_PARERR;
	}
	static const uint8_t zero[512];
	for (UINT i = 0; i < count; ++i) {
		const uint8_t *src = buff + i * 512;
		uint8_t *d = find((uint32_t)(sector + i));
		if (!d && memcmp(src, zero, 512) == 0) {
			continue;
		}
		if (!d) {
			if (n_kept == max_kept) {
				max_kept = max_kept ? max_kept * 2 : 64;
				kept = realloc(kept, max_kept * sizeof *kept);
			}
			kept[n_kept].lba = (uint32_t)(sector + i);
			d = kept[n_kept++].data;
		}
		memcpy(d, src, 512);
	}
	sectors_written += count;
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv != 0) {
		return RES_PARERR;
	}
	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = disk_sectors;
		return RES_OK;
	case GET_SECTOR_SIZE:
		*(WORD *)buff = 512;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1;
		return RES_OK;
	default:
		return RES_PARERR;
	}
}

DWORD get_fattime(void) {
	return 46u << 25 | 10u << 21 | 23u << 16;
}

static void test_choose(void) {
	static const struct {
		uint32_t sectors;
		BYTE fmt;
		DWORD au;
		UINT align;
	} cases[] = {
		{16 * 2048, FM_FAT, 0, 128},            // 16 MB
		{2 * GIB - 1, FM_FAT, 0, 128},          // SDSC's largest
		{2 * GIB, FM_FAT32, 4096, 8192},
		{4 * GIB - 1, FM_FAT32, 4096, 8192},
		{4 * GIB, FM_FAT32, 32768, 8192},
		{15u * GIB, FM_FAT32, 32768, 8192},     // a "16 GB" card
		{64u * GIB, FM_FAT32, 32768, 8192},
		{64u * GIB + 1, FM_FAT32, 65536, 8192},
		{0xffffffffu, FM_FAT32, 65536, 8192},   // 2 TB, the most FatFs addresses here
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		sdformat_t f;
		sdformat_choose(cases[i].sectors, &f);
		CHECK_EQ(f.parm.fmt, cases[i].fmt);
		CHECK_EQ(f.parm.au_size, cases[i].au);
		CHECK_EQ(f.parm.align, cases[i].align);
		CHECK_EQ(f.parm.n_fat, 2);
		CHECK(f.name != NULL);
	}
}

// Format a card of that size, mount it, write a file, read the layout.
static void format_card(uint32_t sectors, BYTE want_type, uint32_t want_cluster) {
	static FATFS fs;
	static uint8_t work[4096];
	disk_new(sectors);
	sdformat_t f;
	sdformat_choose(sectors, &f);
	CHECK_EQ(sdformat_run("", &f, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
	CHECK_EQ(fs.fs_type, want_type);
	CHECK_EQ(fs.csize * 512u, want_cluster);
	CHECK_EQ(fs.n_fats, 2);
	// One partition from the MBR; the clusters start on the alignment.
	uint8_t mbr[512];
	CHECK_EQ(disk_read(0, mbr, 0, 1), RES_OK);
	CHECK(mbr[510] == 0x55 && mbr[511] == 0xaa);
	CHECK(mbr[446 + 4] != 0);       // partition 1's type
	CHECK_EQ(mbr[446 + 16 + 4], 0); // no partition 2
	CHECK_EQ(fs.database % f.parm.align, 0);

	FIL file;
	UINT n;
	CHECK_EQ(f_open(&file, "/HELLO.TXT", FA_WRITE | FA_CREATE_NEW), FR_OK);
	CHECK_EQ(f_write(&file, "hello", 5, &n), FR_OK);
	CHECK_EQ(f_close(&file), FR_OK);
	DWORD free_clusters;
	FATFS *pfs;
	CHECK_EQ(f_getfree("", &free_clusters, &pfs), FR_OK);
	CHECK((uint64_t)free_clusters * fs.csize > (uint64_t)sectors * 9 / 10);
	f_unmount("");
}

static void test_format(void) {
	// Up to 2 GB FatFs picks: FAT16 while there are clusters enough.
	format_card(64 * 2048, FS_FAT16, 4096);
	format_card(8 * 2048, FS_FAT16, 1024);
	format_card(1900 * 2048, FS_FAT16, 32768);
	format_card(2048, FS_FAT12, 512); // 1 MB
	format_card(3 * GIB, FS_FAT32, 4096);
	format_card(15u * GIB, FS_FAT32, 32768);
	format_card(119u * GIB, FS_FAT32, 65536); // a "128 GB" card
}

// A card with a file system already: formatting empties it.
static void test_reformat(void) {
	static FATFS fs;
	static uint8_t work[4096];
	sdformat_t f;
	disk_new(32 * 2048);
	sdformat_choose(32 * 2048, &f);
	CHECK_EQ(sdformat_run("", &f, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
	CHECK_EQ(f_mkdir("/DISKS80"), FR_OK);
	CHECK_EQ(sdformat_run("", &f, work, sizeof work), FR_OK); // unmounts it
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
	FILINFO info;
	CHECK_EQ(f_stat("/DISKS80", &info), FR_NO_FILE);
	f_unmount("");

	// Too small for any FAT: an error, not a crash.
	disk_new(100);
	CHECK(sdformat_run("", &f, work, sizeof work) != FR_OK);
	disk_new(0);
	CHECK_EQ(sdformat_run("", &f, work, sizeof work), FR_NOT_READY);
}

int main(void) {
	RUN(test_choose);
	RUN(test_format);
	RUN(test_reformat);
	free(kept);
	return tests_done();
}
