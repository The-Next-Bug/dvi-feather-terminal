#include "ay8910.h"

#include <string.h>

// One channel at full level: three in mono reach 32766.
#define CHANNEL_FULL 10922
// The DC blocker's pole, Q15: a high-pass around 25 Hz at 32 kHz.
#define DC_POLE 32604

// What each register keeps.
static uint8_t masks[AY_REGS] = {
	0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff,
};

// The AY's 16 output levels, measured (Ayumi's AY table), x CHANNEL_FULL.
// Not const: the refill interrupt reads it, and nothing of its should be
// in flash.
static int16_t dac[16] = {
	0, 109, 158, 230, 335, 497, 704, 1173, 1383, 2239, 3192, 4072, 5379, 6939, 8799, CHANNEL_FULL,
};

// Left and right weights per channel, Q8, per AY_STEREO_*.
static uint16_t pans[3][2][3] = {
	{{256, 256, 256}, {256, 256, 256}},
	{{256, 128, 0}, {0, 128, 256}},
	{{256, 0, 128}, {0, 256, 128}},
};

void ay_init(ay8910_t *ay, uint32_t clock_hz, uint32_t rate) {
	memset(ay, 0, sizeof *ay);
	if (rate == 0 || rate > clock_hz / 8) {
		rate = clock_hz / 8; // at least a tick a frame
	}
	ay->tick_step = (uint32_t)(((uint64_t)(clock_hz / 8) << 16) / rate);
	ay->stereo = AY_STEREO_MONO;
	ay->dc_block = true;
	ay_reset(ay);
}

// The envelope from the start, as the shape register says: CONT 0 holds
// at 0 after the first ramp, HOLD keeps the ramp's end (ALT: the other
// end), ALT alone turns each ramp around.
static void env_restart(ay8910_t *ay) {
	uint8_t shape = ay->regs[AY_ENVELOPE_SHAPE];
	ay->env_attack = shape & 0x04 ? 0x0f : 0;
	if (!(shape & 0x08)) {
		ay->env_hold = true;
		ay->env_alternate = ay->env_attack != 0;
	} else {
		ay->env_hold = shape & 0x01;
		ay->env_alternate = shape & 0x02;
	}
	ay->env_step = 0x0f;
	ay->env_holding = false;
	ay->env_count = 0;
	ay->env_half = 0;
}

void ay_reset(ay8910_t *ay) {
	memset(ay->regs, 0, sizeof ay->regs);
	ay->stereo_reg = 0;
	memset(ay->tone_count, 0, sizeof ay->tone_count);
	ay->tone_out = 0;
	ay->noise_count = 0;
	ay->noise_half = 0;
	ay->rng = 1;
	ay->tick_frac = 0;
	ay->env_seen = ay->env_writes;
	env_restart(ay);
	memset(ay->dc_x, 0, sizeof ay->dc_x);
	memset(ay->dc_y, 0, sizeof ay->dc_y);
}

void ay_write(ay8910_t *ay, uint8_t reg, uint8_t value) {
	if (reg == AY_STEREO_REG) {
		ay->stereo_reg = value & 3;
		return;
	}
	if (reg >= AY_REGS) {
		return;
	}
	ay->regs[reg] = value & masks[reg];
	if (reg == AY_ENVELOPE_SHAPE) {
		++ay->env_writes; // the renderer restarts it
	}
}

uint8_t ay_read(const ay8910_t *ay, uint8_t reg) {
	if (reg == AY_STEREO_REG) {
		return (uint8_t)(ay->stereo_reg | (ay_stereo(ay) + 1) << 4);
	}
	return reg < AY_REGS ? ay->regs[reg] : 0xff;
}

uint8_t ay_stereo(const ay8910_t *ay) {
	uint8_t s = ay->stereo_reg ? (uint8_t)(ay->stereo_reg - 1) : ay->stereo;
	return s <= AY_STEREO_ACB ? s : AY_STEREO_MONO;
}

static void env_tick(ay8910_t *ay, uint16_t period) {
	if (ay->env_holding || ++ay->env_count < period) {
		return;
	}
	ay->env_count = 0;
	if ((ay->env_half ^= 1) != 0) {
		return; // a step every second period count: f / (16 EP)
	}
	if (--ay->env_step >= 0) {
		return;
	}
	if (ay->env_hold) {
		if (ay->env_alternate) {
			ay->env_attack ^= 0x0f;
		}
		ay->env_holding = true;
		ay->env_step = 0;
	} else {
		if (ay->env_alternate) {
			ay->env_attack ^= 0x0f;
		}
		ay->env_step = 0x0f;
	}
}

static void noise_tick(ay8910_t *ay, uint8_t period) {
	if (++ay->noise_count < period) {
		return;
	}
	ay->noise_count = 0;
	if ((ay->noise_half ^= 1) != 0) {
		return;
	}
	// Taps 0 and 3, the new bit in at 16.
	ay->rng = (ay->rng >> 1) | (((ay->rng ^ (ay->rng >> 3)) & 1) << 16);
}

static int16_t dc_block(ay8910_t *ay, unsigned side, int32_t x) {
	int32_t y = x - ay->dc_x[side] + (int32_t)(((int64_t)ay->dc_y[side] * DC_POLE) >> 15);
	ay->dc_x[side] = x;
	ay->dc_y[side] = y;
	return (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
}

bool ay_idle(const ay8910_t *ay) {
	const uint8_t *a = ay->regs + AY_AMPLITUDE_A;
	for (unsigned side = 0; side < 2; ++side) {
		if (ay->dc_y[side] > 2 || ay->dc_y[side] < -2) {
			return false;
		}
	}
	return (a[0] | a[1] | a[2]) == 0;
}

void ay_render(ay8910_t *ay, int16_t *out, unsigned frames) {
	if (ay->env_seen != ay->env_writes) {
		ay->env_seen = ay->env_writes;
		env_restart(ay);
	}
	const uint8_t *r = ay->regs;
	uint16_t tone_period[3];
	for (unsigned ch = 0; ch < 3; ++ch) {
		tone_period[ch] = (uint16_t)(r[AY_TONE_A + 2 * ch] | r[AY_TONE_A + 2 * ch + 1] << 8);
		if (tone_period[ch] == 0) {
			tone_period[ch] = 1;
		}
	}
	uint8_t noise_period = r[AY_NOISE_PERIOD] ? r[AY_NOISE_PERIOD] : 1;
	uint16_t env_period = (uint16_t)(r[AY_ENVELOPE_FINE] | r[AY_ENVELOPE_COARSE] << 8);
	if (env_period == 0) {
		env_period = 1;
	}
	// A channel sounds while (its tone is high or off) and (the noise
	// is high or off for it).
	uint8_t tone_off = r[AY_MIXER] & 7, noise_off = (r[AY_MIXER] >> 3) & 7;
	const uint16_t(*pan)[3] = pans[ay_stereo(ay)];

	for (unsigned f = 0; f < frames; ++f) {
		ay->tick_frac += ay->tick_step;
		unsigned ticks = ay->tick_frac >> 16;
		ay->tick_frac &= 0xffff;
		int32_t sum[3] = {0, 0, 0};
		for (unsigned t = 0; t < ticks; ++t) {
			for (unsigned ch = 0; ch < 3; ++ch) {
				if (++ay->tone_count[ch] >= tone_period[ch]) {
					ay->tone_count[ch] = 0;
					ay->tone_out ^= (uint8_t)(1u << ch);
				}
			}
			noise_tick(ay, noise_period);
			env_tick(ay, env_period);
			uint8_t noise = (ay->rng & 1) ? 7 : 0;
			uint8_t on = (ay->tone_out | tone_off) & (noise | noise_off);
			int16_t env = dac[ay->env_step ^ ay->env_attack];
			for (unsigned ch = 0; ch < 3; ++ch) {
				if (on & (1u << ch)) {
					uint8_t a = r[AY_AMPLITUDE_A + ch];
					sum[ch] += a & AY_AMPLITUDE_ENVELOPE ? env : dac[a & 0x0f];
				}
			}
		}
		int32_t level[3];
		for (unsigned ch = 0; ch < 3; ++ch) {
			level[ch] = ticks ? sum[ch] / (int32_t)ticks : 0;
		}
		for (unsigned side = 0; side < 2; ++side) {
			int32_t x = (level[0] * pan[side][0] + level[1] * pan[side][1] + level[2] * pan[side][2]) >> 8;
			if (ay->dc_block) {
				out[2 * f + side] = dc_block(ay, side, x);
			} else {
				out[2 * f + side] = (int16_t)(x > 32767 ? 32767 : x);
			}
		}
	}
}
