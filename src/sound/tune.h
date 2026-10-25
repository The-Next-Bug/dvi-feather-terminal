#ifndef TUNE_H
#define TUNE_H

// Tunes for the AY-3-8910 (bd 1ls.18): SET-UP's demo plays Korobeiniki
// (a 19th-century Russian folk song, public domain; the Tetris theme) in a
// loop while it shows, melody on A with a decaying envelope, an octave bass
// on B, a noise tick on C each beat; the Sound chip setting plays a chime
// (C E G rising to a C) when it changes. Portable; the caller polls it from
// its main loop and it writes the chip's registers through write.

#include <stdbool.h>
#include <stdint.h>

#define TUNE_EIGHTH_MS 200 // Korobeiniki's step: 150 beats a minute
#define TUNE_TICK_MS 40    // the noise tick's length

#define TUNE_KOROBEINIKI 0 // looped
#define TUNE_CHIME 1       // once, about 0.7 s

typedef struct {
	void (*write)(uint8_t reg, uint8_t value);
	uint8_t piece;    // TUNE_*
	uint16_t step_ms; // its step (an eighth)
	uint32_t start_ms;
	uint32_t eighths; // steps played
	unsigned melody, bass;            // the next note of each
	uint32_t melody_end, bass_end;    // in eighths: when the current note ends
	uint32_t length;                  // eighths in a round of the tune
	uint8_t tick_on;                  // the noise tick sounding
} tune_t;

// Piece (TUNE_*) from the start (the chip set up for it), at now_ms.
void tune_start(tune_t *t, uint8_t piece, void (*write)(uint8_t reg, uint8_t value), uint32_t now_ms);
// What is due by now_ms (call at least every TUNE_TICK_MS / 2); false once
// a piece that does not loop has ended (the chip then silent).
bool tune_poll(tune_t *t, uint32_t now_ms);
// The tone period (1.8432 MHz) of note n: octave * 12 + semitone, octave
// from 2 (24 = C2), as the tune's tables have them.
uint16_t tune_period(uint8_t n);

#endif
