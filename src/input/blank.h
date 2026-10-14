#ifndef BLANK_H
#define BLANK_H

// Screen blanking after a time without keys (the Blank after setting), so
// a still screen, the status line above all, does not burn in. Portable C
// (no pico-sdk), host-tested; main.c feeds it keys and the time and tells
// video.c (video_set_blank). Only keys count as activity: output from the
// host or the Z80 goes on while blanked, unseen, so a program that keeps
// redrawing (a clock) cannot keep the screen lit.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint32_t after_ms; // 0: never blank
	uint32_t last_ms;  // the last activity
	bool blanked;
} blank_t;

// Blank after_ms after the last activity (0: never), counting from now.
// Also used when the setting changes: the screen is shown again.
void blank_init(blank_t *b, uint32_t after_ms, uint32_t now_ms);

// A key at now_ms. Returns true if it only woke the screen: the caller
// drops it, so a key pressed to see the screen is not typed blind.
bool blank_activity(blank_t *b, uint32_t now_ms);

// Call regularly: returns whether the screen is blanked now.
bool blank_tick(blank_t *b, uint32_t now_ms);

#endif
