#ifndef USBMSC_H
#define USBMSC_H

// A USB stick as FatFs volume 1 ("1:", USB: to XLS and XPIP): TinyUSB's
// mass storage host on the port the keyboard uses (through a hub for both),
// LUN 0 only, 512-byte sectors only. usbvol.c picks the stick when several
// are plugged in. Built with USB_KEYBOARD (the USB host).
//
// Disk I/O waits for each transfer by running the USB stack (tuh_task), so
// it happens in the middle of FatFs calls: TinyUSB's callbacks only note
// what changed, and usbmsc_poll mounts and unmounts from the main loop.

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "diskio.h"

// Mount the stick usbvol picks, unmount one that went. Call from the main
// loop, outside FatFs calls.
void usbmsc_poll(void);

typedef struct {
	bool mounted;      // volume 1 is a stick
	unsigned waiting;  // other sticks in, to take over when it goes
	unsigned unusable; // sticks in that could not be mounted
} usbmsc_status_t;

usbmsc_status_t usbmsc_status(void);

// Whether volume 1 was read or written since the last call (its light).
bool usbmsc_activity(void);

// What the USB host sees, for the monitor: every device by address with
// its VID:PID and what took it, then the stick's state (or why it is not
// in use). Lines end in \r\n.
const char *usbmsc_info(void);

// FatFs's disk_* for drive 1.
DSTATUS usbmsc_disk_status(void);
DSTATUS usbmsc_disk_initialize(void);
DRESULT usbmsc_disk_read(BYTE *buff, LBA_t sector, UINT count);
DRESULT usbmsc_disk_write(const BYTE *buff, LBA_t sector, UINT count);
DRESULT usbmsc_disk_ioctl(BYTE cmd, void *buff);

#endif
