#include "tune.h"

#include <stddef.h>

#include "ay8910.h"

// Notes: octave * 12 + semitone (C = 0), 0 a rest; lengths in eighths.
#define N(name, octave) ((octave) * 12 + (name))
#define C 0
#define D 2
#define E 4
#define F 5
#define GS 8
#define G 7
#define A 9
#define B 11
#define REST 0

typedef struct {
	uint8_t note, eighths;
} note_t;

// Korobeiniki's first part: 8 bars, a round of 64 eighths.
static const note_t melody[] = {
	{N(E, 5), 2}, {N(B, 4), 1}, {N(C, 5), 1}, {N(D, 5), 2}, {N(C, 5), 1}, {N(B, 4), 1},
	{N(A, 4), 2}, {N(A, 4), 1}, {N(C, 5), 1}, {N(E, 5), 2}, {N(D, 5), 1}, {N(C, 5), 1},
	{N(B, 4), 3}, {N(C, 5), 1}, {N(D, 5), 2}, {N(E, 5), 2},
	{N(C, 5), 2}, {N(A, 4), 2}, {N(A, 4), 4},
	{REST, 1}, {N(D, 5), 2}, {N(F, 5), 1}, {N(A, 5), 2}, {N(G, 5), 1}, {N(F, 5), 1},
	{N(E, 5), 3}, {N(C, 5), 1}, {N(E, 5), 2}, {N(D, 5), 1}, {N(C, 5), 1},
	{N(B, 4), 2}, {N(B, 4), 1}, {N(C, 5), 1}, {N(D, 5), 2}, {N(E, 5), 2},
	{N(C, 5), 2}, {N(A, 4), 2}, {N(A, 4), 2}, {REST, 2},
};

// The bass: each bar's root in eighths, low and high octave in turn.
static const note_t bass[] = {
	{N(E, 2), 1}, {N(E, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1},
	{N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1},
	{N(GS, 2), 1}, {N(GS, 3), 1}, {N(GS, 2), 1}, {N(GS, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1},
	{N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1},
	{N(D, 2), 1}, {N(D, 3), 1}, {N(D, 2), 1}, {N(D, 3), 1}, {N(D, 2), 1}, {N(D, 3), 1}, {N(D, 2), 1}, {N(D, 3), 1},
	{N(C, 3), 1}, {N(C, 4), 1}, {N(C, 3), 1}, {N(C, 4), 1}, {N(C, 3), 1}, {N(C, 4), 1}, {N(C, 3), 1}, {N(C, 4), 1},
	{N(GS, 2), 1}, {N(GS, 3), 1}, {N(GS, 2), 1}, {N(GS, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1}, {N(E, 2), 1}, {N(E, 3), 1},
	{N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {N(A, 2), 1}, {N(A, 3), 1}, {REST, 2},
};

// The chime: C5 E5 G5 quickly, then C6 dying away.
static const note_t chime[] = {
	{N(C, 5), 1}, {N(E, 5), 1}, {N(G, 5), 1}, {N(C, 6), 6},
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
	const note_t *melody, *bass; // bass NULL: none
	uint8_t melody_n, bass_n;
	uint16_t step_ms;
	bool loop, ticks;
} piece_t;

static const piece_t pieces[] = {
	[TUNE_KOROBEINIKI] = {melody, bass, COUNT(melody), COUNT(bass), TUNE_EIGHTH_MS, true, true},
	[TUNE_CHIME] = {chime, NULL, COUNT(chime), 0, 80, false, false},
};

// Octave 2's periods, 115200 / f: each octave up halves them.
static const uint16_t octave2[12] = {1761, 1662, 1569, 1481, 1398, 1319, 1245, 1175, 1110, 1047, 988, 933};

uint16_t tune_period(uint8_t n) {
	unsigned octave = n / 12u;
	return octave < 2 ? octave2[n % 12u] : (uint16_t)(octave2[n % 12u] >> (octave - 2));
}

// A melody note's envelope: one decay (shape 0) over about 0.4 s.
#define ENVELOPE_PERIOD 2880 // 256 x 2880 / 1.8432 MHz = 0.4 s
#define BASS_LEVEL 9
#define TICK_LEVEL 5

static void set_tone(tune_t *t, unsigned ch, uint8_t note) {
	uint16_t p = tune_period(note);
	t->write((uint8_t)(AY_TONE_A + 2 * ch), (uint8_t)p);
	t->write((uint8_t)(AY_TONE_A + 2 * ch + 1), (uint8_t)(p >> 8));
}

static uint32_t total(const note_t *notes, unsigned n) {
	uint32_t sum = 0;
	for (unsigned i = 0; i < n; ++i) {
		sum += notes[i].eighths;
	}
	return sum;
}

static void silence(tune_t *t) {
	for (unsigned ch = 0; ch < 3; ++ch) {
		t->write((uint8_t)(AY_AMPLITUDE_A + ch), 0);
	}
}

void tune_start(tune_t *t, uint8_t piece, void (*write)(uint8_t reg, uint8_t value), uint32_t now_ms) {
	if (piece >= COUNT(pieces)) {
		piece = TUNE_CHIME;
	}
	const piece_t *p = &pieces[piece];
	*t = (tune_t){.write = write, .piece = piece, .step_ms = p->step_ms, .start_ms = now_ms,
		.length = total(p->melody, p->melody_n)};
	write(AY_MIXER, 0x1c); // tones A and B, noise C
	write(AY_NOISE_PERIOD, 4);
	write(AY_ENVELOPE_FINE, (uint8_t)ENVELOPE_PERIOD);
	write(AY_ENVELOPE_COARSE, (uint8_t)(ENVELOPE_PERIOD >> 8));
	silence(t);
}

// Step e (from the start, any round) is due: each voice's next note if
// its last one has ended.
static void step(tune_t *t, uint32_t e) {
	const piece_t *p = &pieces[t->piece];
	if (e % t->length == 0) { // a new round
		t->melody = t->bass = 0;
		t->melody_end = t->bass_end = e;
	}
	if (e == t->melody_end && t->melody < p->melody_n) {
		const note_t *n = &p->melody[t->melody++];
		t->melody_end = e + n->eighths;
		if (n->note == REST) {
			t->write(AY_AMPLITUDE_A, 0);
		} else {
			set_tone(t, 0, n->note);
			t->write(AY_AMPLITUDE_A, AY_AMPLITUDE_ENVELOPE);
			t->write(AY_ENVELOPE_SHAPE, 0); // restarts the decay
		}
	}
	if (p->bass && e == t->bass_end && t->bass < p->bass_n) {
		const note_t *n = &p->bass[t->bass++];
		t->bass_end = e + n->eighths;
		if (n->note == REST) {
			t->write(AY_AMPLITUDE_A + 1, 0);
		} else {
			set_tone(t, 1, n->note);
			t->write(AY_AMPLITUDE_A + 1, BASS_LEVEL);
		}
	}
	if (p->ticks && e % 2 == 0) { // a beat: the tick
		t->write(AY_AMPLITUDE_A + 2, TICK_LEVEL);
		t->tick_on = 1;
	}
}

bool tune_poll(tune_t *t, uint32_t now_ms) {
	uint32_t elapsed = now_ms - t->start_ms;
	bool loop = pieces[t->piece].loop;
	while (t->eighths * t->step_ms <= elapsed) {
		if (!loop && t->eighths >= t->length) {
			silence(t);
			return false;
		}
		step(t, t->eighths++);
	}
	if (t->tick_on && elapsed - (t->eighths - 1) * t->step_ms >= TUNE_TICK_MS) {
		t->write(AY_AMPLITUDE_A + 2, 0);
		t->tick_on = 0;
	}
	return true;
}
