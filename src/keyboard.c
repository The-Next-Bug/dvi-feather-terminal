#include "keyboard.h"

#include <string.h>

// Boot report modifier bits.
#define MOD_CTRL 0x11  // left, right
#define MOD_SHIFT 0x22
#define MOD_ALT 0x44

// HID keyboard usages (USB HID Usage Tables, page 0x07).
enum {
	KEY_ERROR_ROLLOVER = 0x01,
	KEY_A = 0x04,
	KEY_Z = 0x1d,
	KEY_1 = 0x1e,
	KEY_ENTER = 0x28,
	KEY_ESCAPE = 0x29,
	KEY_BACKSPACE = 0x2a,
	KEY_TAB = 0x2b,
	KEY_SPACE = 0x2c,
	KEY_SLASH = 0x38,
	KEY_CAPS_LOCK = 0x39,
	KEY_F1 = 0x3a,
	KEY_F12 = 0x45,
	KEY_INSERT = 0x49,
	KEY_HOME = 0x4a,
	KEY_PAGE_UP = 0x4b,
	KEY_DELETE = 0x4c,
	KEY_END = 0x4d,
	KEY_PAGE_DOWN = 0x4e,
	KEY_RIGHT = 0x4f,
	KEY_LEFT = 0x50,
	KEY_DOWN = 0x51,
	KEY_UP = 0x52,
	KEY_NUM_LOCK = 0x53,
	KEY_KP_SLASH = 0x54,
	KEY_KP_ASTERISK = 0x55,
	KEY_KP_MINUS = 0x56,
	KEY_KP_PLUS = 0x57,
	KEY_KP_ENTER = 0x58,
	KEY_KP_1 = 0x59,
	KEY_KP_0 = 0x62,
	KEY_KP_DOT = 0x63,
	KEY_NON_US_BACKSLASH = 0x64,
	KEY_KP_EQUAL = 0x67,
	KEY_KP_COMMA = 0x85,
};

// Printable keys from KEY_1 (0x1e) to KEY_SLASH (0x38), unshifted and
// shifted; Enter .. Space (0x28-0x2c) are handled separately.
static const char plain_keys[][2] = {
	{'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
	{'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'},
	{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, // Enter, Escape, Backspace, Tab, Space
	{'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
	{'#', '~'}, // non-US # (ISO keyboards)
	{';', ':'}, {'\'', '"'}, {'`', '~'}, {',', '<'}, {'.', '>'}, {'/', '?'},
};

// Keypad KP_1 .. KP_DOT: the character in numeric mode, the final byte of
// ESC O x in application mode (DECKPAM), and the editing key it is with
// Num Lock off (0: none).
static const struct {
	char numeric, app;
	uint8_t nav;
} keypad[] = {
	{'1', 'q', KEY_END}, {'2', 'r', KEY_DOWN}, {'3', 's', KEY_PAGE_DOWN},
	{'4', 't', KEY_LEFT}, {'5', 'u', 0}, {'6', 'v', KEY_RIGHT},
	{'7', 'w', KEY_HOME}, {'8', 'x', KEY_UP}, {'9', 'y', KEY_PAGE_UP},
	{'0', 'p', KEY_INSERT}, {'.', 'n', KEY_DELETE},
};

// F5 .. F12: the n of ESC [ n ~ (VT220 / xterm).
static const uint8_t function_keys[] = {15, 17, 18, 19, 20, 21, 23, 24};

typedef struct {
	uint8_t data[8];
	size_t len;
} seq_t;

static void seq_add(seq_t *s, uint8_t c) {
	if (s->len < sizeof s->data)
		s->data[s->len++] = c;
}

// ESC [ x, or ESC O x in the given application mode.
static void seq_ss3(seq_t *s, bool app, char final) {
	seq_add(s, 0x1b);
	seq_add(s, app ? 'O' : '[');
	seq_add(s, (uint8_t)final);
}

// ESC [ n ~
static void seq_tilde(seq_t *s, unsigned n) {
	seq_add(s, 0x1b);
	seq_add(s, '[');
	if (n >= 10)
		seq_add(s, (uint8_t)('0' + n / 10));
	seq_add(s, (uint8_t)('0' + n % 10));
	seq_add(s, '~');
}

static void seq_return(seq_t *s, const kbd_modes_t *modes) {
	seq_add(s, '\r');
	if (modes->newline_mode)
		seq_add(s, '\n');
}

// Ctrl with a character: the control code it makes, as on a VT100 (and
// xterm for the digits).
static uint8_t control(uint8_t c) {
	if (c >= 0x40 && c <= 0x7e)
		return c & 0x1f;
	switch (c) {
	case ' ':
	case '2':
		return 0x00;
	case '6':
		return 0x1e;
	case '-':
	case '/':
		return 0x1f;
	case '8':
	case '?':
		return 0x7f;
	default:
		return c;
	}
}

// Editing and cursor keys (also the keypad with Num Lock off).
static bool editing_key(seq_t *s, uint8_t key, const kbd_modes_t *modes) {
	switch (key) {
	case KEY_UP:
		seq_ss3(s, modes->cursor_keys_app, 'A');
		return true;
	case KEY_DOWN:
		seq_ss3(s, modes->cursor_keys_app, 'B');
		return true;
	case KEY_RIGHT:
		seq_ss3(s, modes->cursor_keys_app, 'C');
		return true;
	case KEY_LEFT:
		seq_ss3(s, modes->cursor_keys_app, 'D');
		return true;
	case KEY_HOME:
		seq_ss3(s, modes->cursor_keys_app, 'H');
		return true;
	case KEY_END:
		seq_ss3(s, modes->cursor_keys_app, 'F');
		return true;
	case KEY_INSERT:
		seq_tilde(s, 2);
		return true;
	case KEY_DELETE:
		seq_tilde(s, 3);
		return true;
	case KEY_PAGE_UP:
		seq_tilde(s, 5);
		return true;
	case KEY_PAGE_DOWN:
		seq_tilde(s, 6);
		return true;
	default:
		return false;
	}
}

// A keypad key in numeric or application mode: the character, or ESC O x.
static void keypad_key(seq_t *s, const kbd_modes_t *modes, char numeric, char app) {
	if (modes->keypad_app)
		seq_ss3(s, true, app);
	else
		seq_add(s, (uint8_t)numeric);
}

// What key sends with these modifiers; empty for keys that send nothing.
static void translate(const kbd_t *k, uint8_t key, uint8_t modifiers, const kbd_modes_t *modes, seq_t *s) {
	bool shift = modifiers & MOD_SHIFT, ctrl = modifiers & MOD_CTRL;
	int c = -1; // a single character, which Ctrl and Alt modify

	if (key >= KEY_A && key <= KEY_Z) {
		c = 'a' + (key - KEY_A);
		if (shift != k->caps_lock)
			c -= 'a' - 'A';
	} else if (key >= KEY_1 && key <= KEY_SLASH && plain_keys[key - KEY_1][0]) {
		c = plain_keys[key - KEY_1][shift];
	} else if (key == KEY_NON_US_BACKSLASH) {
		c = shift ? '|' : '\\';
	} else if (key == KEY_SPACE) {
		c = ' ';
	} else if (key == KEY_TAB) {
		c = '\t';
	} else if (key == KEY_ESCAPE) {
		c = 0x1b;
	} else if (key == KEY_BACKSPACE) {
		c = ctrl ? '\b' : 0x7f;
		ctrl = false;
	} else if (key == KEY_ENTER) {
		seq_return(s, modes);
	} else if (key >= KEY_F1 && key < KEY_F1 + 4) {
		seq_ss3(s, true, (char)('P' + (key - KEY_F1))); // PF1-PF4
	} else if (key >= KEY_F1 + 4 && key <= KEY_F12) {
		seq_tilde(s, function_keys[key - KEY_F1 - 4]);
	} else if (key >= KEY_KP_1 && key <= KEY_KP_DOT) {
		unsigned i = key - KEY_KP_1;
		if (k->num_lock)
			keypad_key(s, modes, keypad[i].numeric, keypad[i].app);
		else if (keypad[i].nav)
			editing_key(s, keypad[i].nav, modes);
	} else if (key == KEY_KP_ENTER) {
		if (modes->keypad_app)
			seq_ss3(s, true, 'M');
		else
			seq_return(s, modes);
	} else if (key == KEY_KP_MINUS) {
		keypad_key(s, modes, '-', 'm');
	} else if (key == KEY_KP_PLUS || key == KEY_KP_COMMA) {
		// The PC's keypad + sits where the VT100's keypad comma is.
		keypad_key(s, modes, key == KEY_KP_PLUS ? '+' : ',', 'l');
	} else if (key == KEY_KP_SLASH) {
		keypad_key(s, modes, '/', 'o'); // xterm
	} else if (key == KEY_KP_ASTERISK) {
		keypad_key(s, modes, '*', 'j'); // xterm
	} else if (key == KEY_KP_EQUAL) {
		keypad_key(s, modes, '=', 'X'); // xterm
	} else {
		editing_key(s, key, modes);
	}

	if (c >= 0) {
		if (modifiers & MOD_ALT)
			seq_add(s, 0x1b);
		seq_add(s, ctrl ? control((uint8_t)c) : (uint8_t)c);
	}
}

static void send(kbd_t *k, uint8_t key, const kbd_modes_t *modes) {
	seq_t s = {.len = 0};
	translate(k, key, k->modifiers, modes, &s);
	if (s.len && k->output)
		k->output(k->output_ctx, s.data, s.len);
}

static bool in_report(const uint8_t *keys, uint8_t key) {
	for (int i = 0; i < KBD_REPORT_KEYS; ++i)
		if (keys[i] == key)
			return true;
	return false;
}

void kbd_init(kbd_t *k, kbd_output_t output, void *ctx) {
	memset(k, 0, sizeof *k);
	k->num_lock = true;
	k->output = output;
	k->output_ctx = ctx;
}

bool kbd_report(kbd_t *k, const uint8_t report[8], const kbd_modes_t *modes, uint32_t now_ms) {
	const uint8_t *keys = report + 2;
	// Too many keys down: the keyboard reports ErrorRollOver in every slot
	// and the previous state stands.
	if (keys[0] == KEY_ERROR_ROLLOVER)
		return false;

	k->modifiers = report[0];
	bool leds_changed = false;
	for (int i = 0; i < KBD_REPORT_KEYS; ++i) {
		uint8_t key = keys[i];
		if (key < KEY_A || in_report(k->keys, key))
			continue; // empty slot, error code or still held
		if (key == KEY_CAPS_LOCK) {
			k->caps_lock = !k->caps_lock;
			leds_changed = true;
		} else if (key == KEY_NUM_LOCK) {
			k->num_lock = !k->num_lock;
			leds_changed = true;
		} else {
			send(k, key, modes);
			k->repeat_key = key;
			k->repeat_at = now_ms + KBD_REPEAT_DELAY_MS;
		}
	}
	memcpy(k->keys, keys, KBD_REPORT_KEYS);
	if (k->repeat_key && !in_report(keys, k->repeat_key))
		k->repeat_key = 0;
	return leds_changed;
}

void kbd_tick(kbd_t *k, const kbd_modes_t *modes, uint32_t now_ms) {
	if (!k->repeat_key || (int32_t)(now_ms - k->repeat_at) < 0)
		return;
	send(k, k->repeat_key, modes);
	// If the caller was late, carry on from now rather than catching up
	// with a burst.
	k->repeat_at += KBD_REPEAT_INTERVAL_MS;
	if ((int32_t)(now_ms - k->repeat_at) >= 0)
		k->repeat_at = now_ms + KBD_REPEAT_INTERVAL_MS;
}

void kbd_release_all(kbd_t *k) {
	k->modifiers = 0;
	memset(k->keys, 0, sizeof k->keys);
	k->repeat_key = 0;
}

uint8_t kbd_leds(const kbd_t *k) {
	return (k->num_lock ? KBD_LED_NUM_LOCK : 0) | (k->caps_lock ? KBD_LED_CAPS_LOCK : 0);
}
