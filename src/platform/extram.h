#ifndef EXTRAM_H
#define EXTRAM_H

// The QSPI PSRAM on the QMI's second chip select (PSRAM_BANKS builds:
// boards/feather-rp2350-hstx-psram.h, boards/frankless.h). The SDK's
// hardware_psram finds it and sets it up at boot, at the boot clock;
// extram_retime sets its timing again for the DVI clock. Core 0 only (core
// 1 must not touch XIP), and not while the flash is written (nvflash: XIP
// is off then). Without PSRAM_BANKS, or without a chip, there is none:
// size 0.

#include <stddef.h>
#include <stdint.h>

// After the clock change (video_init), before nvflash_retime: the PSRAM
// setup goes through the SDK's flash_start_xip, which leaves the flash's
// XIP set up for the boot clock. Returns the size found (0: none).
size_t extram_retime(void);

size_t extram_size(void);   // 0: none
uint8_t *extram_base(void); // cached, writable; NULL: none

// The PSRAM handed out from its start, in main()'s order: the Z80 banks
// first, the RAM disk (drives.h) last from what is left, so a new user of
// the PSRAM takes its share between them and the RAM disk shrinks to a
// smaller format if it must. size bytes, or NULL if not that many are left.
uint8_t *extram_take(size_t size);
size_t extram_left(void);

#endif
