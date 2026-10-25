#ifndef AUDIO_H
#define AUDIO_H

// The audio engine (bd 1ls.18.2): a continuous stream to the board's I2S
// DAC (BOARD_AUDIO_I2S_*), two DMA channels taking turns over two blocks
// of MIXER_MAX_FRAMES frames (8 ms at 32 kHz), each refilled by mixer.c
// from DMA_IRQ_1 on core 0 while the other plays, so neither the main
// loop nor a Z80 slice can starve it. The bell (bell.h) and the
// AY-3-8910 (bd 1ls.18.4) are its sources. Set up by bell_init.

#include <stdbool.h>
#include <stdint.h>

// The AY on the Z80's ports: CP/M mode attaches it (rendered from then on;
// detached it costs nothing). No-op without a DAC.
void audio_attach_ay(bool on);
// Its registers, for the machine's sound_write/sound_read: without a DAC
// writes are dropped and reads give 0xFF (no chip).
void audio_ay_write(uint8_t reg, uint8_t value);
uint8_t audio_ay_read(uint8_t reg);

#endif
