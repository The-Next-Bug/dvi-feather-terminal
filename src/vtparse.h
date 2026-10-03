#ifndef VTPARSE_H
#define VTPARSE_H

// DEC-compatible escape sequence parser, after Paul Williams' state machine
// (https://vt100.net/emu/dec_ansi_parser). Portable C, host-tested.
//
// It splits a byte stream into printable characters, C0/C1 controls, escape
// sequences and control sequences, and hands each to a callback. Device
// control strings, OSC strings and SOS/PM/APC strings are recognised and
// discarded, so their contents never reach the screen.
//
// Bytes 0xA0-0xFF are printed (Latin-1). Bytes 0x80-0x9F are ignored, as on
// a (7-bit) VT100, unless c1_controls is set: then they are C1 controls
// (0x9B = CSI and so on), as on a VT220 in 8-bit mode. Leaving them off keeps
// UTF-8 continuation bytes from acting as controls.

#include <stdbool.h>
#include <stdint.h>

#define VTPARSE_MAX_PARAMS 16
#define VTPARSE_MAX_INTERMEDIATES 2

typedef enum {
	VTPARSE_PRINT,       // ch: a printable character
	VTPARSE_EXECUTE,     // ch: a C0 or C1 control
	VTPARSE_ESC_DISPATCH, // ch: final byte; intermediates
	VTPARSE_CSI_DISPATCH, // ch: final byte; private marker, intermediates, params
} vtparse_action_t;

typedef struct vtparse vtparse_t;

typedef void (*vtparse_callback_t)(vtparse_t *p, vtparse_action_t action, uint8_t ch);

struct vtparse {
	vtparse_callback_t callback;
	void *user;
	bool c1_controls; // act on 8-bit C1 controls (default false: ignore them)

	// Valid during a dispatch callback:
	uint8_t private_marker;                          // '<' '=' '>' '?' or 0
	uint8_t intermediates[VTPARSE_MAX_INTERMEDIATES];
	uint8_t n_intermediates;
	uint16_t params[VTPARSE_MAX_PARAMS];             // missing parameters are 0
	uint8_t n_params;

	// Internal.
	uint8_t state;
	bool ignoring;    // too many intermediates: dispatch nothing
	bool params_full; // more than VTPARSE_MAX_PARAMS: drop the extras
};

void vtparse_init(vtparse_t *p, vtparse_callback_t callback, void *user);
void vtparse_byte(vtparse_t *p, uint8_t ch);

// Abandon any sequence or string in progress and go back to ground state.
// The callback, user pointer and c1_controls are kept. Safe to call from the
// callback.
void vtparse_reset(vtparse_t *p);

// Parameter i, or def if it is missing or 0 (the usual VT100 convention).
static inline unsigned vtparse_param(const vtparse_t *p, unsigned i, unsigned def) {
	return i < p->n_params && p->params[i] ? p->params[i] : def;
}

#endif
