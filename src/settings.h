#ifndef SETTINGS_H
#define SETTINGS_H

// Settings kept in flash across power cycles (SET-UP). Portable C (no
// pico-sdk), host-tested; nvflash.c does the flash access.
//
// Two flash sectors are used in turn: each save erases the sector that does
// not hold the newest valid record and programs one record (one flash page)
// at its start. Power lost during a save leaves the other sector's record,
// so the previous settings stand.
//
// Record (little-endian): magic "DVTS", version, payload length, 2 reserved,
// sequence number (u32), CRC-32 of bytes 0-11 and the payload, payload. The
// payload is one byte per setting in the order of settings_t; settings are
// only appended, so an older record just lacks the newer ones (they keep
// their defaults). The version changes only if the layout ever has to.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_RECORD_SIZE 256 // one flash page
#define SETTINGS_SLOTS 2         // flash sectors used in turn

// One byte per setting; the payload stores them in this order.
typedef struct {
	uint8_t columns;    // power-on width: 80 or 132
	uint8_t backspace;  // what Backspace sends at power-on/RIS: 0x7f DEL or 0x08 BS
	uint8_t local_echo; // 1: keys typed are also shown on the terminal
} settings_t;

// Where the next save goes and with which sequence number.
typedef struct {
	unsigned slot;
	uint32_t seq;
} settings_target_t;

void settings_defaults(settings_t *s);

// Encode s as record number seq into out (the rest filled with 0xff, as
// erased flash).
void settings_encode(const settings_t *s, uint32_t seq, uint8_t out[SETTINGS_RECORD_SIZE]);

// Decode a record: false if it is not a valid record (erased, torn or
// foreign data). Settings missing from the record, or with values out of
// range, are left at their defaults.
bool settings_decode(const uint8_t rec[SETTINGS_RECORD_SIZE], settings_t *s, uint32_t *seq);

// Load the newest valid record of slots[0..SETTINGS_SLOTS-1] into s
// (defaults if there is none) and say where the next save goes. Returns
// whether a record was found.
bool settings_load(const uint8_t *const slots[SETTINGS_SLOTS], settings_t *s, settings_target_t *next);

#endif
