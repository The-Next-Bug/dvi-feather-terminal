#ifndef AY8910_H
#define AY8910_H

// The General Instrument AY-3-8910 sound chip (bd 1ls.18.3): three square
// wave tone channels, a noise generator and an envelope, rendered as 16-bit
// stereo frames at the audio engine's rate. Portable; register writes come
// from the Z80 machine's ports (bd 1ls.18.4), rendering from the engine's
// refill interrupt (bd 1ls.18.2) on the same core: ay_write only stores
// bytes, so an interrupt between its stores is harmless, but the two must
// not run on two cores at once.
//
// Rates per the datasheet, f the chip's clock: tone f / (16 TP), noise
// shifts f / (16 NP), an envelope step f / (16 EP) (a whole ramp of 16
// steps f / (256 EP)); a period of 0 counts as 1. The generators run at
// f / 8 and each output frame is the average of the ticks it covers, so a
// tone above the output rate (or a channel switched off in the mixer, the
// chip's constant-level trick for sampled sound) comes out as its average
// level instead of aliasing.

#include <stdbool.h>
#include <stdint.h>

#define AY_REGS 16
#define AY_CLOCK_RC2014 1843200u // RC2014's 7.3728 MHz / 4

// Registers: tone periods (fine, coarse 4 bits) for A, B, C; noise period
// (5 bits); mixer (bits 0-2 tone off, 3-5 noise off, 6-7 the I/O ports'
// direction, stored only); amplitudes (4 bits, bit 4 the envelope's
// instead); envelope period (fine, coarse); envelope shape (4 bits: CONT
// ATT ALT HOLD, a write restarts it); I/O ports A and B (stored only).
#define AY_TONE_A 0
#define AY_NOISE_PERIOD 6
#define AY_MIXER 7
#define AY_AMPLITUDE_A 8
#define AY_ENVELOPE_FINE 11
#define AY_ENVELOPE_COARSE 12
#define AY_ENVELOPE_SHAPE 13
#define AY_AMPLITUDE_ENVELOPE 0x10

// Where the channels go: all in both, or A left, B both, C right (ABC),
// or A left, C both, B right (ACB).
#define AY_STEREO_MONO 0
#define AY_STEREO_ABC 1
#define AY_STEREO_ACB 2

typedef struct {
	uint8_t regs[AY_REGS];
	uint8_t env_writes; // bumped by each write to AY_ENVELOPE_SHAPE
	// The renderer's.
	uint8_t env_seen;
	uint8_t stereo;   // AY_STEREO_*
	bool dc_block;    // take out the chip's DC level (on; off for the tests)
	uint32_t tick_step; // generator ticks per output frame, 16.16
	uint32_t tick_frac;
	uint16_t tone_count[3];
	uint8_t tone_out; // bit n: channel n's square wave is high
	uint8_t noise_count;
	uint8_t noise_half; // the noise shifts every second period count
	uint32_t rng;       // 17-bit LFSR
	uint16_t env_count;
	uint8_t env_half;
	int8_t env_step;
	uint8_t env_attack; // 0 or 15: XORed into the step
	bool env_hold, env_alternate, env_holding;
	int32_t dc_x[2], dc_y[2];
} ay8910_t;

// A chip clocked at clock_hz (at most 8 x rate x 65535) rendering rate
// frames a second (at most clock_hz / 8), reset, mono, DC blocked.
void ay_init(ay8910_t *ay, uint32_t clock_hz, uint32_t rate);
// The chip's reset: every register 0 (silent), the generators restarted.
void ay_reset(ay8910_t *ay);
// Registers 16-255 are ignored (read 0xFF): the chip answers only 0-15.
void ay_write(ay8910_t *ay, uint8_t reg, uint8_t value);
uint8_t ay_read(const ay8910_t *ay, uint8_t reg);
// frames stereo frames (left, right) into out; three channels at full
// level in mono reach about 32767.
void ay_render(ay8910_t *ay, int16_t *out, unsigned frames);
// Silent and settled: the three amplitudes 0 and the DC blocker at rest,
// so the engine can skip ay_render (it carries on from there later).
bool ay_idle(const ay8910_t *ay);

#endif
