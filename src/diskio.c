// FatFs disk I/O on the board: drive 0 the SD card (sdcard.c), drive 1 a
// USB stick (usbmsc.c, with the USB host). The host tests use
// test/diskio_ram.c instead.

#include "ff.h"
#include "diskio.h"

#include "sdcard.h"
#if USB_KEYBOARD
#include "usbmsc.h"
#endif

DSTATUS disk_status(BYTE pdrv) {
	if (pdrv == 0)
		return sdcard_disk_status();
#if USB_KEYBOARD
	if (pdrv == 1)
		return usbmsc_disk_status();
#endif
	return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	if (pdrv == 0)
		return sdcard_disk_initialize();
#if USB_KEYBOARD
	if (pdrv == 1)
		return usbmsc_disk_initialize();
#endif
	return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv == 0)
		return sdcard_disk_read(buff, sector, count);
#if USB_KEYBOARD
	if (pdrv == 1)
		return usbmsc_disk_read(buff, sector, count);
#endif
	return RES_NOTRDY;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv == 0)
		return sdcard_disk_write(buff, sector, count);
#if USB_KEYBOARD
	if (pdrv == 1)
		return usbmsc_disk_write(buff, sector, count);
#endif
	return RES_NOTRDY;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv == 0)
		return sdcard_disk_ioctl(cmd, buff);
#if USB_KEYBOARD
	if (pdrv == 1)
		return usbmsc_disk_ioctl(cmd, buff);
#endif
	return RES_NOTRDY;
}
