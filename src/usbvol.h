#ifndef USBVOL_H
#define USBVOL_H

// Which USB stick is FatFs volume 1 (USB: to XLS and XPIP). Portable C,
// host-tested; usbmsc.c feeds it TinyUSB's attach and detach callbacks and
// does the mounting it asks for from the main loop (never from inside the
// USB stack, which runs in the middle of FatFs calls).
//
// Only one stick is volume 1 at a time: the first that mounts. Others plugged
// in meanwhile wait, in the order they came; when the volume's stick goes,
// the volume is unmounted first (so nothing still open on it reaches another
// stick), then the next waiting stick takes over. A stick that cannot be
// mounted (no FAT file system, sectors other than 512 bytes, a reader with
// no card) is passed over until it is plugged in again.

#include <stdbool.h>
#include <stdint.h>

#define USBVOL_MAX 4 // sticks attached at once (CFG_TUH_DEVICE_MAX)

typedef enum {
	USBVOL_NONE,
	USBVOL_MOUNT,   // mount the stick (*dev), then usbvol_mounted
	USBVOL_UNMOUNT, // unmount the volume, then usbvol_unmounted
} usbvol_action_t;

typedef struct {
	uint8_t dev[USBVOL_MAX]; // attached sticks (USB device addresses), in order
	bool bad[USBVOL_MAX];    // could not be mounted
	unsigned n;
	uint8_t active; // the volume's stick (being mounted or mounted); 0 none
	bool mounted;
	bool gone;      // the active stick was detached: unmount before all else
} usbvol_t;

void usbvol_init(usbvol_t *v);

// TinyUSB's mass storage mount and unmount callbacks.
void usbvol_attach(usbvol_t *v, uint8_t dev);
void usbvol_detach(usbvol_t *v, uint8_t dev);

// What the main loop is to do next.
usbvol_action_t usbvol_next(usbvol_t *v, uint8_t *dev);
void usbvol_mounted(usbvol_t *v, bool ok);
void usbvol_unmounted(usbvol_t *v);

// The stick disk I/O for volume 1 goes to: 0 if none (or it has gone).
uint8_t usbvol_io_dev(const usbvol_t *v);

// Sticks attached besides the volume's: waiting to take over, and passed
// over as unusable.
unsigned usbvol_waiting(const usbvol_t *v);
unsigned usbvol_unusable(const usbvol_t *v);

#endif
