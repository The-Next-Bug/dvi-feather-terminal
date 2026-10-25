#ifndef BELL_H
#define BELL_H

// The bell (bd 1ls.7): a short beep on the board's I2S DAC
// (BOARD_AUDIO_I2S_*), one of the audio engine's sources (audio.h, audio.c:
// the PIO state machine keeps the DAC's clocks running, silence between
// sounds). Without a DAC there is none (bell_ring does nothing). Core 0.

#include <stdbool.h>

#include <stdint.h>

// After video_init: the sample rate comes from clk_sys. Starts the whole
// audio engine.
void bell_init(void);
// The loudness: a level, 0 (silent) to SETTINGS_BELL_MAX (settings.h);
// for now the AY's too.
void bell_set_level(uint8_t level);
// A beep, unless one is still sounding or the bell is off.
void bell_ring(void);
// The board has a DAC and the bell is set up.
bool bell_present(void);

#endif
