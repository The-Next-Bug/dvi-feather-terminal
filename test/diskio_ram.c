// FatFs disk I/O for the host tests: one volume in a RAM buffer
// (diskio_ram.h), standing in for the SD card.

#include "diskio_ram.h"

#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"

static uint8_t *ram;
static size_t ram_sectors;
static unsigned max_write;

void diskio_ram_init(size_t sectors) {
	free(ram);
	ram = calloc(sectors, 512);
	ram_sectors = ram ? sectors : 0;
}

void diskio_ram_free(void) {
	free(ram);
	ram = NULL;
	ram_sectors = 0;
}

unsigned diskio_ram_max_write(void) {
	unsigned n = max_write;
	max_write = 0;
	return n;
}

DSTATUS disk_status(BYTE pdrv) {
	return pdrv == 0 && ram ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || !ram)
		return RES_NOTRDY;
	if (sector + count > ram_sectors)
		return RES_PARERR;
	memcpy(buff, ram + sector * 512, count * 512u);
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != 0 || !ram)
		return RES_NOTRDY;
	if (sector + count > ram_sectors)
		return RES_PARERR;
	memcpy(ram + sector * 512, buff, count * 512u);
	if (count > max_write)
		max_write = count;
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv != 0 || !ram)
		return RES_NOTRDY;
	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = (LBA_t)ram_sectors;
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
