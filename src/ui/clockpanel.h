#ifndef CLOCKPANEL_H
#define CLOCKPANEL_H

// SET-UP's Clock and sound panel (C, bd 1ls.6.4): the date and time, how
// the status line shows them (bd 1ls.6.5), and the sound: the Bell's and
// the AY's levels, the AY's stereo layout (bd 1ls.18.9) and whether the
// board's DAC is fitted (bd 1ls.18.5).
// Portable C, host-tested; the terminal (apps/terminal/setup_glue.c) opens
// it over SET-UP's rows.
//
// Up/Down select a row. Date and Time follow the clock until edited:
// digits overwrite the one under the cursor and move on, Left/Right and
// Backspace move it; Return then sets the clock to them (seconds as typed).
// The settings' rows (settings.h's SETTINGS_PAGE_CLOCK fields) step
// through their choices with Left/Right or Space; Return keeps them in
// SET-UP's edited settings, which SET-UP's Return saves. Escape goes back
// without changes. Without a DAC (has_dac false, or Sound DAC not fitted)
// the sound rows show off, fixed, and are skipped.

#include <stdbool.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"

// In a box: the title line, Date, Time, a gap, the three clock settings,
// what the status line shows, a gap, the four sound settings, message, 2
// help lines, the bottom line.
#define CLOCKPANEL_ROWS 17
#define CLOCKPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	CLOCKPANEL_NONE,
	CLOCKPANEL_REDRAW,
	CLOCKPANEL_APPLY,  // Return: clockpanel_time, the settings into SET-UP's, then close
	CLOCKPANEL_CANCEL, // Escape
} clockpanel_action_t;

#define CLOCKPANEL_DATE 0
#define CLOCKPANEL_TIME 1
#define CLOCKPANEL_BELL 5 // the first sound line
#define CLOCKPANEL_LINES 9 // Date, Time, the clock settings', then the sound settings' rows

typedef struct {
	settings_t values;    // SET-UP's edited settings, the clock's changed here
	char date[11];        // "YYYY-MM-DD"
	char time[9];         // "HH:MM:SS"
	bool edited;          // typed in: no longer follows the clock
	uint32_t shown_secs;  // the time Date and Time show when not edited
	unsigned line;        // CLOCKPANEL_DATE, _TIME, then 2.. the settings
	unsigned cursor;      // in the Date or Time text
	bool has_rtc;         // the board keeps the time over power-off
	bool has_dac;         // the board has a DAC (BOARD_AUDIO_I2S)
	uint8_t esc;          // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;
	const char *message;
} clockpanel_t;

// With SET-UP's edited settings and the clock now.
void clockpanel_open(clockpanel_t *p, const settings_t *values, uint32_t now_secs, bool has_rtc, bool has_dac);

clockpanel_action_t clockpanel_input(clockpanel_t *p, uint8_t c, uint32_t now_ms);
// Call regularly: a lone ESC becomes CLOCKPANEL_CANCEL after the timeout,
// and Date and Time follow the clock (CLOCKPANEL_REDRAW when they change).
clockpanel_action_t clockpanel_tick(clockpanel_t *p, uint32_t now_ms, uint32_t now_secs);

// After CLOCKPANEL_APPLY: true, with *secs, if the clock is to be set (the
// date or time was typed in).
bool clockpanel_time(const clockpanel_t *p, uint32_t *secs);

// Draw it like SET-UP (setup_draw): a box in border, text in fg on bg.
void clockpanel_draw(const clockpanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg, uint8_t border);

#endif
