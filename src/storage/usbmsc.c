#include "usbmsc.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "usbkbd.h"
#include "usbvol.h"
#include "xfer.h"

#define VOLUME "1:"
#define SECTOR 512
#define LUN 0
// A stick answers in milliseconds; one that does not in this long is
// treated as gone for this transfer.
#define IO_TIMEOUT_US 3000000

static usbvol_t vol;
static FATFS fs;
static bool activity;
// For usbmsc_info: the last mount's FatFs result, or the sector size that
// ruled it out.
static FRESULT last_mount = FR_OK;
static uint32_t last_sector;

// The transfer in flight: done by io_done (from tuh_task), for io_dev.
static volatile bool io_busy, io_ok;
static uint8_t io_dev;

void tuh_msc_mount_cb(uint8_t dev_addr) {
	usbkbd_log("msc", dev_addr);
	usbvol_attach(&vol, dev_addr);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
	usbkbd_log("msc-", dev_addr);
	usbvol_detach(&vol, dev_addr);
}

void usbmsc_poll(void) {
	uint8_t dev;
	switch (usbvol_next(&vol, &dev)) {
	case USBVOL_UNMOUNT:
		// Nothing still open on it may reach the next stick.
		usbkbd_log("unvol", vol.active);
		xfer_volume_gone(1);
		f_unmount(VOLUME);
		usbvol_unmounted(&vol);
		break;
	case USBVOL_MOUNT: {
		last_sector = tuh_msc_get_block_size(dev, LUN);
		last_mount = last_sector == SECTOR ? f_mount(&fs, VOLUME, 1) : FR_INVALID_PARAMETER;
		bool ok = last_mount == FR_OK;
		usbkbd_log(ok ? "vol" : "vol?", dev);
		if (!ok) {
			f_unmount(VOLUME);
		}
		usbvol_mounted(&vol, ok);
		break;
	}
	default:
		break;
	}
}

usbmsc_status_t usbmsc_status(void) {
	return (usbmsc_status_t){
		.mounted = vol.mounted && usbvol_io_dev(&vol),
		.waiting = usbvol_waiting(&vol),
		.unusable = usbvol_unusable(&vol),
	};
}

bool usbmsc_activity(void) {
	bool a = activity;
	activity = false;
	return a;
}

const char *usbmsc_info(void) {
	static char text[256];
	size_t n = (size_t)snprintf(text, sizeof text, "USB:     ");
	unsigned devices = 0;
	for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB && n < sizeof text; ++a) {
		uint16_t vid, pid;
		if (!tuh_vid_pid_get(a, &vid, &pid)) {
			continue;
		}
		const char *what = a > CFG_TUH_DEVICE_MAX ? (usbkbd_device_ready(a) ? "hub" : "hub setting up")
			: tuh_msc_mounted(a) ? "stick" : !usbkbd_device_ready(a) ? "setting up" : "ready";
		n += (size_t)snprintf(text + n, sizeof text - n, "%s %u %04x:%04x %s", devices ? "," : "", a, vid, pid,
			what);
		// HID interfaces set up of those found: one stuck stops the device.
		uint8_t hids = a <= CFG_TUH_DEVICE_MAX ? tuh_hid_instance_count(a) : 0, done = 0;
		for (uint8_t i = 0; i < hids; ++i) {
			done += tuh_hid_mounted(a, i);
		}
		if (hids && n < sizeof text) {
			n += (size_t)snprintf(text + n, sizeof text - n, " HID %u/%u", done, hids);
		}
		++devices;
	}
	if (!devices && n < sizeof text) {
		n += (size_t)snprintf(text + n, sizeof text - n, " no devices");
	}
	if (n < sizeof text) {
		usbmsc_status_t st = usbmsc_status();
		if (st.mounted) {
			n += (size_t)snprintf(text + n, sizeof text - n, "\r\n          USB: is the stick at %u, FAT%s, %lu MB",
				vol.active, fs.fs_type == FS_FAT12 ? "12" : fs.fs_type == FS_FAT16 ? "16" : "32",
				(unsigned long)(tuh_msc_get_block_count(vol.active, LUN) / 2048));
		} else if (last_mount != FR_OK && last_sector != SECTOR) {
			n += (size_t)snprintf(text + n, sizeof text - n,
				"\r\n          last stick: %lu-byte sectors (512 needed)", (unsigned long)last_sector);
		} else if (last_mount != FR_OK) {
			n += (size_t)snprintf(text + n, sizeof text - n,
				"\r\n          last stick: no FAT file system (FatFs error %d; exFAT?)", (int)last_mount);
		}
	}
	if (n < sizeof text) {
		snprintf(text + n, sizeof text - n, "\r\n");
	}
	return text;
}

// ---- FatFs disk I/O for drive 1 (diskio.c) ----

static bool io_done(uint8_t dev_addr, tuh_msc_complete_data_t const *data) {
	if (dev_addr == io_dev) { // not a late one for a transfer given up on
		io_ok = data->csw->status == MSC_CSW_STATUS_PASSED;
		io_busy = false;
	}
	return true;
}

// Run the USB stack until the transfer is done, the stick goes or it takes
// too long.
static DRESULT io_wait(uint8_t dev) {
	uint64_t until = time_us_64() + IO_TIMEOUT_US;
	while (io_busy) {
		tuh_task();
		if (usbvol_io_dev(&vol) != dev || time_us_64() > until) {
			usbkbd_log(usbvol_io_dev(&vol) != dev ? "gone" : "timeout", dev);
			io_dev = 0;
			io_busy = false;
			return RES_ERROR;
		}
	}
	if (!io_ok) {
		usbkbd_log("ioerr", dev);
	}
	return io_ok ? RES_OK : RES_ERROR;
}

DSTATUS usbmsc_disk_status(void) {
	return usbvol_io_dev(&vol) ? 0 : STA_NOINIT;
}

DSTATUS usbmsc_disk_initialize(void) {
	return usbmsc_disk_status();
}

DRESULT usbmsc_disk_read(BYTE *buff, LBA_t sector, UINT count) {
	uint8_t dev = usbvol_io_dev(&vol);
	if (!dev) {
		return RES_NOTRDY;
	}
	activity = true;
	io_dev = dev;
	io_busy = true;
	if (!tuh_msc_read10(dev, LUN, buff, (uint32_t)sector, (uint16_t)count, io_done, 0)) {
		usbkbd_log("busy", dev);
		io_busy = false;
		return RES_ERROR;
	}
	return io_wait(dev);
}

DRESULT usbmsc_disk_write(const BYTE *buff, LBA_t sector, UINT count) {
	uint8_t dev = usbvol_io_dev(&vol);
	if (!dev) {
		return RES_NOTRDY;
	}
	activity = true;
	io_dev = dev;
	io_busy = true;
	if (!tuh_msc_write10(dev, LUN, buff, (uint32_t)sector, (uint16_t)count, io_done, 0)) {
		usbkbd_log("busy", dev);
		io_busy = false;
		return RES_ERROR;
	}
	return io_wait(dev);
}

DRESULT usbmsc_disk_ioctl(BYTE cmd, void *buff) {
	uint8_t dev = usbvol_io_dev(&vol);
	if (!dev) {
		return RES_NOTRDY;
	}
	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK; // each write completes before disk_write returns
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = tuh_msc_get_block_count(dev, LUN);
		return RES_OK;
	case GET_SECTOR_SIZE:
		*(WORD *)buff = SECTOR;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1; // unknown
		return RES_OK;
	default:
		return RES_PARERR;
	}
}
