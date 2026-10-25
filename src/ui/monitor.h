#ifndef MONITOR_H
#define MONITOR_H

// The Z80 machine's monitor: the menu shown in CP/M mode before booting and
// when the CPU stops. It mounts the SD card, keeps the drive assignments
// (/DISKS80/DRIVES.CFG), installs the built-in disk images, formats the
// card (sdformat.h), boots and runs the speed test. Portable C (FatFs, machine.h), host-tested; the terminal
// (apps/terminal/cpm_glue.c) feeds it keys and runs the CPU when it says
// so.
//
// Keys are the bytes a VT100 keyboard sends; escape sequences are ignored.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"

#define MONITOR_AUTOBOOT_MS 3000

typedef struct {
	void (*write)(const char *text); // to the console (\r\n line ends)
	// The card found by the last mount, e.g. "SDHC, 7.4 GB"; NULL: no card.
	const char *(*card_info)(void);
	// The card's pins, shown when none is found (NULL: not shown).
	const char *card_pins;
	// Show the console's text so far (NULL: nothing to do). Called during
	// long operations, which hold up the caller's main loop.
	void (*flush)(void);
	// What the USB host sees, shown under the card (NULL: no USB host).
	const char *(*usb_info)(void);
	// A long operation is still making progress (NULL: nothing to do).
	// Called after every chunk written, so the caller's hang check does
	// not take a big disk image for a hang (bd fhu.3).
	void (*busy)(void);
} monitor_platform_t;

typedef enum {
	MONITOR_NONE,
	MONITOR_RUN, // run the CPU (booted or resumed) until it stops
} monitor_action_t;

// Mount the card, read the drive assignments and show the menu. With
// autoboot, boot from drive A after MONITOR_AUTOBOOT_MS unless a key is
// pressed first (now_ms: a millisecond clock).
void monitor_start(const monitor_platform_t *platform, bool autoboot, uint32_t now_ms);

// The CPU stopped (reason from machine_stop_reason) or was interrupted
// (reason NULL, from SET-UP): show the menu, with resume offered if the CPU
// can carry on.
void monitor_enter(const char *reason, bool can_resume);

monitor_action_t monitor_key(uint8_t c, uint32_t now_ms);

// The SD card's file system object (FatFs volume 0), mounted by the
// monitor. Terminal mode has no monitor, so SET-UP's firmware panel mounts
// the card with it while open: one FATFS (~560 bytes of SRAM) for both.
FATFS *monitor_card_fs(void);
monitor_action_t monitor_tick(uint32_t now_ms);

#endif
