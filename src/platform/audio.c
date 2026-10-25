#include "audio.h"

#include "bell.h"
#include "board.h"
#include "settings.h"

#if BOARD_AUDIO_I2S

#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "mixer.h"

#define FRAME_CYCLES 65 // PIO cycles a stereo frame (audio_program)
#define BLOCK MIXER_MAX_FRAMES
#define BLOCK_BITS 10 // a block is 1 KB: a DMA read ring

static_assert(BLOCK * 4 == 1u << BLOCK_BITS, "a block is the ring");

// The two blocks the DMA channels take turns over: while one plays, the
// interrupt refills the other. Each channel reads its block as a ring, so
// it is back at the block's start when it ends: with interrupts off (a
// flash write) the channels replay the blocks (audio_silence zeroes them)
// instead of running on through memory, which sounded as a burst of noise.
static uint32_t __attribute__((aligned(1u << BLOCK_BITS))) blocks[2][BLOCK];
static int dma[2] = {-1, -1};
static mixer_t mix;
static ay8910_t ay;
static PIO pio;
static uint sm, offset;
static pio_program_t program;
static bool ready;   // bell_init has run: the clocks are final
static bool fitted = true; // the Sound DAC setting
static bool ay_attached;
static uint8_t level = SETTINGS_BELL_DEFAULT, ay_level = SETTINGS_SOUND_DEFAULT;
// The square wave's amplitude per Bell level, 2 dB a step: the default, 3,
// 1/16 of full scale (-24 dBFS: loud enough on a speaker, safe in
// headphones), 10 about 1/3 (-10 dBFS).
static const uint16_t amplitudes[SETTINGS_BELL_MAX + 1] = {
	0, 0x050c, 0x065a, 0x0800, 0x0a12, 0x0cae, 0x0ff6, 0x1418, 0x194c, 0x1fd9, 0x2818,
};
// The AY's gain (Q15) per Sound chip level, 2 dB a step: 10 the chip as it
// comes (three channels at 15 reach full scale, never clipped), 9 about
// what the Bell level's 3 x gave the AY at 9, which the user found
// comfortable; the default 7, 6 dB under that.
static const uint16_t ay_gains[SETTINGS_SOUND_MAX + 1] = {
	0, 4125, 5193, 6538, 8231, 10362, 13045, 16422, 20675, 26028, 32767,
};

static bool running(void) {
	return dma[0] >= 0;
}

// A block played: refill it (its channel is back at its start, the other
// is playing now).
static void __not_in_flash_func(audio_irq)(void) {
	for (unsigned i = 0; i < 2; ++i) {
		if (dma_irqn_get_channel_status(1, (uint)dma[i])) {
			dma_irqn_acknowledge_channel(1, (uint)dma[i]);
			mixer_fill(&mix, blocks[i], BLOCK);
		}
	}
}

void __not_in_flash_func(audio_silence)(void) {
	for (unsigned b = 0; b < 2; ++b) {
		for (unsigned i = 0; i < BLOCK; ++i) {
			blocks[b][i] = 0;
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
	program = (pio_program_t){.instructions = audio_program, .length = 9, .origin = -1};
}

static void apply_levels(void) {
	mix.bell_amp = (int16_t)amplitudes[level];
	mix.ay_gain = ay_gains[ay_level];
}

static void start(void) {
	if (!pio_claim_free_sm_and_add_program_for_gpio_range(&program, &pio, &sm, &offset,
			BOARD_AUDIO_I2S_BCK_PIN, 3, true)) {
		return;
	}
	dma[0] = dma_claim_unused_channel(false);
	dma[1] = dma_claim_unused_channel(false);
	if (dma[0] < 0 || dma[1] < 0) {
		if (dma[0] >= 0) {
			dma_channel_unclaim((uint)dma[0]);
		}
		dma[0] = dma[1] = -1;
		pio_remove_program_and_unclaim_sm(&program, pio, sm, offset);
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

	mixer_init(&mix);
	ay_init(&ay, AY_CLOCK_RC2014, MIXER_RATE);
	apply_levels();
	mix.ay = ay_attached ? &ay : NULL;
	for (unsigned i = 0; i < 2; ++i) {
		mixer_fill(&mix, blocks[i], BLOCK); // silence
		dma_channel_config d = dma_channel_get_default_config((uint)dma[i]);
		channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
		channel_config_set_read_increment(&d, true);
		channel_config_set_write_increment(&d, false);
		channel_config_set_ring(&d, false, BLOCK_BITS);
		channel_config_set_dreq(&d, pio_get_dreq(pio, sm, true));
		channel_config_set_chain_to(&d, (uint)dma[i ^ 1]);
		dma_channel_configure((uint)dma[i], &d, &pio->txf[sm], blocks[i], BLOCK, false);
		dma_irqn_set_channel_enabled(1, (uint)dma[i], true);
	}
	// Core 0's (bell_init and SET-UP's save run there); DMA_IRQ_0 is the
	// video's, core 1.
	irq_set_exclusive_handler(DMA_IRQ_1, audio_irq);
	irq_set_enabled(DMA_IRQ_1, true);
	dma_channel_start((uint)dma[0]);
}

// Everything given back, the DAC's pins left as inputs.
static void stop(void) {
	irq_set_enabled(DMA_IRQ_1, false);
	for (unsigned i = 0; i < 2; ++i) {
		uint ch = (uint)dma[i];
		dma_irqn_set_channel_enabled(1, ch, false);
		// Unchained first, so the abort cannot start the other one.
		hw_write_masked(&dma_hw->ch[ch].al1_ctrl, ch << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
			DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
	}
	for (unsigned i = 0; i < 2; ++i) {
		dma_channel_abort((uint)dma[i]);
		dma_irqn_acknowledge_channel(1, (uint)dma[i]);
		dma_channel_unclaim((uint)dma[i]);
		dma[i] = -1;
	}
	irq_remove_handler(DMA_IRQ_1, audio_irq);
	pio_sm_set_enabled(pio, sm, false);
	pio_remove_program_and_unclaim_sm(&program, pio, sm, offset);
	gpio_deinit(BOARD_AUDIO_I2S_BCK_PIN);
	gpio_deinit(BOARD_AUDIO_I2S_WS_PIN);
	gpio_deinit(BOARD_AUDIO_I2S_DATA_PIN);
}

void bell_init(void) {
	build_program();
	ready = true;
	if (fitted) {
		start();
	}
}

void audio_set_fitted(bool on) {
	fitted = on;
	if (!ready || on == running()) {
		return;
	}
	if (on) {
		start();
	} else {
		stop();
	}
}

bool audio_fitted(void) {
	return fitted;
}

void bell_set_level(uint8_t l) {
	if (l <= SETTINGS_BELL_MAX) {
		level = l;
		apply_levels(); // a beep sounding now changes with it
	}
}

void audio_set_ay_level(uint8_t l) {
	if (l <= SETTINGS_SOUND_MAX) {
		ay_level = l;
		apply_levels();
	}
}

void bell_ring(void) {
	if (running()) {
		mixer_ring(&mix);
	}
}

bool bell_present(void) {
	return running();
}

void audio_attach_ay(bool on) {
	ay_attached = on;
	if (running()) {
		mix.ay = on ? &ay : NULL;
	}
}

void audio_ay_write(uint8_t reg, uint8_t value) {
	if (running()) {
		ay_write(&ay, reg, value);
	}
}

uint8_t audio_ay_read(uint8_t reg) {
	return running() ? ay_read(&ay, reg) : 0xff;
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

void audio_set_fitted(bool on) {
	(void)on;
}

bool audio_fitted(void) {
	return false;
}

void audio_set_ay_level(uint8_t l) {
	(void)l;
}

void audio_silence(void) {
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
