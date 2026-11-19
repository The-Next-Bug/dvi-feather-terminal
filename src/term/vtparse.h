#ifndef VTPARSE_H
#define VTPARSE_H

// DEC-compatible escape sequence parser, after Paul Williams' state machine
// (https://vt100.net/emu/dec_ansi_parser). Portable C, host-tested.
//
// It splits a byte stream into printable characters, C0/C1 controls, escape
// sequences and control sequences, and hands each to a callback. Device
// control strings, OSC strings and SOS/PM/APC strings are recognized and
// discarded, so their contents never reach the screen. A string that is
// never terminated must not swallow everything after it (an Alt key sends a
// bare ESC prefix, so Alt+] opens an OSC string): as xterm's
// brokenStringTerm, a C0 control other than NUL (and BEL, OSC's terminator)
// ends a string and is then executed, and a string longer than
// VTPARSE_MAX_STRING bytes ends there, its remaining bytes parsed as usual.
//
// Bytes 0xA0-0xFF are printed (Latin-1). Bytes 0x80-0x9F are ignored, as on
// a (7-bit) VT100, unless c1_controls is set: then they are C1 controls
// (0x9B = CSI and so on), as on a VT220 in 8-bit mode. Leaving them off keeps
// UTF-8 continuation bytes from acting as controls.
//
// With utf8 set (bd cvc), bytes from 0x80 in ground state are UTF-8: each
// character comes as one VTPARSE_PRINT_CODE with its code point in code,
// a malformed sequence (a stray continuation byte, a sequence cut short by
// any other byte, an overlong form, a surrogate, past U+10FFFF) as U+FFFD.
// A byte that cuts a sequence short is then parsed as usual. Outside ground
// state (inside an escape sequence or a string) bytes act as without it.
// c1_controls must stay off with it.

#include <stdbool.h>
#include <stdint.h>

#define VTPARSE_MAX_PARAMS 16
#define VTPARSE_MAX_INTERMEDIATES 2
#define VTPARSE_MAX_STRING 1024 // GNU screen stops at 768

typedef enum {
	VTPARSE_PRINT,       // ch: a printable character
	VTPARSE_EXECUTE,     // ch: a C0 or C1 control
	VTPARSE_ESC_DISPATCH, // ch: final byte; intermediates
	VTPARSE_CSI_DISPATCH, // ch: final byte; private marker, intermediates, params
	VTPARSE_PRINT_CODE,   // utf8: a character from 0x80, its code point in code (ch 0)
} vtparse_action_t;

#define VTPARSE_REPLACEMENT 0xFFFD

typedef struct vtparse vtparse_t;

typedef void (*vtparse_callback_t)(vtparse_t *p, vtparse_action_t action, uint8_t ch);

struct vtparse {
	vtparse_callback_t callback;
	void *user;
	bool c1_controls; // act on 8-bit C1 controls (default false: ignore them)
	bool utf8;        // bytes from 0x80 in ground state are UTF-8 (default false: Latin-1)

	// Valid during a VTPARSE_PRINT_CODE callback:
	uint32_t code;

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
	uint16_t string_len; // bytes discarded in the current string
	uint8_t utf8_need;   // continuation bytes still to come
	uint32_t utf8_min;   // the least code point the sequence's length may encode
	uint32_t utf8_code;  // the bits so far
};

void vtparse_init(vtparse_t *p, vtparse_callback_t callback, void *user);
void vtparse_byte(vtparse_t *p, uint8_t ch);

// Abandon any sequence or string in progress and go back to ground state.
// The callback, user pointer, c1_controls and utf8 are kept (a UTF-8
// sequence in progress is dropped). Safe to call from the callback.
void vtparse_reset(vtparse_t *p);

// Parameter i, or def if it is missing or 0 (the usual VT100 convention).
static inline unsigned vtparse_param(const vtparse_t *p, unsigned i, unsigned def) {
	return i < p->n_params && p->params[i] ? p->params[i] : def;
}

#endif
