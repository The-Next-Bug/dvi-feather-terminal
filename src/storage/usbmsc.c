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
// Sectors per SCSI command (32 KB; TinyUSB splits a data stage over
// 0xFC00 bytes itself). Build with -DUSBMSC_MAX_SECTORS=1 to compare.
#ifndef USBMSC_MAX_SECTORS
#define USBMSC_MAX_SECTORS 64
#endif

static usbvol_t vol;
static FATFS fs;
static bool activity;
// For usbmsc_info: the last mount's FatFs result, or the sector size that
// ruled it out.
static FRESULT last_mount = FR_OK;
static uint32_t last_sector;

// The transfer in flight: done by io_done (from tuh_task), for io_dev.
static volatile bool io_busy, io_ok, io_bad_csw;
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
	if (usbkbd_resets() && n < sizeof text) {
		n += (size_t)snprintf(text + n, sizeof text - n, "\r\n          host restarted %u times (looked stuck)",
			usbkbd_resets());
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

// A CSW that is not this command's, or a short transfer, is a failure:
// never data the stick did not send.
static bool io_done(uint8_t dev_addr, tuh_msc_complete_data_t const *data) {
	if (dev_addr == io_dev) { // not a late one for a transfer given up on
		const msc_csw_t *csw = data->csw;
		io_bad_csw = csw->signature != MSC_CSW_SIGNATURE || csw->tag != data->cbw->tag;
		io_ok = !io_bad_csw && csw->data_residue == 0 && csw->status == MSC_CSW_STATUS_PASSED;
		io_busy = false;
	}
	return true;
}

// The stick's commands are stuck after a lost or garbled one (TinyUSB has
// no Bulk-Only reset): the USB host is restarted, from the main loop.
static DRESULT io_failed(const char *why, uint8_t dev) {
	usbkbd_log(why, dev);
	usbkbd_request_reset("mscrst");
	return RES_ERROR;
}

// Run the USB stack until the transfer is done, the stick goes or it takes
// too long.
static DRESULT io_wait(uint8_t dev) {
	uint64_t until = time_us_64() + IO_TIMEOUT_US;
	while (io_busy) {
		tuh_task();
		if (usbvol_io_dev(&vol) != dev) {
			usbkbd_log("gone", dev);
			io_dev = 0;
			io_busy = false;
			return RES_ERROR;
		}
		if (time_us_64() > until) {
			io_dev = 0;
			io_busy = false;
			return io_failed("timeout", dev);
		}
	}
	if (io_bad_csw) {
		return io_failed("badcsw", dev);
	}
	if (!io_ok) {
		usbkbd_log("ioerr", dev); // the stick says so: it still answers
		return RES_ERROR;
	}
	return RES_OK;
}

DSTATUS usbmsc_disk_status(void) {
	return usbvol_io_dev(&vol) ? 0 : STA_NOINIT;
}

DSTATUS usbmsc_disk_initialize(void) {
	return usbmsc_disk_status();
}

// count sectors at sector, USBMSC_MAX_SECTORS per command.
static DRESULT io_run(bool write, BYTE *buff, LBA_t sector, UINT count);

// The commands run with the controller's own polling paused
// (usbkbd_polls_pause).
static DRESULT io(bool write, BYTE *buff, LBA_t sector, UINT count) {
	uint32_t polls = usbkbd_polls_pause();
	DRESULT r = io_run(write, buff, sector, count);
	usbkbd_polls_resume(polls);
	return r;
}

static DRESULT io_run(bool write, BYTE *buff, LBA_t sector, UINT count) {
	uint8_t dev = usbvol_io_dev(&vol);
	if (!dev || usbkbd_reset_pending()) { // stuck until the restart: fail at once
		return RES_NOTRDY;
	}
	activity = true;
	while (count) {
		uint16_t n = (uint16_t)(count < USBMSC_MAX_SECTORS ? count : USBMSC_MAX_SECTORS);
		io_dev = dev;
		io_busy = true;
		bool sent = write ? tuh_msc_write10(dev, LUN, buff, (uint32_t)sector, n, io_done, 0)
			: tuh_msc_read10(dev, LUN, buff, (uint32_t)sector, n, io_done, 0);
		if (!sent) {
			io_busy = false;
			return io_failed("busy", dev);
		}
		DRESULT r = io_wait(dev);
		if (r != RES_OK) {
			return r;
		}
		buff += n * SECTOR;
		sector += n;
		count -= n;
	}
	return RES_OK;
}

DRESULT usbmsc_disk_read(BYTE *buff, LBA_t sector, UINT count) {
	return io(false, buff, sector, count);
}

DRESULT usbmsc_disk_write(const BYTE *buff, LBA_t sector, UINT count) {
	return io(true, (BYTE *)buff, sector, count);
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
