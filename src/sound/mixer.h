#ifndef MIXER_H
#define MIXER_H

// The audio engine's mix (bd 1ls.18.2): the bell (a 1 kHz square wave,
// bd 1ls.7) and the AY-3-8910 when one is attached, into the words the
// I2S PIO program shifts out (left in the high half). Portable; the
// RP2350's engine (src/platform/audio.c) calls mixer_fill from its DMA
// refill interrupt on core 0. The setters are plain stores, safe from the
// main loop under that interrupt.

#include <stdbool.h>
#include <stdint.h>

#include "ay8910.h"

#define MIXER_RATE 32000       // frames a second
#define MIXER_BELL_PERIOD 32   // frames: 1 kHz
#define MIXER_BELL_FRAMES 3200 // 100 ms, whole periods
#define MIXER_MAX_FRAMES 256   // the most one mixer_fill takes

typedef struct {
	ay8910_t *ay;      // NULL: none
	uint16_t ay_gain;  // Q15 on the chip's output (full: three channels at level 15)
	int16_t bell_amp;  // the square wave's amplitude; 0: the bell is off
	uint32_t bell_left; // frames still to sound
	uint32_t bell_phase;
	int16_t ay_out[2 * MIXER_MAX_FRAMES];
} mixer_t;

void mixer_init(mixer_t *m);
// A beep, unless one is sounding or the bell is off (amplitude 0).
void mixer_ring(mixer_t *m);
// frames (at most MIXER_MAX_FRAMES) words into out.
void mixer_fill(mixer_t *m, uint32_t *out, unsigned frames);

#endif
