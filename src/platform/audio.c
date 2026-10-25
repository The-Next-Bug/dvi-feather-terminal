#include "audio.h"

#include "bell.h"
#include "board.h"
#include "settings.h"

#if BOARD_AUDIO_I2S

#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "mixer.h"

#define FRAME_CYCLES 65 // PIO cycles a stereo frame (audio_program)
#define BLOCK MIXER_MAX_FRAMES

// The two blocks the DMA channels take turns over: while one plays, the
// interrupt refills the other.
static uint32_t blocks[2][BLOCK];
static int dma[2] = {-1, -1};
static mixer_t mix;
static ay8910_t ay;
static PIO pio;
static uint sm;
static uint8_t level = SETTINGS_BELL_DEFAULT;
// The square wave's amplitude per Bell level, 2 dB a step: the default, 3,
// 1/16 of full scale (-24 dBFS: loud enough on a speaker, safe in
// headphones), 10 about 1/3 (-10 dBFS).
static const uint16_t amplitudes[SETTINGS_BELL_MAX + 1] = {
	0, 0x050c, 0x065a, 0x0800, 0x0a12, 0x0cae, 0x0ff6, 0x1418, 0x194c, 0x1fd9, 0x2818,
};

// The level sets the bell's amplitude and, until the AY has a setting of
// its own (bd 1ls.18.5), its gain: one AY channel at its loudest as loud
// as the bell (three of them in mono reach 3 x).
static void apply_level(void) {
	mix.bell_amp = (int16_t)amplitudes[level];
	mix.ay_gain = (uint16_t)(3u * amplitudes[level]);
}

// A block played: point its channel back at it (the other is playing now)
// and refill it.
static void __not_in_flash_func(audio_irq)(void) {
	for (unsigned i = 0; i < 2; ++i) {
		if (dma_irqn_get_channel_status(1, (uint)dma[i])) {
			dma_irqn_acknowledge_channel(1, (uint)dma[i]);
			dma_channel_set_read_addr((uint)dma[i], blocks[i], false);
			mixer_fill(&mix, blocks[i], BLOCK);
		}
	}
}

// Side-set pins: bit 0 BCK, bit 1 WS. Philips I2S, as the TDA1387 takes
// it (bd 1ls.18.1; frank-386's audio_i2s.pio): 16-bit words, MSB first,
// the high half (left) with WS low, the low half (right) with WS high, WS
// switching on each half's last bit so the next MSB follows one BCK after
// it; each bit set up while BCK is low, taken on its rise. With WS and the
// MSB together the DAC read bit 14 as the sign: twice the amplitude, and
// samples past half scale wrapped. A frame is a 32-bit word (pull noblock:
// X, 0, when the FIFO is empty, so silence and the clocks keep running), Y
// counting the bits. The pull adds a cycle to the last bit's low half (65
// cycles a frame).
static uint16_t audio_program[9];

static void build_program(void) {
	uint16_t *p = audio_program;
	p[0] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0)); // high half's bits
	p[1] = (uint16_t)(pio_encode_jmp_y_dec(0) | pio_encode_sideset(2, 1));
	p[2] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2)); // its last, WS up
	p[3] = (uint16_t)(pio_encode_set(pio_y, 14) | pio_encode_sideset(2, 3));
	p[4] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2)); // low half's bits
	p[5] = (uint16_t)(pio_encode_jmp_y_dec(4) | pio_encode_sideset(2, 3));
	p[6] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0)); // its last, WS down
	p[7] = (uint16_t)(pio_encode_pull(false, false) | pio_encode_sideset(2, 0)); // the next frame (start here)
	p[8] = (uint16_t)(pio_encode_set(pio_y, 14) | pio_encode_sideset(2, 1));
}

void bell_init(void) {
	build_program();
	const pio_program_t program = {.instructions = audio_program, .length = 9, .origin = -1};
	uint offset;
	if (!pio_claim_free_sm_and_add_program_for_gpio_range(&program, &pio, &sm, &offset,
			BOARD_AUDIO_I2S_BCK_PIN, 3, true)) {
		return;
	}
	pio_sm_config c = pio_get_default_sm_config();
	sm_config_set_wrap(&c, offset, offset + 8);
	sm_config_set_sideset(&c, 2, false, false);
	sm_config_set_sideset_pins(&c, BOARD_AUDIO_I2S_BCK_PIN);
	sm_config_set_out_pins(&c, BOARD_AUDIO_I2S_DATA_PIN, 1);
	sm_config_set_out_shift(&c, false, false, 32); // MSB first, no autopull
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
	// clk_sys / (rate x cycles a frame), in 1/256ths.
	uint64_t div = ((uint64_t)clock_get_hz(clk_sys) << 8) / ((uint64_t)MIXER_RATE * FRAME_CYCLES);
	sm_config_set_clkdiv_int_frac8(&c, (uint32_t)(div >> 8), (uint8_t)div);
	pio_gpio_init(pio, BOARD_AUDIO_I2S_BCK_PIN);
	pio_gpio_init(pio, BOARD_AUDIO_I2S_WS_PIN);
	pio_gpio_init(pio, BOARD_AUDIO_I2S_DATA_PIN);
	pio_sm_set_consecutive_pindirs(pio, sm, BOARD_AUDIO_I2S_BCK_PIN, 2, true);
	pio_sm_set_consecutive_pindirs(pio, sm, BOARD_AUDIO_I2S_DATA_PIN, 1, true);
	pio_sm_init(pio, sm, offset + 7, &c);
	pio_sm_exec(pio, sm, pio_encode_set(pio_x, 0)); // the silence word
	pio_sm_set_enabled(pio, sm, true);

	dma[0] = dma_claim_unused_channel(false);
	dma[1] = dma_claim_unused_channel(false);
	if (dma[0] < 0 || dma[1] < 0) {
		if (dma[0] >= 0) {
			dma_channel_unclaim((uint)dma[0]);
		}
		dma[0] = dma[1] = -1;
		return; // the PIO sends silence (pull noblock), nothing else
	}
	mixer_init(&mix);
	ay_init(&ay, AY_CLOCK_RC2014, MIXER_RATE);
	apply_level();
	for (unsigned i = 0; i < 2; ++i) {
		mixer_fill(&mix, blocks[i], BLOCK); // silence
		dma_channel_config d = dma_channel_get_default_config((uint)dma[i]);
		channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
		channel_config_set_read_increment(&d, true);
		channel_config_set_write_increment(&d, false);
		channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
		channel_config_set_chain_to(&d, (uint)dma[i ^ 1]);
		dma_channel_configure((uint)dma[i], &d, &pio->txf[sm], blocks[i], BLOCK, false);
		dma_irqn_set_channel_enabled(1, (uint)dma[i], true);
	}
	// Core 0's (bell_init runs there); DMA_IRQ_0 is the video's, core 1.
	irq_set_exclusive_handler(DMA_IRQ_1, audio_irq);
	irq_set_enabled(DMA_IRQ_1, true);
	dma_channel_start((uint)dma[0]);
}

void bell_set_level(uint8_t l) {
	if (l >= sizeof amplitudes / sizeof amplitudes[0] || l == level) {
		return;
	}
	level = l;
	apply_level(); // a beep sounding now changes with it
}

void bell_ring(void) {
	if (dma[0] >= 0) {
		mixer_ring(&mix);
	}
}

bool bell_present(void) {
	return dma[0] >= 0;
}

void audio_attach_ay(bool on) {
	if (dma[0] >= 0) {
		mix.ay = on ? &ay : NULL;
	}
}

void audio_ay_write(uint8_t reg, uint8_t value) {
	if (dma[0] >= 0) {
		ay_write(&ay, reg, value);
	}
}

uint8_t audio_ay_read(uint8_t reg) {
	return dma[0] >= 0 ? ay_read(&ay, reg) : 0xff;
}

#else

void bell_init(void) {
}

void bell_set_level(uint8_t l) {
	(void)l;
}

void bell_ring(void) {
}

bool bell_present(void) {
	return false;
}

void audio_attach_ay(bool on) {
	(void)on;
}

void audio_ay_write(uint8_t reg, uint8_t value) {
	(void)reg;
	(void)value;
}

uint8_t audio_ay_read(uint8_t reg) {
	(void)reg;
	return 0xff;
}

#endif
