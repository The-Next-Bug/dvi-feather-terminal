#include "mixer.h"

#include <string.h>

void mixer_init(mixer_t *m) {
	memset(m, 0, sizeof *m);
}

void mixer_ring(mixer_t *m) {
	if (m->bell_amp != 0 && m->bell_left == 0) {
		m->bell_phase = 0;
		m->bell_left = MIXER_BELL_FRAMES;
	}
}

static int32_t clamp16(int32_t v) {
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

void mixer_fill(mixer_t *m, uint32_t *out, unsigned frames) {
	if (frames > MIXER_MAX_FRAMES) {
		frames = MIXER_MAX_FRAMES;
	}
	ay8910_t *ay = m->ay;
	bool ay_on = ay && m->ay_gain && !ay_idle(ay);
	if (ay_on) {
		ay_render(ay, m->ay_out, frames);
	}
	int32_t gain = m->ay_gain;
	for (unsigned f = 0; f < frames; ++f) {
		int32_t left = 0, right = 0;
		if (ay_on) {
			left = (m->ay_out[2 * f] * gain) >> 15;
			right = (m->ay_out[2 * f + 1] * gain) >> 15;
		}
		if (m->bell_left) {
			int32_t b = m->bell_phase < MIXER_BELL_PERIOD / 2 ? m->bell_amp : -m->bell_amp;
			left += b;
			right += b;
			if (++m->bell_phase == MIXER_BELL_PERIOD) {
				m->bell_phase = 0;
			}
			--m->bell_left;
		}
		out[f] = (uint32_t)(uint16_t)clamp16(left) << 16 | (uint16_t)clamp16(right);
	}
}
