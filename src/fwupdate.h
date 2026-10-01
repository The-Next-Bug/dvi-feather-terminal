#ifndef FWUPDATE_H
#define FWUPDATE_H

// Firmware update from a UF2 file on the SD card (the monitor's U), in two
// phases, as the running program's const data (strings, fonts, tables) is
// read from the very flash being replaced:
//
// 1. fwupdate_stage: check the file block by block (uf2.c) while writing
//    the image to the staging area, the top half of flash, then read the
//    staging area back against the CRC. The running image is untouched, so
//    any failure up to here costs nothing.
// 2. fwupdate_commit: with interrupts off, copy the staging area over the
//    image a sector at a time, then restart through the watchdog. Runs from
//    SRAM and reads nothing from the old image: no string literals, const
//    tables or printf (scripts/check_fwupdate.py checks the code it runs).
//    Power lost meanwhile leaves the flash unbootable; BOOTSEL and a USB
//    flash still recover the board.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// From the start of flash: the staging area, and the largest image (which
// must stay below it, and fit above it below the settings sectors).
#define FWUPDATE_STAGING (PICO_FLASH_SIZE_BYTES / 2)

typedef void (*fwupdate_progress_t)(void *ctx, uint32_t done, uint32_t total);

// Read the file through and check it, writing nothing. True if it can be
// installed; info gets a line about it (or why not), without a line end.
bool fwupdate_check(const char *path, char *info, size_t n);

// Check the file again while writing it to the staging area (progress in
// bytes of the file read), then verify that. False: why in info.
bool fwupdate_stage(const char *path, fwupdate_progress_t progress, void *ctx, char *info, size_t n);

// Install the image fwupdate_stage last staged and restart. Does not return
// (unless nothing is staged). Close the files on the card first.
void fwupdate_commit(void);

#endif
