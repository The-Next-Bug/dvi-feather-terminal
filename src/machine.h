#ifndef MACHINE_H
#define MACHINE_H

// A Z80 CP/M machine on z80pack's CPU core (lib/z80pack), compatible with
// z80pack's picosim: its boot ROM, disk controller (SD-FDC) and port map,
// so picosim's CP/M 2.2, CP/M 3 and MP/M II disk images boot on it.
// Portable C (no pico-sdk), host-tested; the platform supplies time, the
// console, the second serial port and the disk sectors.
//
// Ports (picosim's): 0/1 SIO1 status/data (the console), 2/3 SIO2 (aux
// serial), 4 FDC, 5/6 printer, 64 MMU (bank select; reads banks << 4 |
// bank), 65/66 clock, 67 60 Hz timer (RST 38H), 160 hardware control
// (after 0xAA: bit 7 halt, bit 6 reset), 254/255 front panel switches;
// and our own 8: the format of a drive (OUT the drive, IN its format), for
// the 8-drive BIOS in src/cpm.
//
// The CPU runs in slices (machine_run) from the platform's main loop;
// while it waits (HALT, the speed limit) it calls the platform's idle hook,
// which must not call back into the machine except machine_key.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint64_t (*clock_us)(void); // monotonic microseconds
	void (*idle)(void);         // the CPU is waiting: keep the screen and keyboard going
	void (*console_out)(uint8_t c);
	// SIO2: send, and whether a byte has arrived (then read it into *c).
	void (*aux_out)(uint8_t c);
	bool (*aux_in)(uint8_t *c);
	void (*printer_out)(uint8_t c); // may be NULL
	// Disk sectors (128 bytes; track from 0, sector from 1, within the
	// drive's format): FDC status, 0 = OK.
	uint8_t (*disk_read)(unsigned drive, unsigned track, unsigned sector, uint8_t *buf);
	uint8_t (*disk_write)(unsigned drive, unsigned track, unsigned sector, const uint8_t *buf);
	// The format of the disk in a drive (port 8): 0 none, 1 8" floppy,
	// 2 4 MB, 3 8 MB hard disk (drives.h). May be NULL: always 0.
	uint8_t (*disk_format)(unsigned drive);
} machine_platform_t;

typedef enum {
	MACHINE_RUNNING, // the slice ended; call machine_run again
	MACHINE_STOPPED, // the CPU stopped: machine_stop_reason says why
} machine_state_t;

// Power on: memory filled with noise, the boot ROM in the top page, the CPU
// reset to the boot ROM (which loads track 0 sector 1 of drive A to 0 and
// jumps there).
void machine_init(const machine_platform_t *platform);

// The reset switch: CPU and MMU reset, timer off, start the boot ROM.
void machine_reset(void);

// Run the CPU for about us microseconds.
machine_state_t machine_run(uint32_t us);

// Why the CPU stopped, e.g. "System halted" (BYE) or "HALT with
// interrupts off at 0x1234".
const char *machine_stop_reason(void);

// A key for the console (SIO1). Returns false if the queue is full.
bool machine_key(uint8_t c);

// Emulated CPU speed limit in MHz, 0 = none (the default).
void machine_set_speed(unsigned mhz);

// The emulated clock rate in kHz (T-states per second of CPU time), over
// the runs since the last call; 0 if the CPU has not run.
uint32_t machine_speed_khz(void);

// Measure the CPU's speed: run JP 0 (10 T-states) at address 0 for ms
// milliseconds and return the clock rate in kHz. Memory and registers are
// put back.
uint32_t machine_benchmark_khz(uint32_t ms);

#endif
