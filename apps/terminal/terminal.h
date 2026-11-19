#ifndef TERMINAL_H
#define TERMINAL_H

// The terminal program's own header (bd gr1.10): what its files share.
// - main.c: the terminal's state and settings, the startup (logo, demo)
//   and the main loop;
// - setup_glue.c: SET-UP and the panels over its rows (Drives, firmware
//   update) and its demo, run from the main loop;
// - cpm_glue.c: CP/M mode, the Z80 machine's and the monitor's platforms;
// - hwinfo_glue.c: the machine's hardware information (port 12);
// - status.c: the status line (terminal, CP/M, debug);
// - input_glue.c: keys from the USB keyboard;
// - usblog_glue.c: the USB log on the SD card (USBLOG.TXT);
// - printer_glue.c: the Z80's printer through a Zimodem on TTY2;
// - crash_glue.c: core 0's panics, hard faults and hangs, reported.
// Everything here is core 0's, from the main loop or the USB stack's
// callbacks (keys).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/time.h"

#include "blank.h"
#include "clockpanel.h"
#include "drivepanel.h"
#include "drives.h"
#include "fwpanel.h"
#include "machine.h"
#include "monitor.h"
#include "printpanel.h"
#include "telnetpanel.h"
#include "screen.h"
#include "settings.h"
#include "setup.h"
#include "term.h"

#define TERM_ROWS (SCREEN_ROWS - 1)
#define STATUS_ROW (SCREEN_ROWS - 1)
#define STATUS_INTERVAL_MS 250
#define DEMO_INTERVAL_MS 5000
#define CPM_KEY_QUEUE 64 // a power of two
#define SETUP_TOP (TERM_ROWS - SETUP_ROWS)

// ---- main.c ----

extern screen_t screen;
extern term_t term;
// Saved rows (screen.h): the main screen while the alternate screen is up,
// and the rows under SET-UP.
extern screen_stash_t stash;
extern bool term_started;
extern bool key_pressed; // before the terminal started: ends the logo and demo
extern bool keys_held;   // keys only set key_pressed, going nowhere (the Display prompt)

extern settings_t settings;          // as saved in flash
extern settings_target_t save_target;
extern settings_colors_t colors;   // in use: the status line's
extern unsigned default_columns;     // the SET-UP width in use: what F11 goes back to
extern blank_t blank;                // the Blank after setting: keys keep the screen lit
extern bool screen_blanked;          // blank_tick's answer: the LED's heartbeat runs then

void show_palette(const settings_t *s);
void apply_settings(const settings_t *s);
// The serial line at rate with the saved framing, until apply_settings
// (SET-UP's save) or line_rate(0), which puts the saved line back (CP/M
// 3's DEVICE through port 14, bd c2d). False if the UART cannot have it.
bool line_rate(unsigned rate);

// Save s as the settings (flash, then `settings`); false if the write
// failed.
bool settings_store(const settings_t *s);

// The Display setting this boot runs with (SETTINGS_DISPLAY_*): saving
// another reboots into it, with display_was = display_booted saved too, so
// that boot (and any after it until a key) asks to keep it (bd cpx.6).
extern uint8_t display_booted;

// ---- setup_glue.c ----

// SET-UP state; the panel's rows are saved (stash) while it covers them.
typedef struct {
	bool active, from_host;
	bool open_requested;  // by the SET-UP key
	bool dial_requested;  // by F11 in terminal mode: the Telnet panel's Host row
	setup_action_t action; // a save or cancel waiting for the main loop
	setup_t ui;
	uint32_t saved_reverse;
	bool saved_cursor;
	uint8_t bell_heard; // the Bell setting last sounded while SET-UP is open
	uint8_t sound_heard; // the Sound chip level last set while SET-UP is open
	uint8_t stereo_heard; // the Stereo setting last set while SET-UP is open
	// CP/M mode: the Drives panel (D), over the same rows; its card work
	// (mount, save or remove a set) waits for the main loop.
	bool drives_open;
	drivepanel_action_t drives_work;
	drivepanel_t drives;
	// The Clock panel (C), over the same rows; setting the clock (I2C)
	// waits for the main loop.
	bool clock_open, clock_apply;
	clockpanel_t clock;
	// The Printer panel (P), over the same rows.
	bool print_open, print_apply;
	printpanel_t print;
	// The Telnet panel (N, or F11 straight to it: direct, where Escape
	// leaves SET-UP too); its Return waits for the main loop.
	bool telnet_open, telnet_direct;
	telnetpanel_action_t telnet_work;
	telnetpanel_t telnet;
	char print_last[64]; // what the last job did, as the panel shows it
	// The firmware update panel (U), over the same rows; its work and its
	// cancel wait for the main loop.
	bool fw_open, fw_work, fw_cancel;
	fwpanel_t fw;
	// The demo screen (T), over the whole screen until a key.
	bool demo, demo_end;
	unsigned demo_cols, term_cols;
	absolute_time_t demo_switch;
} setup_state_t;

extern setup_state_t setup;

void setup_feed(const uint8_t *data, size_t len);
void setup_enter(bool from_host);
void setup_open_telnet(bool direct); // the Telnet panel (SET-UP entered first if it is not open)
// For the panels kept outside setup_glue.c (telnet_glue.c):
void setup_draw_panel(void);
void setup_leave(bool apply);       // rows back; apply: use the edited settings
bool save_and_leave(void);          // SET-UP's Return; false if the flash write failed
void telnet_feed(uint8_t c, uint32_t now); // a key for the Telnet panel
void telnet_service(void);          // its Return or Escape, from the main loop
void setup_service(void);
void draw_demo(unsigned cols, const char *what);

// ---- cpm_glue.c ----

// The Z80 machine (CP/M mode). Keys are queued here and handed to the CPU
// or the monitor from the main loop, never from inside the USB stack.
typedef struct {
	bool on;      // Mode was CP/M at power-on
	bool running; // the CPU runs; otherwise the monitor has the console
	bool reset_requested; // F11, from inside the USB stack
	uint8_t keys[CPM_KEY_QUEUE];
	unsigned key_head, key_tail;
	uint32_t khz; // last measured emulated clock
	uint32_t light_until[DRIVES]; // ms: the drive's letter is lit until then
	uint32_t usb_light_until; // ms: USB is lit until then
	uint16_t lit; // drive letters (bits 0-7) and USB (bit 8) shown lit on the status line
} cpm_state_t;

extern cpm_state_t cpm;
extern const machine_platform_t cpm_platform;
extern const monitor_platform_t cpm_monitor;

void cpm_step(void);

// ---- hwinfo_glue.c ----

// The machine's hardware information device (port 12, machine.h).
size_t cpm_hwinfo(uint8_t item, uint8_t *buf);

// ---- status.c ----

typedef struct {
	unsigned keyboards;
	settings_t settings;
	unsigned baud;      // in use: CP/M can change it (line_rate)
	char usb[12];       // usb_label
	const char *type;   // term_type_name: the host can switch VT52 mode
	uint8_t telnet;     // telnet_glue_status (telnet_status_t)
} status_t;

bool status_shown(void);
status_t status_now(void);
bool status_equal(const status_t *a, const status_t *b);
void draw_status(const status_t *st);
void draw_cpm_status(void);
uint16_t drive_lights(uint32_t now);
void status_clock_poll(void); // each main-loop pass: the clock's colon and minute
void status_flow_poll(void);  // each main-loop pass: the flow control field's lit names
bool debug_status_on(void);
void debug_status_start(void);
void debug_status_pass(void);
void debug_status_refresh(void);

// ---- input_glue.c ----

// The last key sequence sent to the host, for the status line.
typedef struct {
	uint8_t data[8];
	uint8_t len;
	uint32_t count; // keys sent: tells a repeat of the same key apart
} last_key_t;

extern last_key_t last_key;

void send_key(void *ctx, const uint8_t *data, size_t len);
void setup_key(void *ctx);
void reset_key(void *ctx);
void dial_key(void *ctx);
void keyboard_poll(void);

// ---- telnet_glue.c ----

// Telnet in terminal mode (bd xir.10): the status line's view of it.
typedef enum {
	TELNET_STATUS_OFF,     // off (or CP/M mode)
	TELNET_STATUS_WAITING, // auto or on, nothing negotiated yet
	TELNET_STATUS_ACTIVE,  // the server negotiated
	TELNET_STATUS_HANGUP,  // a hang-up's +++ and ATH under way
} telnet_status_t;

void telnet_glue_init(void);  // after term_init, before the first apply_settings
void telnet_glue_apply(void); // the Telnet setting changed (apply_settings)
// The host's bytes, telnet commands taken out: out has room for n + 1.
size_t telnet_glue_filter(const uint8_t *in, size_t n, uint8_t *out);
void telnet_glue_send(const uint8_t *data, size_t len); // keys and replies to the host
void telnet_glue_poll(void);  // each main-loop pass: RIS, size, type, the hang-up
bool telnet_glue_dial(const char *host); // ATD"host", telnet on for the call
void telnet_glue_hangup(void);
const char *telnet_glue_call(void);   // the call's host, NULL: none
const char *telnet_type_default(void); // TTYPE from the Terminal type now
telnet_status_t telnet_glue_status(void);

// ---- modem_glue.c ----

// The WiFi modem's start-up (bd xir.17.2; WIFI_MODEM builds with the Port
// on the modem, else no-ops). While busy the line is its own: nothing else
// reads or writes the terminal UART.
void modem_glue_start(void);  // at boot: reset the module and set it up
extern bool modem_owns_line;  // in SRAM (main.c): read on every SIO2 byte
static inline bool modem_glue_busy(void) {
	return modem_owns_line;
}
void modem_glue_poll(void);   // while busy: each main-loop pass, the logo's wait and the CPU's
void modem_glue_describe(char *text, size_t size); // one line, for SET-UP

// ---- printer_glue.c ----

typedef enum {
	PRINTER_STATUS_OFF,    // TTY2 is the serial port
	PRINTER_STATUS_IDLE,   // no job, the last one fine (or none)
	PRINTER_STATUS_BUSY,   // a job is being opened, printed or ended
	PRINTER_STATUS_FAILED, // the job, or the last one, failed
} printer_status_t;

// The Printer settings in use (apply_settings; CP/M mode only).
void printer_configure(const settings_t *s);
// TTY2 is the printer's (SIO2 gets nothing).
bool printer_has_tty2(void);
void printer_poll(void); // each main-loop pass and while the CPU waits
// The machine's printer hooks (ports 5/6).
void printer_out(uint8_t c);
bool printer_ready(void);
void printer_end(void);
printer_status_t printer_status(void);
void printer_last_job(char *out, size_t size); // the Printer panel's last line
size_t printer_record(uint8_t *buf);           // HWINFO_PRINTER

// ---- usblog_glue.c ----

void usblog_poll(void); // each main-loop pass

// ---- crash_glue.c ----

// Before usbkbd_init: the report a crash's reboot kept (NULL if none), also
// put in the USB log with the log text it kept; starts the hang check.
const char *crash_init(void);
void crash_heartbeat(void);     // core 0 is not stuck (keyboard_poll, the monitor's long writes)
bool crash_redraw_status(void); // a hang report covered the status line

#endif
