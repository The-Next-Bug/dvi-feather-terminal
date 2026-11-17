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
//
// Text settings (the printer's target, bd wt6.4) are in a block of their own
// at the page's end (SETTINGS_TEXT_OFFSET): 'T', the length, CRC-32 of the
// text, the text (printable ASCII). Firmware from before it never reads
// there, and its records leave it erased (0xFF): no block, the default.
// The telnet texts (bd xir.10) are a second block just before it
// (SETTINGS_TEXT2_OFFSET): 'U', the length, CRC-32, the terminal type, a
// NUL, the host.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_RECORD_SIZE 256 // one flash page
#define SETTINGS_TARGET_MAX 63   // the printer's target (= PRINTJOB_TARGET_MAX)
// The text block: 'T', length, CRC-32 (4), the text.
#define SETTINGS_TEXT_OFFSET (SETTINGS_RECORD_SIZE - 6 - SETTINGS_TARGET_MAX)
#define SETTINGS_TYPE_MAX 40 // telnet's terminal type override (= TELNET_TYPE_MAX)
#define SETTINGS_HOST_MAX 63 // telnet's host[:port]
// The second text block: 'U', length, CRC-32 (4), the type, NUL, the host.
#define SETTINGS_TEXT2_OFFSET (SETTINGS_TEXT_OFFSET - 6 - SETTINGS_TYPE_MAX - 1 - SETTINGS_HOST_MAX)
#define SETTINGS_SLOTS 2         // flash sectors used in turn

// One byte per setting; the payload stores them in this order (append only).
typedef struct {
	uint8_t columns;    // power-on width: 80 or 132
	uint8_t backspace;  // what Backspace sends at power-on/RIS: 0x7f DEL or 0x08 BS (default: CP/M wants it)
	uint8_t local_echo; // 1: keys typed are also shown on the terminal
	uint8_t baud;       // index: see settings_baud_rate
	uint8_t framing;    // index: see settings_framing
	uint8_t newline;    // LNM at power-on/RIS: 1 on
	uint8_t colors;    // hue: PALETTE_HUE_* (palette.h)
	uint8_t palette;    // 1: mono, every color a level of the hue (palette.h)
	uint8_t mode;       // SETTINGS_MODE_*: what the screen and keyboard are for
	uint8_t status_line; // 1: shown; 0: row 24 left blank (the text area stays 24 rows);
	                     // SETTINGS_STATUS_DEBUG(_USB, _SERIAL): video and main-loop timing (USB events, serial counts) instead
	uint8_t blank_after; // minutes without a key before the screen blanks; 0: never (blank.h)
	// Payload byte 11 held the retired 80 columns layout (0 centered 8x16,
	// 1 full width), so term_type's values avoid 1: older records come back
	// as VT100 (0, or 1 rejected as invalid).
	uint8_t term_type; // SETTINGS_TERM_*: the personality and mode at power-on/RIS
	// The status line's clock (SET-UP's Clock panel, not its main rows):
	uint8_t clock;         // SETTINGS_CLOCK_*: 24-hour, 12-hour, or off
	uint8_t clock_seconds; // 1: with the seconds
	uint8_t clock_date;    // SETTINGS_DATE_*: the date before the time, or none
	uint8_t bell;          // 0 (SETTINGS_BELL_OFF) to SETTINGS_BELL_MAX: the bell's level (bell.c)
	uint8_t usb_log;       // SETTINGS_USB_LOG_*: the USB host's log kept in USBLOG.TXT on the SD card
	// The sound (the Clock and sound panel, with Bell; bd 1ls.18.5):
	uint8_t sound_level;   // 0 (off) to SETTINGS_SOUND_MAX: the AY-3-8910's level (audio.c)
	uint8_t sound_dac;     // 1: the board's DAC is fitted; 0: no sound at all, no AY on the ports
	uint8_t sound_stereo;  // SETTINGS_STEREO_*: the AY's channels left and right (bd 1ls.18.9)
	uint8_t flow;          // SETTINGS_FLOW_*: flow control on the serial line (bd dmt.27)
	uint8_t display;       // SETTINGS_DISPLAY_*: the DVI mode, used from the next boot (bd cpx.6)
	uint8_t display_was;   // not in SET-UP: the mode before an unconfirmed change, or SETTINGS_DISPLAY_NONE
	// The printer on TTY2 (SET-UP's Printer panel, bd wt6.4; printjob.h):
	uint8_t printer;       // SETTINGS_PRINTER_*: off (TTY2 a serial port) or IPP
	uint8_t printer_text;  // IPP: 1 text (AT+PRINTA), 0 raw (AT+PRINTR)
	uint8_t printer_idle;  // seconds without a byte that end a job
	uint8_t telnet;        // SETTINGS_TELNET_*: telnet for the host's bytes (SET-UP's Telnet panel, bd xir.10)
	// New one-byte settings go above this line. Text settings, in the
	// record's text blocks, not the payload:
	char printer_target[SETTINGS_TARGET_MAX + 1]; // host:port/path, NUL-padded
	char telnet_type[SETTINGS_TYPE_MAX + 1];      // TTYPE's name; empty: the Terminal type's
	char telnet_host[SETTINGS_HOST_MAX + 1];      // the last host dialed (F11), host[:port]
} settings_t;

// Telnet (telnet.h's telnet_mode_t, same values): off, once the host sends
// a command, or always.
#define SETTINGS_TELNET_OFF 0
#define SETTINGS_TELNET_AUTO 1
#define SETTINGS_TELNET_ON 2

// The printer (settings_t.printer; printjob.h's PRINTJOB_* values).
#define SETTINGS_PRINTER_OFF 0
#define SETTINGS_PRINTER_IPP 1
// 2 was raw TCP (JetDirect), dropped: such a record comes back as off.
#define SETTINGS_PRINTER_IDLE_DEFAULT 30

#define SETTINGS_MODE_TERMINAL 0 // a serial terminal on UART0
#define SETTINGS_MODE_CPM 1      // the console of the Z80 CP/M machine (machine.h)

#define SETTINGS_TERM_VT100 0 // VT100, ANSI mode
#define SETTINGS_TERM_VT52 2  // VT100 in VT52 mode (DECANM reset)
#define SETTINGS_TERM_ADM3A 3 // Lear Siegler ADM-3A (80 columns)

#define SETTINGS_STATUS_DEBUG 2 // status_line: core 1 timing, late lines, frame rate, main-loop time
#define SETTINGS_STATUS_DEBUG_USB 3 // status_line: the USB host's last events (usbkbd_log)
#define SETTINGS_STATUS_DEBUG_SERIAL 4 // status_line: the serial line's rates, buffer, holds, errors (bd xir.4)

#define SETTINGS_CLOCK_24H 24
#define SETTINGS_CLOCK_12H 12
#define SETTINGS_CLOCK_OFF 0

#define SETTINGS_DATE_NONE 0
#define SETTINGS_DATE_ISO 1   // 2026-10-22
#define SETTINGS_DATE_SHORT 2 // Thu 22 Oct

// The bell's level, 2 dB a step (bell.c). Records from before the levels
// held off/low/medium/high as 0-3: they come back as levels 0-3, never
// louder than the default.
#define SETTINGS_BELL_OFF 0
#define SETTINGS_BELL_DEFAULT 3 // safe in headphones
#define SETTINGS_BELL_MAX 10

#define SETTINGS_SOUND_DEFAULT 7
#define SETTINGS_SOUND_MAX 10

// The AY's layout, ay8910.h's AY_STEREO_* (a program can pick another
// through MACHINE_AY_STEREO). Mono by default: A and C each in one ear
// only tires on headphones.
#define SETTINGS_STEREO_MONO 0
#define SETTINGS_STEREO_ABC 1 // A left, B center, C right
#define SETTINGS_STEREO_ACB 2 // A left, C center, B right

// Flow control on the terminal UART (serial.c). RTS/CTS needs the board's
// CTS and RTS pins (SET-UP does not offer it without them); XON/XOFF takes
// ^Q and ^S out of the received bytes.
#define SETTINGS_FLOW_NONE 0
#define SETTINGS_FLOW_RTSCTS 1
#define SETTINGS_FLOW_XONXOFF 2

// The DVI mode (video.c, hstx_dvi.c). Changing it reboots, and the new mode
// is kept only if a key is pressed on its first screen: until then
// display_was holds the old one, in flash, so a power cycle meanwhile asks
// again (and falls back) rather than keeping a mode the monitor may not show. The default is the
// build's DVI_MODE (SETTINGS_DISPLAY_DEFAULT).
#define SETTINGS_DISPLAY_800 0  // 800x600
#define SETTINGS_DISPLAY_1024 1 // 1024x600
#define SETTINGS_DISPLAY_NONE 2 // display_was: no change waiting to be kept
#ifndef SETTINGS_DISPLAY_DEFAULT
#define SETTINGS_DISPLAY_DEFAULT SETTINGS_DISPLAY_1024
#endif

// The width of the mode s->display picks: 800 or 1024.
#define SETTINGS_DISPLAY_WIDTH(d) ((d) == SETTINGS_DISPLAY_1024 ? 1024u : 800u)

#define SETTINGS_USB_LOG_OFF 0
#define SETTINGS_USB_LOG_SD 1 // appended to the SD card's USBLOG.TXT (apps/terminal/usblog_glue.c)

// The settings, in settings_t order, with the values each can take (the
// first choice need not be the default), their names and the SET-UP page
// they are on: SET-UP's own rows (SETTINGS_MAIN_FIELDS of them, in
// settings_main_field's order) or its Clock panel (clockpanel.h).
#define SETTINGS_FIELDS 27
#define SETTINGS_MAIN_FIELDS 15
#define SETTINGS_MAX_CHOICES 12

#define SETTINGS_PAGE_MAIN 0
#define SETTINGS_PAGE_CLOCK 1 // the Clock and sound panel (clockpanel.h)
#define SETTINGS_PAGE_NONE 2  // kept, not shown (display_was)
#define SETTINGS_PAGE_PRINTER 3 // the Printer panel (printpanel.h)
#define SETTINGS_PAGE_TELNET 4  // the Telnet panel (telnetpanel.h)

typedef struct {
	const char *name;
	uint8_t n_choices;
	uint8_t values[SETTINGS_MAX_CHOICES];
	const char *labels[SETTINGS_MAX_CHOICES];
	uint8_t page; // SETTINGS_PAGE_*
} settings_field_t;

extern const settings_field_t settings_fields[SETTINGS_FIELDS];

// The field shown on SET-UP's row (< SETTINGS_MAIN_FIELDS): the main
// page's fields in their order, but Flow control under Data/parity/stop
// and Display under Palette.
unsigned settings_main_field(unsigned row);

// A field's value in s (i < SETTINGS_FIELDS).
static inline uint8_t *settings_field(settings_t *s, unsigned i) {
	return (uint8_t *)s + i;
}

unsigned settings_baud_rate(const settings_t *s);

// Set the printer's target (cut at SETTINGS_TARGET_MAX, anything but
// printable ASCII dropped), NUL-padded so settings compare with memcmp.
void settings_set_target(settings_t *s, const char *target);

// Likewise telnet's terminal type and host (no spaces in either).
void settings_set_telnet_type(settings_t *s, const char *type);
void settings_set_telnet_host(settings_t *s, const char *host);

// The width in use: the Columns setting, but 80 for the ADM-3A.
unsigned settings_columns(const settings_t *s);

// Why field i is decided by the other settings and has no effect as it is
// (Columns under the ADM-3A: "ADM-3A"), or NULL when it can be changed.
// SET-UP skips such a field and shows the value in use; the stored value is
// kept for when it counts again.
const char *settings_field_fixed(const settings_t *s, unsigned i);

// Data bits (7 or 8), parity ('N', 'E' or 'O') and stop bits (1 or 2).
void settings_framing(const settings_t *s, unsigned *data_bits, char *parity, unsigned *stop_bits);

// What the Colors and Palette settings give besides the palette table
// (palette_build(table, s->colors, s->palette)): the status line's palette
// indices (white in color: bright white on blue; otherwise the text
// colors reversed) and the logo's hue.
typedef struct {
	uint8_t status_fg, status_bg; // palette indices: the status line and the SET-UP panel
	uint8_t logo_fg;              // RGB222 text color, for splash_tint
} settings_colors_t;

settings_colors_t settings_colors(const settings_t *s);

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
