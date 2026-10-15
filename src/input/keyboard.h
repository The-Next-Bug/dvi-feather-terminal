#ifndef KEYBOARD_H
#define KEYBOARD_H

// USB HID keyboard (boot protocol reports) to VT100 key codes. Portable C
// (no pico-sdk, no TinyUSB), host-tested; usbkbd.c feeds it from TinyUSB.
//
// US layout. Letters honor Caps Lock, Ctrl makes control characters, Alt
// prefixes ESC (meta). Cursor keys follow DECCKM (ESC [ A or ESC O A), the
// keypad follows DECKPAM/DECKPNM while Num Lock is on (it is navigation
// otherwise, as on a PC), F1-F4 are PF1-PF4 (ESC O P..S), and Return sends
// CR, or CR LF in LNM. Backspace sends DEL, as Unix hosts expect, or BS in
// DECBKM (Ctrl-Backspace sends the other one). The editing
// keys and F5-F11 send the VT220/xterm sequences (ESC [ n ~).
//
// F12 (the VT100 has none) and Pause (for full-size keyboards) are the
// SET-UP key: they send nothing and call the setup callback
// (kbd_set_setup_key) instead. With a reset callback set
// (kbd_set_reset_key, the Z80 machine's reset), F11 calls it instead of
// sending its sequence.
//
// Boot keyboards do not repeat keys themselves: the last key pressed repeats
// after KBD_REPEAT_DELAY_MS, every KBD_REPEAT_INTERVAL_MS, while held.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KBD_REPORT_KEYS 6
#define KBD_REPEAT_DELAY_MS 500
#define KBD_REPEAT_INTERVAL_MS 33 // about 30 characters a second

// Output report bits (boot protocol LEDs).
#define KBD_LED_NUM_LOCK 0x01
#define KBD_LED_CAPS_LOCK 0x02

typedef void (*kbd_output_t)(void *ctx, const uint8_t *data, size_t len);
typedef void (*kbd_setup_t)(void *ctx);

// The terminal modes that change what keys send.
typedef struct {
	bool cursor_keys_app; // DECCKM
	bool keypad_app;      // DECKPAM
	bool newline_mode;    // LNM
	bool backspace_bs;    // DECBKM
	bool vt52;            // VT52 mode: cursor keys ESC x, PF keys ESC P-S, keypad ESC ? x;
	                      // Home, End, Insert, Delete, Page Up/Down, F5-F11 send nothing
	bool adm3a;           // ADM-3A: cursor keys ^K ^J ^H ^L, Home ^^, no function keys
} kbd_modes_t;

typedef struct {
	uint8_t modifiers;                // from the last report
	uint8_t keys[KBD_REPORT_KEYS];    // from the last report
	bool caps_lock, num_lock;
	uint8_t repeat_key;               // 0: none
	uint32_t repeat_at;               // ms, when it next repeats

	kbd_output_t output;
	kbd_setup_t setup; // the SET-UP key; NULL: none
	kbd_setup_t reset; // F11 as the reset key; NULL: F11 sends ESC [ 2 3 ~
	void *output_ctx;  // for all three
} kbd_t;

// Num Lock starts on, so the keypad sends digits (or DECKPAM codes).
void kbd_init(kbd_t *k, kbd_output_t output, void *ctx);

// Call setup (with the output ctx) when the SET-UP key is pressed.
void kbd_set_setup_key(kbd_t *k, kbd_setup_t setup);

// Make F11 call reset (with the output ctx) instead of sending a sequence;
// NULL: F11 is a function key again.
void kbd_set_reset_key(kbd_t *k, kbd_setup_t reset);

// Handle an 8-byte boot protocol report (modifiers, reserved, 6 key codes)
// received at now_ms. Returns true if the lock LEDs should change (see
// kbd_leds).
bool kbd_report(kbd_t *k, const uint8_t report[8], const kbd_modes_t *modes, uint32_t now_ms);

// Send auto-repeats that are due. Call regularly.
void kbd_tick(kbd_t *k, const kbd_modes_t *modes, uint32_t now_ms);

// All keys up (the keyboard was unplugged): stops any repeat.
void kbd_release_all(kbd_t *k);

// The LED output report for the current lock state.
uint8_t kbd_leds(const kbd_t *k);

#endif
