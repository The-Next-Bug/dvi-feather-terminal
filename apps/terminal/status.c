// The status line, row 24 (terminal.h): the terminal's (type, width,
// line settings, keyboard, stick, echo), CP/M mode's (drive
// letters lit while accessed, the stick, the Z80's speed) or the debug
// choices' (timing, the USB host's events, the serial line's counts and
// the last key); blank when hidden. The first
// two end in the clock once the time is real.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "audio.h"
#include "bell.h"
#include "datetime.h"
#include "keyname.h"
#include "palette.h"
#include "rtc.h"
#include "serial.h"
#include "term_build.h"
#include "terminal.h"
#include "usbkbd.h"
#include "usbmsc.h"
#include "video.h"

#define DRIVE_LIGHT_MS 150 // a drive letter stays lit this long after an access

// With the Status line setting hidden, row 24 is left blank (the text area
// stays 24 rows) and the status line drawers stop here.
bool status_shown(void) {
	if (settings.status_line) {
		return true;
	}
	screen_clear_line(&screen, STATUS_ROW, PALETTE_TEXT, PALETTE_BACKGROUND);
	return false;
}

// The clock at the right end (bd 1ls.6.3), as the Status line clock,
// Seconds and Date settings choose (SET-UP's Clock panel, bd 1ls.6.5):
// shown once the time is real (the RTC's, or set since start-up), a space
// either side, the colons on for the first half of each second, in step
// with the RTC's 1 Hz output while it ticks. Only its cells are redrawn as
// it changes; the line's text stops before it.
#define CLOCK_COLON_US 500000u

static uint32_t clock_drawn = UINT32_MAX; // the minute (or second) * 2 + colon shown

static datetime_clock_t clock_format(void) {
	return (datetime_clock_t){settings.clock == SETTINGS_CLOCK_12H, settings.clock_seconds != 0,
		settings.clock_date};
}

static bool clock_on(void) {
	return rtc_valid() && settings.clock != SETTINGS_CLOCK_OFF && settings.status_line && !debug_status_on() &&
		!setup.demo;
}

// The columns the clock takes, its spaces included (0 when it is off).
static unsigned clock_cols(void) {
	if (!clock_on()) {
		return 0;
	}
	char text[DATETIME_CLOCK_MAX + 1];
	datetime_clock_t format = clock_format();
	return datetime_clock_text(text, 0, true, &format) + 2; // every time is as wide
}

static void draw_clock(bool force) {
	if (!clock_on()) {
		return;
	}
	datetime_clock_t format = clock_format();
	uint32_t secs = rtc_now();
	bool colons = rtc_fraction_us() < CLOCK_COLON_US;
	uint32_t key = (format.seconds ? secs : secs / 60) * 2 + colons;
	if (key == clock_drawn && !force) {
		return;
	}
	clock_drawn = key;
	char text[DATETIME_CLOCK_MAX + 3] = " ";
	unsigned n = datetime_clock_text(text + 1, secs, colons, &format);
	text[n + 1] = ' ';
	text[n + 2] = '\0';
	screen_puts(&screen, screen.cols - (n + 2), STATUS_ROW, text, colors.status_fg, colors.status_bg);
}

void status_clock_poll(void) {
	draw_clock(false);
}

// The flow control field (bd xir.3): "RTS/CTS" or "XON/XOFF", its left
// name lit while the terminal holds the host off (RTS down, or XOFF sent),
// its right name while the host holds the terminal (CTS down, or the
// host's XOFF). A hold lights its name at least FLOW_LIGHT_MS, so a short
// one (a CTS change between two polls) still shows. Only its cells are
// redrawn as it changes, from each main-loop pass.
#define FLOW_LIGHT_MS 250

static struct {
	unsigned col;            // where the field starts on the status line; 0: not on it
	unsigned lit;            // bit 0 left name, bit 1 right, as drawn
	uint32_t holding_count, held_count; // serial_flow_state's at the last poll
	uint32_t left_until, right_until;   // lit until (ms since boot)
} flow_light;

// The field's text for the Flow control setting now: empty for none.
static const char *flow_label(void) {
	serial_flow_t f = serial_flow();
	return f == SERIAL_FLOW_RTSCTS ? "RTS/CTS" : f == SERIAL_FLOW_XONXOFF ? "XON/XOFF" : "";
}

static void draw_flow(bool force) {
	const char *label = flow_label();
	uint32_t now = to_ms_since_boot(get_absolute_time());
	serial_flow_state_t s = serial_flow_state();
	if (s.holding || s.holding_count != flow_light.holding_count) {
		flow_light.left_until = now + FLOW_LIGHT_MS;
	}
	if (s.held || s.held_count != flow_light.held_count) {
		flow_light.right_until = now + FLOW_LIGHT_MS;
	}
	flow_light.holding_count = s.holding_count;
	flow_light.held_count = s.held_count;
	unsigned lit = ((int32_t)(flow_light.left_until - now) > 0 ? 1u : 0u) |
		((int32_t)(flow_light.right_until - now) > 0 ? 2u : 0u);
	bool shown = settings.status_line && !debug_status_on() && !setup.demo; // the field is on the line
	if (!shown || !flow_light.col || !label[0] || (lit == flow_light.lit && !force)) {
		return;
	}
	flow_light.lit = lit;
	size_t slash = strcspn(label, "/");
	for (size_t i = 0; label[i]; ++i) {
		bool on = i < slash ? lit & 1u : i > slash && lit & 2u;
		screen_put(&screen, flow_light.col + (unsigned)i, STATUS_ROW, (uint8_t)label[i],
			on ? colors.status_bg : colors.status_fg, on ? colors.status_fg : colors.status_bg);
	}
}

void status_flow_poll(void) {
	draw_flow(false);
}

// Append " | " and the flow control field to a status line's text (at n,
// its length) and note where it starts; nothing for no flow control.
static size_t put_flow_field(char *text, size_t size, size_t n, const char *sep) {
	const char *label = flow_label();
	if (label[0] && n + strlen(sep) + strlen(label) < size) {
		flow_light.col = (unsigned)(n + strlen(sep));
		snprintf(text + n, size - n, "%s%s", sep, label);
		n = strlen(text);
	}
	return n;
}

// A status line's text, ended where the clock begins; the flow control
// field drawn lit as it is, unless the clock cut it off.
static void put_status_text(char *text) {
	size_t end = (size_t)screen.cols - clock_cols();
	if (strlen(text) > end) {
		text[end] = '\0';
	}
	if (flow_light.col + strlen(flow_label()) > strlen(text)) {
		flow_light.col = 0;
	}
	screen_clear_line(&screen, STATUS_ROW, colors.status_fg, colors.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colors.status_fg, colors.status_bg);
	draw_clock(true);
	draw_flow(true);
}

// The WiFi modem once started (bd xir.17.6): "wifi" on a network, lit
// while the modem has a connection open (DCD); "no wifi" without one;
// "modem?" when it did not answer. NULL for the serial line, and while it
// starts (the notice at the line's start says more).
static const char *wifi_label(uint8_t status) {
	switch (status) {
	case MODEM_STATUS_WIFI:
		return "wifi";
	case MODEM_STATUS_NO_WIFI:
		return "no wifi";
	case MODEM_STATUS_FAILED:
		return "modem?";
	default:
		return NULL;
	}
}

// The label at col drawn lit (reverse), if the line's text reached it.
static void light_label(unsigned col, const char *label, const char *text) {
	if (!col || col + strlen(label) > strlen(text)) {
		return;
	}
	for (unsigned i = 0; label[i]; ++i) {
		screen_put(&screen, col + i, STATUS_ROW, (uint8_t)label[i], colors.status_bg, colors.status_fg);
	}
}

// The USB stick on the status line: "USB" while one is volume 1 (USB: to
// XLS and XPIP), "+n" for others waiting to take over, "?" if one could
// not be used; empty if none is in.
static void usb_label(char *out, size_t size) {
	usbmsc_status_t u = usbmsc_status();
	out[0] = '\0';
	if (!u.mounted && !u.waiting && !u.unusable) {
		return;
	}
	int n = snprintf(out, size, "%s", u.mounted ? "USB" : "usb");
	if (u.waiting && n > 0 && (size_t)n < size) {
		n += snprintf(out + n, size - (size_t)n, "+%u", u.waiting);
	}
	if (u.unusable && n > 0 && (size_t)n < size) {
		snprintf(out + n, size - (size_t)n, "?");
	}
}

// The Status line setting's debug choice: core 1's timing and core 0's
// main-loop time, refreshed every STATUS_INTERVAL_MS in both modes.
static struct {
	char text[SCREEN_MAX_COLS + 1]; // as last drawn: redrawn as is between refreshes
	uint32_t last_ms, frames, late;  // at the last refresh
	uint32_t line_peak;              // compose cycles, since the debug line came on
	uint32_t render_peak_us;         // likewise
	uint32_t pass_start_us, pass_max_us, pass_peak_us; // main-loop passes
	uint32_t audio_us;               // audio_busy_us at the last refresh
} debug_status;

bool debug_status_on(void) {
	return settings.status_line == SETTINGS_STATUS_DEBUG || settings.status_line == SETTINGS_STATUS_DEBUG_USB ||
		settings.status_line == SETTINGS_STATUS_DEBUG_SERIAL;
}

// debug serial (bd xir.4): is this terminal ever the bottleneck? The
// rates over the last whole second; the rest since the line came on.
static struct {
	uint32_t rate_ms;            // the last rate's end
	serial_stats_t rate_at;      // the counts then
	unsigned rx_rate, tx_rate;   // bytes a second
	uint32_t peak;               // the receive buffer's fullest since on
	serial_stats_t base;         // the counts when the line came on
	serial_flow_state_t flow_base;
} debug_serial;

// " rx R/s tx T/s|buf N/P/M|hold H/O|ovr V err E drop D/X|key K": bytes a
// second each way; the receive buffer now / fullest this refresh / since
// on (of 2048: the terminal holds the host at 1984); holds the terminal
// started / the host's (CTS changes, either way, or XOFFs); UART
// overruns, framing/parity/break errors, bytes lost to a full receive
// buffer / transmit queue; the last key sent (only here, not on the
// normal line, where it showed passwords as they were typed). Fits 80
// columns.
static void debug_serial_text(char *t, size_t size, uint32_t now, bool restart) {
	serial_stats_t st = serial_stats();
	serial_flow_state_t fl = serial_flow_state();
	if (restart) {
		debug_serial.base = debug_serial.rate_at = st;
		debug_serial.flow_base = fl;
		debug_serial.rate_ms = now;
		debug_serial.rx_rate = debug_serial.tx_rate = 0;
		debug_serial.peak = 0;
	}
	uint32_t ms = now - debug_serial.rate_ms;
	if (ms >= 1000) {
		debug_serial.rx_rate = (unsigned)((uint64_t)(st.received - debug_serial.rate_at.received) * 1000 / ms);
		debug_serial.tx_rate = (unsigned)((uint64_t)(st.sent - debug_serial.rate_at.sent) * 1000 / ms);
		debug_serial.rate_at = st;
		debug_serial.rate_ms = now;
	}
	if (st.rx_peak > debug_serial.peak) {
		debug_serial.peak = st.rx_peak;
	}
	serial_reset_rx_peak();
	int n = snprintf(t, size, " rx %u/s tx %u/s|buf %lu/%lu/%lu|hold %lu/%lu|ovr %lu err %lu drop %lu/%lu",
		debug_serial.rx_rate, debug_serial.tx_rate, (unsigned long)st.rx_level, (unsigned long)st.rx_peak,
		(unsigned long)debug_serial.peak, (unsigned long)(fl.holding_count - debug_serial.flow_base.holding_count),
		(unsigned long)(fl.held_count - debug_serial.flow_base.held_count),
		(unsigned long)(st.overruns - debug_serial.base.overruns),
		(unsigned long)(st.errors - debug_serial.base.errors),
		(unsigned long)(st.dropped - debug_serial.base.dropped),
		(unsigned long)(st.tx_dropped - debug_serial.base.tx_dropped));
	if (last_key.count && n > 0 && (size_t)n < size) {
		char name[24];
		keyname(last_key.data, last_key.len, name, sizeof name);
		snprintf(t + n, size - (size_t)n, "|key %s", name);
	}
}

// The main loop starts.
void debug_status_start(void) {
	debug_status.pass_start_us = time_us_32();
}

// One main-loop pass ends and the next starts.
void debug_status_pass(void) {
	uint32_t now = time_us_32(), us = now - debug_status.pass_start_us;
	debug_status.pass_start_us = now;
	if (us > debug_status.pass_max_us) {
		debug_status.pass_max_us = us;
	}
}

static unsigned percent(uint32_t part, uint32_t whole) {
	return whole ? (unsigned)((part * 100 + whole / 2) / whole) : 0;
}

// " late T+N|line C/P%|Ffps|loop M/Qms|render R/Sus|Z MHz|snd A%": late
// lines in all (+ since the last refresh); the longest scanline compose as
// a share of a line's time, this refresh / the peak; frames a second; the
// longest main-loop pass and the longest line render on core 0 (core 1 can
// show that line half drawn meanwhile), now / peak; in CP/M mode the Z80's
// speed; with a DAC, core 0's time in the audio engine's interrupt since
// the last refresh (bd 1ls.18.8). Tight: it fits 80 columns.
void debug_status_refresh(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	video_stats_t st = video_stats();
	uint32_t ms = now - debug_status.last_ms;
	bool restart = ms > 4 * STATUS_INTERVAL_MS;
	if (restart) {
		// Just turned on (or the first time): the peaks start again.
		debug_status.line_peak = debug_status.pass_peak_us = debug_status.render_peak_us = 0;
		ms = 0;
	}
	if (st.max_line_cycles > debug_status.line_peak) {
		debug_status.line_peak = st.max_line_cycles;
	}
	uint32_t pass_us = debug_status.pass_max_us;
	debug_status.pass_max_us = 0;
	if (pass_us > debug_status.pass_peak_us) {
		debug_status.pass_peak_us = pass_us;
	}
	if (st.max_render_us > debug_status.render_peak_us) {
		debug_status.render_peak_us = st.max_render_us;
	}
	unsigned fps = ms ? (unsigned)(((st.frames - debug_status.frames) * 1000 + ms / 2) / ms) : 0;
	char *t = debug_status.text;
	size_t size = sizeof debug_status.text;
	int n = snprintf(t, size, " late %lu+%lu|line %u/%u%%|%ufps|loop %lu/%lums|render %lu/%luus",
		(unsigned long)st.late, (unsigned long)(ms ? st.late - debug_status.late : 0),
		percent(st.max_line_cycles, st.cycles_budget), percent(debug_status.line_peak, st.cycles_budget), fps,
		(unsigned long)((pass_us + 500) / 1000), (unsigned long)((debug_status.pass_peak_us + 500) / 1000),
		(unsigned long)st.max_render_us, (unsigned long)debug_status.render_peak_us);
	if (cpm.on && cpm.running && n > 0 && (size_t)n < size) {
		n += snprintf(t + n, size - (size_t)n, "|%lu.%02luMHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	}
	uint32_t audio_us = audio_busy_us();
	if (bell_present() && ms && n > 0 && (size_t)n < size) {
		uint32_t per_mille = (audio_us - debug_status.audio_us) / ms; // us a ms
		snprintf(t + n, size - (size_t)n, "|snd %lu.%lu%%", (unsigned long)(per_mille / 10),
			(unsigned long)(per_mille % 10));
	}
	debug_status.audio_us = audio_us;
	debug_status.last_ms = now;
	debug_status.frames = st.frames;
	debug_status.late = st.late;
	// debug usb: the USB host's last events instead, after the time, which
	// stops if the main loop (keyboard, USB, CP/M) does.
	if (settings.status_line == SETTINGS_STATUS_DEBUG_USB) {
		n = snprintf(t, size, " %lus | usb:", (unsigned long)(now / 1000));
		if (n > 0 && (size_t)n < size) {
			usbkbd_log_text(t + n, size - (size_t)n);
		}
	} else if (settings.status_line == SETTINGS_STATUS_DEBUG_SERIAL) {
		debug_serial_text(t, size, now, restart);
	}
}

static void draw_debug_status(void) {
	screen_clear_line(&screen, STATUS_ROW, colors.status_fg, colors.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, debug_status.text, colors.status_fg, colors.status_bg);
}

// Drives accessed light up on the status line, like a drive's busy light;
// so does USB (bit 8) while the stick is read or written.
uint16_t drive_lights(uint32_t now) {
	uint8_t accessed = drives_activity();
	uint16_t lit = 0;
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (accessed & 1u << d) {
			cpm.light_until[d] = now + DRIVE_LIGHT_MS;
		}
		if ((int32_t)(cpm.light_until[d] - now) > 0) {
			lit |= (uint16_t)(1u << d);
		}
	}
	if (usbmsc_activity()) {
		cpm.usb_light_until = now + DRIVE_LIGHT_MS;
	}
	if ((int32_t)(cpm.usb_light_until - now) > 0) {
		lit |= 1u << DRIVES;
	}
	return lit;
}

void draw_cpm_status(void) {
	static uint32_t next_speed_ms;
	uint32_t now = to_ms_since_boot(get_absolute_time());
	if ((int32_t)(now - next_speed_ms) >= 0) {
		next_speed_ms = now + 1000;
		cpm.khz = machine_speed_khz();
	}
	cpm.lit = drive_lights(now);
	if (setup.demo || !status_shown()) {
		return; // the demo has the status line
	}
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
	// The WiFi modem starting (bd xir.17.2) first: the line is not hung.
	char text[SCREEN_MAX_COLS + 1];
	const char *modem = modem_glue_notice();
	snprintf(text, sizeof text, "%s%s%s CP/M |", modem ? " " : "", modem ? modem : "", modem ? " |" : "");
	unsigned letter_col[DRIVES];
	// The drives' letters, "-" for an empty one; SET-UP's Drives panel
	// shows what is mounted.
	size_t n = strlen(text);
	for (unsigned d = 0; d < DRIVES && n + 2 < sizeof text; ++d) {
		letter_col[d] = (unsigned)n + 1;
		text[n++] = ' ';
		text[n++] = drives_name(d) ? (char)('A' + d) : '-';
		text[n] = '\0';
	}
	char usb[12];
	usb_label(usb, sizeof usb);
	unsigned usb_col = 0;
	if (usb[0] && n + 4 + strlen(usb) < sizeof text) {
		usb_col = (unsigned)n + 3;
		snprintf(text + n, sizeof text - n, " | %s", usb);
		n = strlen(text);
	}
	// The printer (bd wt6.6): PRN, lit while a job runs, PRN! after one failed.
	printer_status_t prn = printer_status();
	unsigned prn_col = 0;
	if (prn != PRINTER_STATUS_OFF && n + 7 < sizeof text) {
		prn_col = (unsigned)n + 3;
		snprintf(text + n, sizeof text - n, " | PRN%s", prn == PRINTER_STATUS_FAILED ? "!" : "");
		n = strlen(text);
	}
	if (cpm.running) {
		snprintf(text + n, sizeof text - n, " | %lu.%02lu MHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	} else {
		snprintf(text + n, sizeof text - n, " | monitor");
	}
	n = strlen(text);
	// The WiFi modem, then the flow control on its line: "wifi RTS/CTS".
	const char *wifi = wifi_label((uint8_t)modem_glue_status());
	unsigned wifi_col = 0;
	if (wifi && n + 3 + strlen(wifi) < sizeof text) {
		wifi_col = (unsigned)n + 3;
		snprintf(text + n, sizeof text - n, " | %s", wifi);
		n = strlen(text);
	}
	flow_light.col = 0;
	n = put_flow_field(text, sizeof text, n, wifi_col ? " " : " | "); // the Z80's SIO2 and the printer use the line
	snprintf(text + n, sizeof text - n, "%s | %s", usbkbd_count() ? " | kbd" : "", TERM_BUILD_ID);
	put_status_text(text);
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (cpm.lit & 1u << d && drives_name(d)) {
			screen_put(&screen, letter_col[d], STATUS_ROW, (uint8_t)('A' + d), colors.status_bg,
				colors.status_fg);
		}
	}
	if (usb_col && cpm.lit & 1u << DRIVES) {
		for (unsigned i = 0; i < 3; ++i) {
			screen_put(&screen, usb_col + i, STATUS_ROW, (uint8_t)usb[i], colors.status_bg, colors.status_fg);
		}
	}
	if (prn_col && prn == PRINTER_STATUS_BUSY) {
		for (unsigned i = 0; i < 3; ++i) {
			screen_put(&screen, prn_col + i, STATUS_ROW, (uint8_t)"PRN"[i], colors.status_bg, colors.status_fg);
		}
	}
	if (wifi && modem_glue_carrier()) {
		light_label(wifi_col, wifi, text);
	}
}

// The terminal type now, for the status line.
static const char *term_type_name(void) {
	return term.personality == TERM_ADM3A ? "ADM-3A" : term.vt100.vt52 ? "VT52" : "VT100";
}

status_t status_now(void) {
	status_t st = {
		.keyboards = usbkbd_count(),
		.settings = settings,
		.baud = serial_baud(),
		.type = term_type_name(),
		.telnet = (uint8_t)telnet_glue_status(),
		.wifi = (uint8_t)modem_glue_status(),
		.carrier = modem_glue_carrier(),
	};
	usb_label(st.usb, sizeof st.usb);
	const char *modem = modem_glue_notice();
	snprintf(st.modem, sizeof st.modem, "%s", modem ? modem : "");
	st.echo = settings.local_echo && !telnet_glue_connected();
	return st;
}

bool status_equal(const status_t *a, const status_t *b) {
	return a->keyboards == b->keyboards &&
		memcmp(&a->settings, &b->settings, sizeof a->settings) == 0 && a->baud == b->baud &&
		strcmp(a->usb, b->usb) == 0 &&
		a->type == b->type && a->telnet == b->telnet &&
		strcmp(a->modem, b->modem) == 0 && a->echo == b->echo && a->wifi == b->wifi && a->carrier == b->carrier;
}

void draw_status(const status_t *st) {
	if (setup.demo || !status_shown()) {
		return; // the demo has the status line; DECCOLM after it redraws
	}
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
	unsigned data_bits, stop_bits;
	char parity;
	settings_framing(&st->settings, &data_bits, &parity, &stop_bits);
	char text[SCREEN_MAX_COLS + 1];
	// The WiFi modem starting (bd xir.17.2) first: the terminal is not hung.
	bool modem = st->modem[0] != '\0';
	int n0 = snprintf(text, sizeof text, "%s%s%s %s %ux24 | %s | ", modem ? " " : "", st->modem, modem ? " |" : "",
		st->type, screen.cols, TERM_BUILD_ID);
	// The WiFi modem's line is fixed (921600 8N1): its state in their place.
	const char *wifi = wifi_label(st->wifi);
	unsigned wifi_col = 0;
	if (n0 > 0 && (size_t)n0 < sizeof text) {
		if (wifi) {
			wifi_col = (unsigned)n0;
			snprintf(text + n0, sizeof text - (size_t)n0, "%s", wifi);
		} else {
			snprintf(text + n0, sizeof text - (size_t)n0, "%u %u%c%u", st->baud, data_bits, parity, stop_bits);
		}
	}
	flow_light.col = 0;
	size_t n = strlen(text);
	const char *label = flow_label();
	if (label[0] && n + 1 + strlen(label) < sizeof text) { // after the framing, as before: "8N1 RTS/CTS"
		flow_light.col = (unsigned)n + 1;
		snprintf(text + n, sizeof text - n, " %s", label);
		n = strlen(text);
	}
	// Telnet (bd xir.10): "telnet" while on or auto, lit once the server
	// negotiated; "hangup" during a hang-up's +++ and ATH.
	unsigned telnet_col = 0;
	if (st->telnet != TELNET_STATUS_OFF && n + 9 < sizeof text) {
		telnet_col = (unsigned)n + 3;
		snprintf(text + n, sizeof text - n, " | %s", st->telnet == TELNET_STATUS_HANGUP ? "hangup" : "telnet");
		n = strlen(text);
	}
	snprintf(text + n, sizeof text - n, "%s%s%s%s", st->keyboards ? " | kbd" : "", st->usb[0] ? " | " : "",
		st->usb, st->echo ? " | echo" : "");
	put_status_text(text);
	if (st->telnet == TELNET_STATUS_ACTIVE) {
		light_label(telnet_col, "telnet", text);
	}
	if (wifi && st->carrier) {
		light_label(wifi_col, wifi, text);
	}
}
