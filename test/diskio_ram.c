// FatFs disk I/O for the host tests: volumes in RAM buffers
// (diskio_ram.h), standing in for the SD card (drive 0) and a USB stick
// (drive 1).

#include "diskio_ram.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"

#define DRIVES 2

static struct {
	uint8_t *ram;
	size_t sectors;
} drive[DRIVES];
static unsigned max_write;

void diskio_ram_init_drive(unsigned pdrv, size_t sectors) {
	if (pdrv >= DRIVES)
		return;
	free(drive[pdrv].ram);
	drive[pdrv].ram = calloc(sectors, 512);
	drive[pdrv].sectors = drive[pdrv].ram ? sectors : 0;
}

void diskio_ram_free_drive(unsigned pdrv) {
	if (pdrv >= DRIVES)
		return;
	free(drive[pdrv].ram);
	drive[pdrv].ram = NULL;
	drive[pdrv].sectors = 0;
}

void diskio_ram_init(size_t sectors) {
	diskio_ram_init_drive(0, sectors);
}

void diskio_ram_free(void) {
	diskio_ram_free_drive(0);
	diskio_ram_free_drive(1);
}

unsigned diskio_ram_max_write(void) {
	unsigned n = max_write;
	max_write = 0;
	return n;
}

static bool present(BYTE pdrv) {
	return pdrv < DRIVES && drive[pdrv].ram;
}

DSTATUS disk_status(BYTE pdrv) {
	return present(pdrv) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (!present(pdrv))
		return RES_NOTRDY;
	if (sector + count > drive[pdrv].sectors)
		return RES_PARERR;
	memcpy(buff, drive[pdrv].ram + sector * 512, count * 512u);
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (!present(pdrv))
		return RES_NOTRDY;
	if (sector + count > drive[pdrv].sectors)
		return RES_PARERR;
	memcpy(drive[pdrv].ram + sector * 512, buff, count * 512u);
	if (count > max_write)
		max_write = count;
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (!present(pdrv))
		return RES_NOTRDY;
	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = (LBA_t)drive[pdrv].sectors;
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
