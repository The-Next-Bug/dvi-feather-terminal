#ifndef XFER_H
#define XFER_H

// The file transfer device's files (port 10, machine.h): the folder /XFER
// of a FatFs volume (0 the SD card, 1 a USB stick), for CP/M's XLS and XPIP.
// Portable C over FatFs, host-tested.
//
// Names cross in CP/M's FCB form and are 8.3 on the FAT side (no long file
// names in ffconf: a PC's long names show as their short forms). Only names
// CP/M can take are listed: letters, digits and !#$%&'()-@^`{}~. One file
// is open at a time (opening or creating another closes it), and one
// listing, which keeps its place while a file is open.

#include <stdint.h>

// The machine's xfer hook: run cmd (XFER_*) on volume with buf (at least
// XFER_RECORD bytes) as its input and output. Returns XFER_ST_*.
uint8_t xfer_command(uint8_t cmd, uint8_t volume, uint8_t *buf);

// Close the open file and listing: before the volume is unmounted (and on a
// machine reset, through XFER_RESET).
void xfer_reset(void);

// Drop the file and listing open on volume (the USB stick went): before it
// is unmounted. What is open on the other volume stays.
void xfer_volume_gone(uint8_t volume);

#endif
