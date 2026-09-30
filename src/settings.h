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

// One byte per setting; the payload stores them in this order (append only).
typedef struct {
	uint8_t columns;    // power-on width: 80 or 132
	uint8_t backspace;  // what Backspace sends at power-on/RIS: 0x7f DEL or 0x08 BS
	uint8_t local_echo; // 1: keys typed are also shown on the terminal
	uint8_t baud;       // index: see settings_baud_rate
	uint8_t framing;    // index: see settings_framing
	uint8_t newline;    // LNM at power-on/RIS: 1 on
	uint8_t colours;    // index: see settings_colours
	uint8_t palette;    // 1: mono, SGR colours ignored (term_set_palette)
} settings_t;

// The settings, in settings_t order, with the values each can take (the
// first choice need not be the default) and their names for SET-UP.
#define SETTINGS_FIELDS 8
#define SETTINGS_MAX_CHOICES 9

typedef struct {
	const char *name;
	uint8_t n_choices;
	uint8_t values[SETTINGS_MAX_CHOICES];
	const char *labels[SETTINGS_MAX_CHOICES];
} settings_field_t;

extern const settings_field_t settings_fields[SETTINGS_FIELDS];

// A field's value in s (i < SETTINGS_FIELDS).
static inline uint8_t *settings_field(settings_t *s, unsigned i) {
	return (uint8_t *)s + i;
}

unsigned settings_baud_rate(const settings_t *s);

// Data bits (7 or 8), parity ('N', 'E' or 'O') and stop bits (1 or 2).
void settings_framing(const settings_t *s, unsigned *data_bits, char *parity, unsigned *stop_bits);

// The colours of the "Colors" setting (RGB222). With the mono palette the
// status line is the text colours reversed.
typedef struct {
	uint8_t fg, bold_fg, bg;      // default text
	uint8_t status_fg, status_bg; // the status line and the SET-UP panel
} settings_colours_t;

settings_colours_t settings_colours(const settings_t *s);

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
