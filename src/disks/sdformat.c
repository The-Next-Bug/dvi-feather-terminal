#include "sdformat.h"

#define GIB_SECTORS (1024u * 1024 * 1024 / 512)

void sdformat_choose(uint32_t sectors, sdformat_t *f) {
	// Two FATs, as every SD card comes; 512 root entries on FAT12/16.
	f->parm = (MKFS_PARM){.n_fat = 2};
	if (sectors < 2 * GIB_SECTORS) {
		// SDSC: FAT12 or FAT16 by the cluster count, FatFs's cluster size;
		// the data area on 64 KB.
		f->parm.fmt = FM_FAT;
		f->parm.align = 128;
		f->name = "FAT12/16";
		return;
	}
	// SDHC/SDXC: FAT32, the data area on 4 MB (the cards' allocation unit).
	// FAT32 needs 65525 clusters: 4 KB ones below 4 GB. Above 64 GB, 64 KB
	// clusters halve the FATs (and the time to write them).
	f->parm.fmt = FM_FAT32;
	f->parm.align = 8192;
	if (sectors < 4 * GIB_SECTORS) {
		f->parm.au_size = 4096;
		f->name = "FAT32, 4 KB clusters";
	} else if (sectors <= 64 * GIB_SECTORS) {
		f->parm.au_size = 32768;
		f->name = "FAT32, 32 KB clusters";
	} else {
		f->parm.au_size = 65536;
		f->name = "FAT32, 64 KB clusters";
	}
}

FRESULT sdformat_run(const char *path, const sdformat_t *f, void *work, size_t len) {
	f_unmount(path);
	return f_mkfs(path, &f->parm, work, (UINT)len);
}
