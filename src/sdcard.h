#ifndef SDCARD_H
#define SDCARD_H

// microSD card on SPI0 for FatFs (it implements FatFs's disk_* functions
// for drive 0): SD v1 (SDSC) and v2 (SDSC/SDHC/SDXC) cards in SPI mode,
// polled, 512-byte blocks (written several at a time).
//
// Pins (SPI0): GP4 MISO (card DO), GP5 CS, GP6 SCK, GP7 MOSI (card DI);
// 3V3 and GND. The adapter has no card-detect line: a card is found by
// initialising it (FatFs does on f_mount), and a removed card shows up as
// I/O errors until it is mounted again.

#include <stdbool.h>
#include <stdint.h>

#define SDCARD_MISO_PIN 4
#define SDCARD_CS_PIN 5
#define SDCARD_SCK_PIN 6
#define SDCARD_MOSI_PIN 7

typedef enum {
	SDCARD_NONE,  // not initialised, or no card answered
	SDCARD_SDSC,  // byte addressed, up to 2 GB
	SDCARD_SDHC,  // block addressed (SDHC and SDXC)
} sdcard_type_t;

// What the last initialisation found, and its size in 512-byte blocks.
sdcard_type_t sdcard_type(void);
uint32_t sdcard_blocks(void);

#endif
