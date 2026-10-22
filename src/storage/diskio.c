// FatFs disk I/O on the board: drive 0 the SD card (sdcard.c), drive 1 a
// USB stick (usbmsc.c, with the USB host), and the files' dates from the
// wall clock (rtc.c). The host tests use test/diskio_ram.c instead. Reads
// and writes blink the LED.

#include "ff.h"
#include "diskio.h"

#include "datetime.h"
#include "led.h"
#include "rtc.h"
#include "sdcard.h"
#include "usbmsc.h"

DSTATUS disk_status(BYTE pdrv) {
	if (pdrv == 0) {
		return sdcard_disk_status();
	}
	if (pdrv == 1) {
		return usbmsc_disk_status();
	}
	return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
	if (pdrv == 0) {
		return sdcard_disk_initialize();
	}
	if (pdrv == 1) {
		return usbmsc_disk_initialize();
	}
	return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	led_activity();
	if (pdrv == 0) {
		return sdcard_disk_read(buff, sector, count);
	}
	if (pdrv == 1) {
		return usbmsc_disk_read(buff, sector, count);
	}
	return RES_NOTRDY;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	led_activity();
	if (pdrv == 0) {
		return sdcard_disk_write(buff, sector, count);
	}
	if (pdrv == 1) {
		return usbmsc_disk_write(buff, sector, count);
	}
	return RES_NOTRDY;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv == 0) {
		return sdcard_disk_ioctl(cmd, buff);
	}
	if (pdrv == 1) {
		return usbmsc_disk_ioctl(cmd, buff);
	}
	return RES_NOTRDY;
}

DWORD get_fattime(void) {
	return datetime_to_fat(rtc_now());
}
