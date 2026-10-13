// The status line, row 24 (terminal.h): the terminal's (type, width,
// line settings, keyboard, stick, echo, last key), CP/M mode's (drive
// letters lit while accessed, the stick, the Z80's speed) or the debug
// choices' (timing, the USB host's events); blank when hidden.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "keyname.h"
#include "palette.h"
#include "term_build.h"
#include "terminal.h"
#include "usbkbd.h"
#include "usbmsc.h"
#include "video.h"

#define DRIVE_LIGHT_MS 150 // a drive letter stays lit this long after an access

// With the Status line setting hidden, row 24 is left blank (the text area
// stays 24 rows) and the status line drawers stop here.
bool status_shown(void) {
	if (settings.status_line)
		return true;
	screen_clear_line(&screen, STATUS_ROW, PALETTE_TEXT, PALETTE_BACKGROUND);
	return false;
}

// The USB stick on the status line: "USB" while one is volume 1 (USB: to
// XLS and XPIP), "+n" for others waiting to take over, "?" if one could
// not be used; empty if none is in.
static void usb_label(char *out, size_t size) {
	usbmsc_status_t u = usbmsc_status();
	out[0] = '\0';
	if (!u.mounted && !u.waiting && !u.unusable)
		return;
	int n = snprintf(out, size, "%s", u.mounted ? "USB" : "usb");
	if (u.waiting && n > 0 && (size_t)n < size)
		n += snprintf(out + n, size - (size_t)n, "+%u", u.waiting);
	if (u.unusable && n > 0 && (size_t)n < size)
		snprintf(out + n, size - (size_t)n, "?");
}

// The Status line setting's debug choice: core 1's timing and core 0's
// main-loop time, refreshed every STATUS_INTERVAL_MS in both modes.
static struct {
	char text[SCREEN_MAX_COLS + 1]; // as last drawn: redrawn as is between refreshes
	uint32_t last_ms, frames, late;  // at the last refresh
	uint32_t line_peak;              // compose cycles, since the debug line came on
	uint32_t render_peak_us;         // likewise
	uint32_t pass_start_us, pass_max_us, pass_peak_us; // main-loop passes
} debug_status;

bool debug_status_on(void) {
	return settings.status_line == SETTINGS_STATUS_DEBUG || settings.status_line == SETTINGS_STATUS_DEBUG_USB;
}

// The main loop starts.
void debug_status_start(void) {
	debug_status.pass_start_us = time_us_32();
}

// One main-loop pass ends and the next starts.
void debug_status_pass(void) {
	uint32_t now = time_us_32(), us = now - debug_status.pass_start_us;
	debug_status.pass_start_us = now;
	if (us > debug_status.pass_max_us)
		debug_status.pass_max_us = us;
}

static unsigned percent(uint32_t part, uint32_t whole) {
	return whole ? (unsigned)((part * 100 + whole / 2) / whole) : 0;
}

// " late T+N | line C/P% | F fps | loop M/Q ms | render R/S us | Z MHz":
// late lines in all (+ since the last refresh); the longest scanline
// compose as a share of a line's time, this refresh / the peak; frames a
// second; the longest main-loop pass and the longest line render on core 0
// (core 1 can show that line half drawn meanwhile), now / peak; in CP/M
// mode the Z80's speed.
void debug_status_refresh(void) {
	uint32_t now = to_ms_since_boot(get_absolute_time());
	video_stats_t st = video_stats();
	uint32_t ms = now - debug_status.last_ms;
	if (ms > 4 * STATUS_INTERVAL_MS) {
		// Just turned on (or the first time): the peaks start again.
		debug_status.line_peak = debug_status.pass_peak_us = debug_status.render_peak_us = 0;
		ms = 0;
	}
	if (st.max_line_cycles > debug_status.line_peak)
		debug_status.line_peak = st.max_line_cycles;
	uint32_t pass_us = debug_status.pass_max_us;
	debug_status.pass_max_us = 0;
	if (pass_us > debug_status.pass_peak_us)
		debug_status.pass_peak_us = pass_us;
	if (st.max_render_us > debug_status.render_peak_us)
		debug_status.render_peak_us = st.max_render_us;
	unsigned fps = ms ? (unsigned)(((st.frames - debug_status.frames) * 1000 + ms / 2) / ms) : 0;
	char *t = debug_status.text;
	size_t size = sizeof debug_status.text;
	int n = snprintf(t, size, " late %lu+%lu | line %u/%u%% | %u fps | loop %lu/%lu ms | render %lu/%lu us",
		(unsigned long)st.late, (unsigned long)(ms ? st.late - debug_status.late : 0),
		percent(st.max_line_cycles, st.cycles_budget), percent(debug_status.line_peak, st.cycles_budget), fps,
		(unsigned long)((pass_us + 500) / 1000), (unsigned long)((debug_status.pass_peak_us + 500) / 1000),
		(unsigned long)st.max_render_us, (unsigned long)debug_status.render_peak_us);
	if (cpm.on && cpm.running && n > 0 && (size_t)n < size)
		snprintf(t + n, size - (size_t)n, " | %lu.%02lu MHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	debug_status.last_ms = now;
	debug_status.frames = st.frames;
	debug_status.late = st.late;
	// debug usb: the USB host's last events instead, after the time, which
	// stops if the main loop (keyboard, USB, CP/M) does.
	if (settings.status_line == SETTINGS_STATUS_DEBUG_USB) {
		n = snprintf(t, size, " %lus | usb:", (unsigned long)(now / 1000));
		if (n > 0 && (size_t)n < size)
			usbkbd_log_text(t + n, size - (size_t)n);
	}
}

static void draw_debug_status(void) {
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, debug_status.text, colours.status_fg, colours.status_bg);
}

// Drives accessed light up on the status line, like a drive's busy light;
// so does USB (bit 8) while the stick is read or written.
uint16_t drive_lights(uint32_t now) {
	uint8_t accessed = drives_activity();
	uint16_t lit = 0;
	for (unsigned d = 0; d < DRIVES; ++d) {
		if (accessed & 1u << d)
			cpm.light_until[d] = now + DRIVE_LIGHT_MS;
		if ((int32_t)(cpm.light_until[d] - now) > 0)
			lit |= (uint16_t)(1u << d);
	}
	if (usbmsc_activity())
		cpm.usb_light_until = now + DRIVE_LIGHT_MS;
	if ((int32_t)(cpm.usb_light_until - now) > 0)
		lit |= 1u << DRIVES;
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
	if (setup.demo || !status_shown())
		return; // the demo has the status line
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
	char text[SCREEN_MAX_COLS + 1] = " CP/M |";
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
	if (cpm.running)
		snprintf(text + n, sizeof text - n, " | %lu.%02lu MHz", (unsigned long)(cpm.khz / 1000),
			(unsigned long)(cpm.khz % 1000 / 10));
	else
		snprintf(text + n, sizeof text - n, " | monitor");
	n = strlen(text);
	snprintf(text + n, sizeof text - n, "%s | %s", usbkbd_count() ? " | kbd" : "", TERM_BUILD_ID);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colours.status_fg, colours.status_bg);
	for (unsigned d = 0; d < DRIVES; ++d)
		if (cpm.lit & 1u << d && drives_name(d))
			screen_put(&screen, letter_col[d], STATUS_ROW, (uint8_t)('A' + d), colours.status_bg,
				colours.status_fg);
	if (usb_col && cpm.lit & 1u << DRIVES)
		for (unsigned i = 0; i < 3; ++i)
			screen_put(&screen, usb_col + i, STATUS_ROW, (uint8_t)usb[i], colours.status_bg, colours.status_fg);
}

// The terminal type now, for the status line.
static const char *term_type_name(void) {
	return term.personality == TERM_ADM3A ? "ADM-3A" : term.vt100.vt52 ? "VT52" : "VT100";
}

status_t status_now(void) {
	status_t st = {usbkbd_count(), last_key.count, settings, "", term_type_name()};
	usb_label(st.usb, sizeof st.usb);
	return st;
}

bool status_equal(const status_t *a, const status_t *b) {
	return a->keyboards == b->keyboards && a->keys_sent == b->keys_sent &&
		memcmp(&a->settings, &b->settings, sizeof a->settings) == 0 && strcmp(a->usb, b->usb) == 0 &&
		a->type == b->type;
}

void draw_status(const status_t *st) {
	if (setup.demo || !status_shown())
		return; // the demo has the status line; DECCOLM after it redraws
	if (debug_status_on()) {
		draw_debug_status();
		return;
	}
	char key[32] = "";
	if (st->keys_sent) {
		char name[24];
		keyname(last_key.data, last_key.len, name, sizeof name);
		snprintf(key, sizeof key, " | key %s", name);
	}
	unsigned data_bits, stop_bits;
	char parity;
	settings_framing(&st->settings, &data_bits, &parity, &stop_bits);
	char text[SCREEN_MAX_COLS + 1];
	snprintf(text, sizeof text, " %s %ux24 | %s | %u %u%c%u%s%s%s%s%s%s",
		st->type, screen.cols, TERM_BUILD_ID, settings_baud_rate(&st->settings), data_bits, parity, stop_bits,
		SERIAL_FLOW_CONTROL ? " RTS/CTS" : "", st->keyboards ? " | kbd" : "", st->usb[0] ? " | " : "", st->usb,
		st->settings.local_echo ? " | echo" : "", key);
	screen_clear_line(&screen, STATUS_ROW, colours.status_fg, colours.status_bg);
	screen_puts(&screen, 0, STATUS_ROW, text, colours.status_fg, colours.status_bg);
}
