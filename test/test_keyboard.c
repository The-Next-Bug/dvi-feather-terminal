#include <string.h>

#include "keyboard.h"
#include "test.h"

#define LCTRL 0x01
#define LSHIFT 0x02
#define LALT 0x04
#define RSHIFT 0x20

static kbd_t k;
static kbd_modes_t modes;
static char out[256];
static uint32_t now;

static void on_output(void *ctx, const uint8_t *data, size_t len) {
	(void)ctx;
	size_t room = sizeof out - strlen(out) - 1;
	strncat(out, (const char *)data, len < room ? len : room);
}

static void setup(void) {
	kbd_init(&k, on_output, NULL);
	memset(&modes, 0, sizeof modes);
	out[0] = 0;
	now = 1000;
}

// Send a report with up to two keys down.
static bool report(uint8_t modifiers, uint8_t key1, uint8_t key2) {
	uint8_t r[8] = {modifiers, 0, key1, key2, 0, 0, 0, 0};
	return kbd_report(&k, r, &modes, now);
}

// Press and release one key; return what it sent.
static const char *tap(uint8_t modifiers, uint8_t key) {
	out[0] = 0;
	report(modifiers, key, 0);
	report(modifiers, 0, 0);
	return out;
}

static void test_letters_and_shift(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x04), "a"));
	CHECK(!strcmp(tap(LSHIFT, 0x04), "A"));
	CHECK(!strcmp(tap(RSHIFT, 0x1d), "Z"));
	CHECK(!strcmp(tap(0, 0x1e), "1"));
	CHECK(!strcmp(tap(LSHIFT, 0x1f), "@"));
	CHECK(!strcmp(tap(0, 0x2c), " "));
	CHECK(!strcmp(tap(LSHIFT, 0x38), "?"));
	CHECK(!strcmp(tap(LSHIFT, 0x34), "\""));
	CHECK(!strcmp(tap(0, 0x31), "\\"));
	CHECK(!strcmp(tap(LSHIFT, 0x35), "~"));
}

static void test_caps_lock(void) {
	setup();
	out[0] = 0;
	CHECK(report(0, 0x39, 0)); // LEDs change
	CHECK(!report(0, 0, 0));
	CHECK_EQ(kbd_leds(&k), KBD_LED_NUM_LOCK | KBD_LED_CAPS_LOCK);
	CHECK(!strcmp(out, ""));
	CHECK(!strcmp(tap(0, 0x04), "A"));
	CHECK(!strcmp(tap(LSHIFT, 0x04), "a"));
	CHECK(!strcmp(tap(0, 0x1e), "1")); // letters only
	report(0, 0x39, 0);
	report(0, 0, 0);
	CHECK_EQ(kbd_leds(&k), KBD_LED_NUM_LOCK);
	CHECK(!strcmp(tap(0, 0x04), "a"));
}

static void test_control_and_alt(void) {
	setup();
	CHECK(!strcmp(tap(LCTRL, 0x06), "\x03"));       // ^C
	CHECK(!strcmp(tap(LCTRL | LSHIFT, 0x06), "\x03"));
	CHECK(!strcmp(tap(LCTRL, 0x2f), "\x1b"));       // ^[
	CHECK_EQ(tap(LCTRL, 0x2c)[0], 0);              // ^space: NUL (not visible to strcmp)
	CHECK(!strcmp(tap(LCTRL, 0x23), "\x1e"));       // ^6
	CHECK(!strcmp(tap(LCTRL, 0x2d), "\x1f"));       // ^-
	CHECK(!strcmp(tap(LALT, 0x04), "\x1b" "a"));
	CHECK(!strcmp(tap(LALT | LCTRL, 0x04), "\x1b\x01"));
}

static void test_basic_controls(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x28), "\r"));
	CHECK(!strcmp(tap(0, 0x29), "\x1b"));
	CHECK(!strcmp(tap(0, 0x2a), "\x7f"));
	CHECK(!strcmp(tap(LCTRL, 0x2a), "\b"));
	modes.backspace_bs = true; // DECBKM: the other way round
	CHECK(!strcmp(tap(0, 0x2a), "\b"));
	CHECK(!strcmp(tap(LCTRL, 0x2a), "\x7f"));
	modes.backspace_bs = false;
	CHECK(!strcmp(tap(0, 0x2b), "\t"));
	modes.newline_mode = true;
	CHECK(!strcmp(tap(0, 0x28), "\r\n"));
	CHECK(!strcmp(tap(0, 0x58), "\r\n"));
}

static void test_cursor_keys(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x52), "\x1b[A"));
	CHECK(!strcmp(tap(0, 0x51), "\x1b[B"));
	CHECK(!strcmp(tap(0, 0x4f), "\x1b[C"));
	CHECK(!strcmp(tap(0, 0x50), "\x1b[D"));
	CHECK(!strcmp(tap(0, 0x4a), "\x1b[H"));
	modes.cursor_keys_app = true;
	CHECK(!strcmp(tap(0, 0x52), "\x1bOA"));
	CHECK(!strcmp(tap(0, 0x50), "\x1bOD"));
	CHECK(!strcmp(tap(0, 0x4d), "\x1bOF"));
}

static void test_function_and_editing_keys(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x3a), "\x1bOP"));
	CHECK(!strcmp(tap(0, 0x3d), "\x1bOS"));
	CHECK(!strcmp(tap(0, 0x3e), "\x1b[15~"));
	CHECK(!strcmp(tap(0, 0x44), "\x1b[23~"));
	CHECK(!strcmp(tap(0, 0x45), "")); // F12 is SET-UP
	CHECK(!strcmp(tap(0, 0x49), "\x1b[2~"));
	CHECK(!strcmp(tap(0, 0x4c), "\x1b[3~"));
	CHECK(!strcmp(tap(0, 0x4b), "\x1b[5~"));
	CHECK(!strcmp(tap(0, 0x4e), "\x1b[6~"));
	CHECK(!strcmp(tap(0, 0x46), ""));  // Print Screen: nothing
	CHECK(!strcmp(tap(0, 0xe0), ""));  // a modifier usage in a key slot
}

static void test_keypad(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x59), "1"));
	CHECK(!strcmp(tap(0, 0x62), "0"));
	CHECK(!strcmp(tap(0, 0x63), "."));
	CHECK(!strcmp(tap(0, 0x56), "-"));
	CHECK(!strcmp(tap(0, 0x57), "+"));
	CHECK(!strcmp(tap(0, 0x58), "\r"));

	modes.keypad_app = true;
	CHECK(!strcmp(tap(0, 0x59), "\x1bOq"));
	CHECK(!strcmp(tap(0, 0x62), "\x1bOp"));
	CHECK(!strcmp(tap(0, 0x63), "\x1bOn"));
	CHECK(!strcmp(tap(0, 0x56), "\x1bOm"));
	CHECK(!strcmp(tap(0, 0x57), "\x1bOl"));
	CHECK(!strcmp(tap(0, 0x58), "\x1bOM"));

	// Num Lock off: the keypad is the editing keys, and 5 does nothing.
	report(0, 0x53, 0);
	report(0, 0, 0);
	CHECK_EQ(kbd_leds(&k), 0);
	CHECK(!strcmp(tap(0, 0x60), "\x1b[A")); // keypad 8
	CHECK(!strcmp(tap(0, 0x5d), ""));       // keypad 5
	CHECK(!strcmp(tap(0, 0x63), "\x1b[3~"));
}

// VT52 mode: cursor and PF keys ESC x, the alternate keypad ESC ? x.
static void test_vt52_keys(void) {
	setup();
	modes.vt52 = true;
	CHECK(!strcmp(tap(0, 0x52), "\x1b" "A"));
	CHECK(!strcmp(tap(0, 0x51), "\x1b" "B"));
	CHECK(!strcmp(tap(0, 0x4f), "\x1b" "C"));
	CHECK(!strcmp(tap(0, 0x50), "\x1b" "D"));
	modes.cursor_keys_app = true; // DECCKM does not apply
	CHECK(!strcmp(tap(0, 0x52), "\x1b" "A"));
	CHECK(!strcmp(tap(0, 0x3a), "\x1bP"));
	CHECK(!strcmp(tap(0, 0x3d), "\x1bS"));
	CHECK(!strcmp(tap(0, 0x59), "1"));
	modes.keypad_app = true;
	CHECK(!strcmp(tap(0, 0x59), "\x1b?q"));
	CHECK(!strcmp(tap(0, 0x63), "\x1b?n"));
	CHECK(!strcmp(tap(0, 0x58), "\x1b?M"));
}

static void test_held_keys_and_rollover(void) {
	setup();
	out[0] = 0;
	report(0, 0x04, 0);
	report(0, 0x04, 0x05); // a still held, b pressed
	report(0, 0, 0x05);
	CHECK(!strcmp(out, "ab"));

	// ErrorRollOver: ignored, so the held key is not pressed again after it.
	out[0] = 0;
	uint8_t rollover[8] = {0, 0, 1, 1, 1, 1, 1, 1};
	kbd_report(&k, rollover, &modes, now);
	report(0, 0, 0x05);
	CHECK(!strcmp(out, ""));
}

static void test_repeat(void) {
	setup();
	out[0] = 0;
	report(0, 0x04, 0);
	now += KBD_REPEAT_DELAY_MS - 1;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "a"));
	now += 1;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "aa"));
	now += KBD_REPEAT_INTERVAL_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "aaa"));
	// Late: one repeat, not a burst.
	now += 10 * KBD_REPEAT_INTERVAL_MS;
	kbd_tick(&k, &modes, now);
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "aaaa"));

	// Shift pressed while held: the repeat follows it.
	report(LSHIFT, 0x04, 0);
	now += KBD_REPEAT_INTERVAL_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "aaaaA"));

	// The last key pressed repeats; releasing it stops the repeat.
	out[0] = 0;
	report(0, 0x04, 0x05);
	now += KBD_REPEAT_DELAY_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "bb"));
	report(0, 0x04, 0);
	now += KBD_REPEAT_DELAY_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "bb"));

	// Unplugged while held.
	out[0] = 0;
	report(0, 0x06, 0);
	kbd_release_all(&k);
	now += KBD_REPEAT_DELAY_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, "c"));

	// Lock keys do not repeat.
	out[0] = 0;
	report(0, 0, 0);
	report(0, 0x39, 0);
	now += 2 * KBD_REPEAT_DELAY_MS;
	kbd_tick(&k, &modes, now);
	CHECK(!strcmp(out, ""));
	CHECK_EQ(kbd_leds(&k), KBD_LED_NUM_LOCK | KBD_LED_CAPS_LOCK);
}

static int setup_presses;

static void on_setup(void *ctx) {
	(void)ctx;
	++setup_presses;
}

// F12 and Pause are the SET-UP key: they send nothing and do not repeat.
static void test_setup_key(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x45), "")); // no callback set: nothing happens
	kbd_set_setup_key(&k, on_setup);
	setup_presses = 0;
	for (uint8_t key = 0x45; key <= 0x48; key += 3) { // F12, Pause
		report(0, key, 0);
		now += KBD_REPEAT_DELAY_MS + 10 * KBD_REPEAT_INTERVAL_MS;
		kbd_tick(&k, &modes, now);
		report(0, 0, 0);
	}
	CHECK_EQ(setup_presses, 2);
	CHECK(!strcmp(out, ""));
}

static int reset_presses;

static void on_reset(void *ctx) {
	(void)ctx;
	++reset_presses;
}

// F11 sends ESC [ 2 3 ~ until it is made the reset key; then it calls that
// instead, once per press (no repeat).
static void test_reset_key(void) {
	setup();
	CHECK(!strcmp(tap(0, 0x44), "\x1b[23~"));
	kbd_set_reset_key(&k, on_reset);
	reset_presses = 0;
	out[0] = '\0';
	report(0, 0x44, 0);
	now += KBD_REPEAT_DELAY_MS + 10 * KBD_REPEAT_INTERVAL_MS;
	kbd_tick(&k, &modes, now);
	report(0, 0, 0);
	CHECK_EQ(reset_presses, 1);
	CHECK(!strcmp(out, ""));
	kbd_set_reset_key(&k, NULL);
	CHECK(!strcmp(tap(0, 0x44), "\x1b[23~"));
}

int main(void) {
	RUN(test_letters_and_shift);
	RUN(test_setup_key);
	RUN(test_reset_key);
	RUN(test_caps_lock);
	RUN(test_control_and_alt);
	RUN(test_basic_controls);
	RUN(test_cursor_keys);
	RUN(test_function_and_editing_keys);
	RUN(test_keypad);
	RUN(test_vt52_keys);
	RUN(test_held_keys_and_rollover);
	RUN(test_repeat);
	return tests_done();
}
