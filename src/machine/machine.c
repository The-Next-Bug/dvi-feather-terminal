// The Z80 machine (machine.h): z80pack's CPU core with picosim's memory,
// ports and boot ROM. The port handlers follow picosim's simio.c (z80pack,
// Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt, MIT licence).
//
// z80pack keeps the CPU in globals (A, B, PC, T, ...), so only this file
// includes its headers.

#include "machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simmem.h"
#include "simcore.h"
#include "simio.h"
#include "simport.h"
#include "sd-fdc.h"

#define TIMER_PERIOD_US 16667 // 60 Hz
#define CHECK_TSTATES 20000   // how often the CPU loop reads the clock when unlimited
#define KEY_QUEUE 64          // a power of two
#define HWCTL_UNLOCK 0xaa
// The clock's date at power-on, in days since 1.1.1978 (CP/M 3's day 1):
// 1.1.2026 (no RTC: the date only moves on while the board is powered).
#ifndef MACHINE_EPOCH_DAYS
#define MACHINE_EPOCH_DAYS 17533
#endif

BYTE __attribute__((aligned(4))) bnk0[65536];
BYTE __attribute__((aligned(4))) bnks[NUMSEG][SEGSIZ];
BYTE selbnk, *curbnk;

static const machine_platform_t *plat;

static struct {
	uint64_t slice_end;
	bool slicing, slice_over;
	bool timer;          // 60 Hz timer on (port 67)
	uint64_t next_tick;
	BYTE hwctl_lock;
	BYTE sio1_last, sio2_last;
	BYTE aux_held;       // SIO2's received byte
	bool aux_have;
	uint8_t keys[KEY_QUEUE];
	unsigned key_head, key_tail;
	Tstates_t speed_T;   // machine_speed_khz's last sample
	uint64_t speed_time;
	char reason[48];
	BYTE clk_cmd, clk_fmt; // clock: register selected, 1 = decimal (else BCD)
	BYTE dinfo_drive;      // port 8: the drive asked about
	bool dinfo_format_read; // its format read: the next IN is the mount count
	uint8_t dinfo_asked;   // drives (bit n) asked about since the reset
	// Port 10: 0 waiting for a command, 1/2 for the DMA address's low/high
	// byte; the address and the last command's status.
	BYTE xfer_state, xfer_status;
	WORD xfer_dma;
} m;

// picosim's boot ROM (picosim/roms/bootrom.asm): read drive A track 0
// sector 1 to 0000H and jump there; HALT on a disk error other than "no
// disk".
static const BYTE boot_rom[] = {
	0xf3, 0xaf, 0x32, 0x00, 0xf0, 0x32, 0x02, 0xf0, 0x32, 0x03, 0xf0, 0x3e,
	0x01, 0x32, 0x01, 0xf0, 0x3e, 0x10, 0xd3, 0x04, 0x3e, 0x00, 0xd3, 0x04,
	0x3e, 0xf0, 0xd3, 0x04, 0x3e, 0x20, 0xd3, 0x04, 0xdb, 0x04, 0xb7, 0xca,
	0x00, 0x00, 0xfe, 0x02, 0xca, 0x00, 0x00, 0x76,
};

// ---- time: the slices and the 60 Hz timer ----

static void timer_check(uint64_t now) {
	if (!m.timer || now < m.next_tick)
		return;
	int_data = 0xff; // RST 38H in IM 0, the vector byte in IM 2
	int_int = true;
	m.next_tick += TIMER_PERIOD_US;
	if (now >= m.next_tick) // far behind (the CPU was not running): resync
		m.next_tick = now + TIMER_PERIOD_US;
}

// z80pack reads the clock every CHECK_TSTATES T-states and around port I/O:
// end the slice there (the CPU loop stops after the current instruction).
uint64_t machine_clock_us(void) {
	uint64_t now = plat->clock_us();
	timer_check(now);
	if (m.slicing && now >= m.slice_end && cpu_state == ST_CONTIN_RUN) {
		cpu_state = ST_STOPPED;
		m.slice_over = true;
	}
	return now;
}

// The speed limit's wait.
void machine_sleep_us(unsigned long time) {
	uint64_t end = plat->clock_us() + time;
	uint64_t now;
	while ((now = plat->clock_us()) < end) {
		timer_check(now);
		plat->idle();
	}
}

// HALT with interrupts on waits here, a millisecond at a time, for an
// interrupt. At the end of the slice it stops the CPU with PC back on the
// HALT, so the next slice halts again rather than run on.
void machine_halt_wait_ms(unsigned ms) {
	uint64_t end = plat->clock_us() + ms * 1000ull;
	uint64_t now;
	while ((now = plat->clock_us()) < end && !int_int) {
		timer_check(now);
		plat->idle();
	}
	if (m.slicing && now >= m.slice_end && !int_int && cpu_state == ST_CONTIN_RUN) {
		cpu_state = ST_STOPPED;
		m.slice_over = true;
		--PC;
	}
}

// ---- ports ----

static BYTE sio1s_in(void) {
	BYTE stat = 0x01; // bit 0 = 0: input ready; bit 7 = 0: output ready
	if (m.key_head != m.key_tail)
		stat &= (BYTE)~0x01;
	return stat;
}

static BYTE sio1d_in(void) {
	if (m.key_head != m.key_tail)
		m.sio1_last = m.keys[m.key_tail++ % KEY_QUEUE];
	return m.sio1_last;
}

static void sio1d_out(BYTE data) {
	plat->console_out(data & 0x7f); // strip parity, some software won't
}

// SIO2: one byte held from the platform until the CPU reads it.
static BYTE sio2s_in(void) {
	if (!m.aux_have && plat->aux_in(&m.aux_held))
		m.aux_have = true;
	return m.aux_have ? 0x00 : 0x01;
}

static BYTE sio2d_in(void) {
	if (!m.aux_have && plat->aux_in(&m.aux_held))
		m.aux_have = true;
	if (m.aux_have) {
		m.sio2_last = m.aux_held;
		m.aux_have = false;
	}
	return m.sio2_last;
}

static void sio2s_out(BYTE data) {
	UNUSED(data);
}

static void sio2d_out(BYTE data) {
	plat->aux_out(data);
}

static BYTE prts_in(void) {
	return plat->printer_out ? 0xff : 0x00;
}

static BYTE prtd_in(void) {
	return 0x1a; // CP/M EOF
}

static void prts_out(BYTE data) {
	UNUSED(data);
}

static void prtd_out(BYTE data) {
	if (plat->printer_out && data != '\r' && data != 0)
		plat->printer_out(data);
}

static void led_out(BYTE data) {
	UNUSED(data); // picosim's RGB LED
}

// Banks available in the high nibble, the selected one in the low.
static BYTE mmu_in(void) {
	return (BYTE)(NUMSEG << 4 | selbnk);
}

static void mmu_out(BYTE data) {
	if (data > NUMSEG) {
		snprintf(m.reason, sizeof m.reason, "MMU: no bank %u (%u fitted) at 0x%04x", data, NUMSEG, PC);
		cpu_error = IOERROR;
		cpu_state = ST_STOPPED;
		return;
	}
	selbnk = data;
	if (selbnk != 0)
		curbnk = bnks[selbnk - 1];
}

static BYTE timer_in(void) {
	return m.timer ? 1 : 0;
}

static void timer_out(BYTE data) {
	if (data == 1) {
		if (!m.timer) {
			m.timer = true;
			m.next_tick = plat->clock_us() + TIMER_PERIOD_US;
		}
	} else {
		m.timer = false;
	}
}

static BYTE hwctl_in(void) {
	return m.hwctl_lock;
}

// Locked until 0xAA is written; then one command: bit 7 halts the machine
// (BYE.COM), bit 6 resets it (RESET.COM). The CPU switches (bits 5, 4) do
// nothing: there is no 8080.
static void hwctl_out(BYTE data) {
	if (m.hwctl_lock) {
		if (data == HWCTL_UNLOCK)
			m.hwctl_lock = 0;
		return;
	}
	m.hwctl_lock = 0xff;
	if (data & 0x80) {
		cpu_error = IOHALT;
		cpu_state = ST_STOPPED;
	} else if (data & 0x40) {
		machine_reset();
	}
}

// The clock (ports 65/66), as z80pack's rtc80.c reads it but without the C
// library's localtime: the time is MACHINE_EPOCH_DAYS (days since 1.1.1978)
// plus the time since power-on. Register 3/4: days since 1.1.1978; 6: the
// month 0-11 and 7: the year - 1900, as rtc80.c gives them.
static BYTE to_bcd(unsigned v) {
	return (BYTE)((v / 10 % 10) << 4 | v % 10);
}

static BYTE clkc_in(void) {
	return m.clk_fmt;
}

static void clkc_out(BYTE data) {
	m.clk_cmd = data;
	if (data == 255)
		m.clk_fmt ^= 1;
}

static BYTE clkd_in(void) {
	uint64_t secs = plat->clock_us() / 1000000u;
	unsigned days = MACHINE_EPOCH_DAYS + (unsigned)(secs / 86400);
	unsigned day_secs = (unsigned)(secs % 86400);
	// The date from the days since 1.3.0000 (Howard Hinnant's
	// civil_from_days); 1.1.1978 is day 722390 of that count.
	unsigned z = days - 1 + 722390, era = z / 146097, doe = z - era * 146097;
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	unsigned mday = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
	unsigned year = yoe + era * 400 + (month <= 2);
	unsigned v;
	switch (m.clk_cmd) {
	case 0:
		v = day_secs % 60;
		break;
	case 1:
		v = day_secs / 60 % 60;
		break;
	case 2:
		v = day_secs / 3600;
		break;
	case 3:
		return (BYTE)days;
	case 4:
		return (BYTE)(days >> 8);
	case 5:
		v = mday;
		break;
	case 6:
		v = month - 1;
		break;
	case 7:
		v = year - 1900;
		break;
	default:
		return 0;
	}
	return m.clk_fmt ? (BYTE)v : to_bcd(v);
}

static void clkd_out(BYTE data) {
	UNUSED(data); // the clock cannot be set
}

// Port 8: the format of the disk in the drive last written, then (IN
// again) how many images have been mounted on it.
static void dinfo_out(BYTE data) {
	m.dinfo_drive = data;
	m.dinfo_format_read = false;
}

static BYTE dinfo_in(void) {
	if (m.dinfo_format_read)
		return plat->disk_mounts ? plat->disk_mounts(m.dinfo_drive) : 0;
	m.dinfo_format_read = true;
	if (m.dinfo_drive < 8)
		m.dinfo_asked |= (uint8_t)(1u << m.dinfo_drive);
	return plat->disk_format ? plat->disk_format(m.dinfo_drive) : 0;
}

// A hard-disk image is only read or written for a system that asked port 8
// for its format (bd f8w.7): a BIOS that does not ask (picosim's CP/M 3 and
// MP/M) takes every drive for an 8" floppy, whose tracks and sectors fit
// inside a hard disk's, so it would read garbage and write over the image.
static bool disk_refused(int drive) {
	if (drive < 0 || drive >= 8 || (m.dinfo_asked & (1u << drive)) || !plat->disk_format)
		return false;
	return plat->disk_format((unsigned)drive) >= 2;
}

// Port 10: the file transfer device (machine.h). Input and output move
// between memory at the DMA address and the platform's buffer. Cold: built
// for size, as all code takes SRAM.
static void __attribute__((cold)) xfer_out(BYTE data) {
	if (m.xfer_state == 1) {
		m.xfer_dma = data;
		m.xfer_state = 2;
		return;
	}
	if (m.xfer_state == 2) {
		m.xfer_dma |= (WORD)(data << 8);
		m.xfer_state = 0;
		return;
	}
	uint8_t cmd = data & 0xf0, volume = data & 0x0f;
	if (cmd == XFER_SETDMA) {
		m.xfer_state = 1;
		m.xfer_status = XFER_ST_OK;
		return;
	}
	unsigned in = cmd == XFER_WRITE ? XFER_RECORD
		: cmd == XFER_FIND_FIRST || cmd == XFER_OPEN || cmd == XFER_CREATE || cmd == XFER_DELETE ? XFER_NAME
		: 0;
	unsigned out = cmd == XFER_READ ? XFER_RECORD : cmd == XFER_FIND_FIRST || cmd == XFER_FIND_NEXT ? XFER_ENTRY : 0;
	uint8_t buf[XFER_RECORD];
	memset(buf, 0, sizeof buf);
	for (unsigned i = 0; i < in; ++i)
		buf[i] = dma_read((WORD)(m.xfer_dma + i));
	m.xfer_status = plat->xfer ? plat->xfer(cmd, volume, buf) : XFER_ST_NO_VOLUME;
	if (m.xfer_status == XFER_ST_OK)
		for (unsigned i = 0; i < out; ++i)
			dma_write((WORD)(m.xfer_dma + i), buf[i]);
}

static BYTE xfer_in(void) {
	return m.xfer_status;
}

static BYTE fpsw_in(void) {
	return 0;
}

static void fpsw_out(BYTE data) {
	UNUSED(data);
}

in_func_t *const port_in[256] = {
	[0] = sio1s_in,
	[1] = sio1d_in,
	[2] = sio2s_in,
	[3] = sio2d_in,
	[4] = fdc_in,
	[5] = prts_in,
	[6] = prtd_in,
	[8] = dinfo_in,
	[10] = xfer_in,
	[64] = mmu_in,
	[65] = clkc_in,
	[66] = clkd_in,
	[67] = timer_in,
	[160] = hwctl_in,
	[254] = fpsw_in,
	[255] = fpsw_in,
};

out_func_t *const port_out[256] = {
	[0] = led_out,
	[1] = sio1d_out,
	[2] = sio2s_out,
	[3] = sio2d_out,
	[4] = fdc_out,
	[5] = prts_out,
	[6] = prtd_out,
	[8] = dinfo_out,
	[10] = xfer_out,
	[64] = mmu_out,
	[65] = clkc_out,
	[66] = clkd_out,
	[67] = timer_out,
	[160] = hwctl_out,
	[254] = fpsw_out,
	[255] = fpsw_out,
};

void init_io(void) {
}

void exit_io(void) {
	m.timer = false;
}

// ---- the disk controller's platform functions (sd-fdc.h) ----

BYTE read_sec(int drive, int track, int sector, WORD addr) {
	if (addr > 0x10000 - SEC_SZ)
		return FDC_STAT_DMAADR;
	if (disk_refused(drive))
		return FDC_STAT_NODISK;
	uint8_t buf[SEC_SZ];
	BYTE st = plat->disk_read((unsigned)drive, (unsigned)track, (unsigned)sector, buf);
	if (st == FDC_STAT_OK)
		for (unsigned i = 0; i < SEC_SZ; ++i)
			dma_write((WORD)(addr + i), buf[i]);
	return st;
}

BYTE write_sec(int drive, int track, int sector, WORD addr) {
	if (addr > 0x10000 - SEC_SZ)
		return FDC_STAT_DMAADR;
	if (disk_refused(drive))
		return FDC_STAT_NODISK;
	uint8_t buf[SEC_SZ];
	for (unsigned i = 0; i < SEC_SZ; ++i)
		buf[i] = dma_read((WORD)(addr + i));
	return plat->disk_write((unsigned)drive, (unsigned)track, (unsigned)sector, buf);
}

void get_fdccmd(BYTE *cmd, WORD addr) {
	for (unsigned i = 0; i < 4; ++i)
		cmd[i] = dma_read((WORD)(addr + i));
}

// ---- the machine ----

void machine_reset(void) {
	m.timer = false;
	m.hwctl_lock = 0xff;
	m.dinfo_asked = 0; // the next system has to ask again
	// No file stays open for a program that is gone.
	m.xfer_state = 0;
	m.xfer_status = XFER_ST_OK;
	if (plat->xfer) {
		uint8_t buf[XFER_RECORD];
		plat->xfer(XFER_RESET, 0, buf);
	}
	reset_cpu();
	selbnk = 0;
	PC = ROM_PAGE;
}

void machine_init(const machine_platform_t *platform) {
	plat = platform;
	memset(&m, 0, sizeof m);
	// Noise, as in a real machine after power on.
	for (size_t i = 0; i < ROM_PAGE; ++i)
		bnk0[i] = (BYTE)rand();
	for (unsigned b = 0; b < NUMSEG; ++b)
		for (size_t i = 0; i < SEGSIZ; ++i)
			bnks[b][i] = (BYTE)rand();
	memset(bnk0 + ROM_PAGE, 0xff, 0x10000 - ROM_PAGE);
	memcpy(bnk0 + ROM_PAGE, boot_rom, sizeof boot_rom);
	init_cpu();
	init_io();
	machine_set_speed(0);
	machine_reset();
	m.speed_T = T;
	m.speed_time = cpu_time;
}

machine_state_t machine_run(uint32_t us) {
	m.slice_end = plat->clock_us() + us;
	m.slicing = true;
	m.slice_over = false;
	run_cpu();
	m.slicing = false;
	if (cpu_error == NONE && m.slice_over)
		return MACHINE_RUNNING;
	return MACHINE_STOPPED;
}

const char *machine_stop_reason(void) {
	switch (cpu_error) {
	case NONE:
		snprintf(m.reason, sizeof m.reason, "Stopped at 0x%04x", PC);
		break;
	case OPHALT:
		snprintf(m.reason, sizeof m.reason, "HALT with interrupts off at 0x%04x", (WORD)(PC - 1));
		break;
	case IOHALT:
		snprintf(m.reason, sizeof m.reason, "System halted");
		break;
	case IOERROR:
		break; // set by the port handler
	case OPTRAP1:
	case OPTRAP2:
	case OPTRAP4: {
		// PC is past the opcode's bytes (prefixes, a DD CB / FD CB
		// displacement): show them.
		unsigned n = cpu_error == OPTRAP1 ? 1 : cpu_error == OPTRAP2 ? 2 : 4;
		WORD at = (WORD)(PC - n);
		int len = snprintf(m.reason, sizeof m.reason, "Unknown instruction");
		for (unsigned i = 0; i < n; ++i)
			len += snprintf(m.reason + len, sizeof m.reason - (size_t)len, " %02X", getmem((WORD)(at + i)));
		snprintf(m.reason + len, sizeof m.reason - (size_t)len, " at 0x%04x", at);
		break;
	}
	case USERINT:
		snprintf(m.reason, sizeof m.reason, "Interrupted at 0x%04x", PC);
		break;
	case INTERROR:
		snprintf(m.reason, sizeof m.reason, "Bad interrupt data 0x%02x", int_data & 0xff);
		break;
	default:
		snprintf(m.reason, sizeof m.reason, "CPU error %d at 0x%04x", cpu_error, PC);
		break;
	}
	return m.reason;
}

bool machine_key(uint8_t c) {
	if (m.key_head - m.key_tail >= KEY_QUEUE)
		return false;
	m.keys[m.key_head++ % KEY_QUEUE] = c;
	return true;
}

void machine_set_speed(unsigned mhz) {
	f_value = (int)mhz;
	// z80pack's limiter works in 10 ms steps of mhz * 10000 T-states.
	tmax = mhz ? (int)mhz * 10000 : CHECK_TSTATES;
}

uint32_t machine_speed_khz(void) {
	Tstates_t dt = T - m.speed_T;
	uint64_t us = cpu_time - m.speed_time;
	m.speed_T = T;
	m.speed_time = cpu_time;
	return us ? (uint32_t)(dt * 1000 / us) : 0;
}

uint32_t machine_benchmark_khz(uint32_t ms) {
	BYTE saved[3] = {getmem(0), getmem(1), getmem(2)};
	WORD saved_pc = PC;
	BYTE saved_iff = IFF;
	bool saved_timer = m.timer;
	int saved_f = f_value;

	m.timer = false;
	IFF = 0;
	machine_set_speed(0);
	putmem(0, 0xc3); // JP 0000H
	putmem(1, 0x00);
	putmem(2, 0x00);
	PC = 0;
	Tstates_t t0 = T;
	uint64_t start = plat->clock_us(), end = start + ms * 1000ull, now;
	while ((now = plat->clock_us()) < end && machine_run((uint32_t)(end - now)) == MACHINE_RUNNING) {
	}
	uint64_t elapsed = plat->clock_us() - start;
	Tstates_t dt = T - t0;

	putmem(0, saved[0]);
	putmem(1, saved[1]);
	putmem(2, saved[2]);
	PC = saved_pc;
	IFF = saved_iff;
	m.timer = saved_timer;
	machine_set_speed((unsigned)saved_f);
	return elapsed ? (uint32_t)(dt * 1000 / elapsed) : 0;
}
