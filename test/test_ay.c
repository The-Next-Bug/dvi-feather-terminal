#include <stdlib.h>
#include <string.h>

#include "ay8910.h"
#include "test.h"

#define RATE 32000
#define FULL 10922 // one channel at level 15 (ay8910.c's CHANNEL_FULL)

static ay8910_t ay;
static int16_t buf[2 * RATE];

static void start(uint32_t clock, bool dc_block) {
	ay_init(&ay, clock, RATE);
	ay.dc_block = dc_block;
}

static void render(unsigned frames) {
	ay_render(&ay, buf, frames);
}

// Rising zero crossings on the left over frames frames.
static unsigned crossings(unsigned frames) {
	unsigned n = 0;
	for (unsigned f = 1; f < frames; ++f) {
		n += buf[2 * (f - 1)] < 0 && buf[2 * f] >= 0;
	}
	return n;
}

static void test_reset_silent(void) {
	start(AY_CLOCK_RC2014, true);
	render(RATE / 10);
	for (unsigned i = 0; i < 2 * RATE / 10; ++i) {
		CHECK_EQ(buf[i], 0);
	}
	// Volumes 0 with everything on is silence too.
	ay_write(&ay, AY_TONE_A, 100);
	render(RATE / 10);
	CHECK_EQ(buf[2 * (RATE / 10) - 1], 0);
}

// Each register keeps only its bits; there are 16.
static void test_registers(void) {
	static const uint8_t kept[AY_REGS] = {
		0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff,
	};
	start(AY_CLOCK_RC2014, true);
	for (unsigned r = 0; r < AY_REGS; ++r) {
		CHECK_EQ(ay_read(&ay, (uint8_t)r), 0);
		ay_write(&ay, (uint8_t)r, 0xff);
		CHECK_EQ(ay_read(&ay, (uint8_t)r), kept[r]);
	}
	ay_write(&ay, 16, 0x12);
	CHECK_EQ(ay_read(&ay, 16), 0xff);
	ay_reset(&ay);
	CHECK_EQ(ay_read(&ay, AY_MIXER), 0);
}

// f / (16 TP) on each channel, the others silent.
static void test_tone_frequency(void) {
	static const unsigned periods[] = {262, 100, 1000, 0xfff};
	for (unsigned ch = 0; ch < 3; ++ch) {
		for (unsigned i = 0; i < sizeof periods / sizeof periods[0]; ++i) {
			start(AY_CLOCK_RC2014, true);
			ay_write(&ay, (uint8_t)(AY_TONE_A + 2 * ch), (uint8_t)periods[i]);
			ay_write(&ay, (uint8_t)(AY_TONE_A + 2 * ch + 1), (uint8_t)(periods[i] >> 8));
			ay_write(&ay, AY_MIXER, (uint8_t)(0x3f & ~(1u << ch)));
			ay_write(&ay, (uint8_t)(AY_AMPLITUDE_A + ch), 15);
			render(RATE);
			unsigned want = AY_CLOCK_RC2014 / (16 * periods[i]);
			unsigned got = crossings(RATE);
			CHECK(got + 2 >= want && got <= want + 2);
			if (!(got + 2 >= want && got <= want + 2)) {
				fprintf(stderr, "  channel %u period %u: %u Hz, want %u\n", ch, periods[i], got, want);
			}
		}
	}
}

static void test_period_zero(void) {
	static int16_t one[2 * 1000];
	for (unsigned p = 0; p < 2; ++p) {
		start(AY_CLOCK_RC2014, false);
		ay_write(&ay, AY_TONE_A, (uint8_t)p);
		ay_write(&ay, AY_MIXER, 0x3e);
		ay_write(&ay, AY_AMPLITUDE_A, 15);
		render(1000);
		if (p == 0) {
			memcpy(one, buf, sizeof one);
		}
	}
	CHECK(memcmp(one, buf, sizeof one) == 0);
}

// Everything off in the mixer: the channel holds its level (raw).
static int16_t level_of(unsigned v) {
	start(AY_CLOCK_RC2014, false);
	ay_write(&ay, AY_MIXER, 0x3f);
	ay_write(&ay, AY_AMPLITUDE_A, (uint8_t)v);
	render(4);
	return buf[6];
}

static int16_t levels[16];

static void test_levels(void) {
	for (unsigned v = 0; v < 16; ++v) {
		levels[v] = level_of(v);
		if (v > 0) {
			CHECK(levels[v] > levels[v - 1]);
		}
	}
	CHECK_EQ(levels[0], 0);
	CHECK_EQ(levels[15], FULL);
	// Logarithmic: level 13 is about half of 15 (Ayumi's 0.635).
	CHECK(levels[13] > FULL / 2 && levels[13] < FULL * 2 / 3);
	// DC blocked, the held level dies away.
	start(AY_CLOCK_RC2014, true);
	ay_write(&ay, AY_MIXER, 0x3f);
	ay_write(&ay, AY_AMPLITUDE_A, 15);
	render(RATE);
	CHECK(abs(buf[2 * RATE - 2]) < 50);
}

static unsigned volume_of(int16_t sample) {
	for (unsigned v = 0; v < 16; ++v) {
		if (levels[v] == sample) {
			return v;
		}
	}
	return 99;
}

// The datasheet's 16 shapes, step k after the write.
static unsigned shape_step(unsigned shape, unsigned k) {
	bool attack = shape & 4, cont = shape & 8, alt = shape & 2, hold = shape & 1;
	unsigned pos = k % 16;
	if (k < 16) {
		return attack ? pos : 15 - pos;
	}
	if (!cont) {
		return 0;
	}
	if (hold) {
		unsigned end = attack ? 15 : 0;
		return alt ? 15 - end : end;
	}
	bool up = attack ^ (alt && (k / 16) % 2);
	return up ? pos : 15 - pos;
}

// A tick a frame and EP 1 (a step every second tick): frame 2k holds step k.
static void test_envelope_shapes(void) {
	for (unsigned shape = 0; shape < 16; ++shape) {
		start(8 * RATE, false);
		ay_write(&ay, AY_MIXER, 0x3f);
		ay_write(&ay, AY_AMPLITUDE_A, AY_AMPLITUDE_ENVELOPE);
		ay_write(&ay, AY_ENVELOPE_FINE, 1);
		ay_write(&ay, AY_ENVELOPE_SHAPE, (uint8_t)shape);
		render(2 * 64);
		unsigned bad = 0;
		for (unsigned k = 0; k < 64; ++k) {
			bad += volume_of(buf[4 * k]) != shape_step(shape, k);
		}
		CHECK_EQ(bad, 0);
		if (bad) {
			fprintf(stderr, "  shape %u:", shape);
			for (unsigned k = 0; k < 40; ++k) {
				fprintf(stderr, " %u", volume_of(buf[4 * k]));
			}
			fprintf(stderr, "\n");
		}
	}
	// A write restarts it, even with the same shape.
	render(64);
	ay_write(&ay, AY_ENVELOPE_SHAPE, 15);
	render(2);
	CHECK_EQ(volume_of(buf[0]), 0);
	CHECK_EQ(volume_of(buf[2]), 1);
}

// A sawtooth (shape 8) repeats at f / (256 EP).
static void test_envelope_rate(void) {
	start(AY_CLOCK_RC2014, true);
	ay_write(&ay, AY_MIXER, 0x3f);
	ay_write(&ay, AY_AMPLITUDE_A, AY_AMPLITUDE_ENVELOPE);
	ay_write(&ay, AY_ENVELOPE_FINE, 100);
	ay_write(&ay, AY_ENVELOPE_SHAPE, 8);
	render(RATE);
	unsigned want = AY_CLOCK_RC2014 / (256 * 100), got = crossings(RATE);
	CHECK(got + 2 >= want && got <= want + 2);
}

// Noise alone on A: half the time high; a longer period, fewer rises (a
// frame part way through a shift averages, so rises past half level).
static unsigned noise_changes(uint8_t period, unsigned *high) {
	start(AY_CLOCK_RC2014, false);
	ay_write(&ay, AY_NOISE_PERIOD, period);
	ay_write(&ay, AY_MIXER, 0x37);
	ay_write(&ay, AY_AMPLITUDE_A, 15);
	render(RATE);
	unsigned rises = 0;
	*high = 0;
	for (unsigned f = 0; f < RATE; ++f) {
		*high += buf[2 * f] > FULL / 2;
		rises += f && buf[2 * f - 2] <= FULL / 2 && buf[2 * f] > FULL / 2;
	}
	return rises;
}

static void test_noise(void) {
	unsigned high, slow = noise_changes(31, &high);
	CHECK(high > RATE * 2 / 5 && high < RATE * 3 / 5);
	// f / (16 x 31) = 3716 shifts a second, the bit going up on a quarter.
	CHECK(slow > 700 && slow < 1200);
	unsigned fast = noise_changes(1, &high);
	CHECK(fast > 3 * slow);
	CHECK(high > RATE / 5 && high < RATE * 4 / 5);
}

// ABC: A left, B half in each, C right; ACB swaps B and C.
static void test_stereo(void) {
	static const struct {
		uint8_t stereo, ch;
		int16_t left, right;
	} cases[] = {
		{AY_STEREO_ABC, 0, FULL, 0}, {AY_STEREO_ABC, 1, FULL / 2, FULL / 2}, {AY_STEREO_ABC, 2, 0, FULL},
		{AY_STEREO_ACB, 1, 0, FULL}, {AY_STEREO_ACB, 2, FULL / 2, FULL / 2}, {AY_STEREO_MONO, 2, FULL, FULL},
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		start(AY_CLOCK_RC2014, false);
		ay.stereo = cases[i].stereo;
		ay_write(&ay, AY_MIXER, 0x3f);
		ay_write(&ay, (uint8_t)(AY_AMPLITUDE_A + cases[i].ch), 15);
		render(2);
		CHECK_EQ(buf[2], cases[i].left);
		CHECK_EQ(buf[3], cases[i].right);
	}
	// All three at full in mono stay in range.
	start(AY_CLOCK_RC2014, false);
	ay_write(&ay, AY_MIXER, 0x3f);
	for (unsigned ch = 0; ch < 3; ++ch) {
		ay_write(&ay, (uint8_t)(AY_AMPLITUDE_A + ch), 15);
	}
	render(2);
	CHECK_EQ(buf[2], 3 * FULL);
}

// A tone far above the output rate averages to half its level.
static void test_ultrasonic(void) {
	start(AY_CLOCK_RC2014, false);
	ay_write(&ay, AY_TONE_A, 1);
	ay_write(&ay, AY_MIXER, 0x3e);
	ay_write(&ay, AY_AMPLITUDE_A, 15);
	render(1000);
	for (unsigned f = 10; f < 1000; ++f) {
		CHECK(abs(buf[2 * f] - FULL / 2) <= FULL / 6);
	}
}

int main(void) {
	RUN(test_reset_silent);
	RUN(test_registers);
	RUN(test_tone_frequency);
	RUN(test_period_zero);
	RUN(test_levels);
	RUN(test_envelope_shapes);
	RUN(test_envelope_rate);
	RUN(test_noise);
	RUN(test_stereo);
	RUN(test_ultrasonic);
	return tests_done();
}
