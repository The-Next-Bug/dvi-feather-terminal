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
// and our own 8: the format of a drive (OUT the drive, IN its format, IN
// again its mount count), for our 8-drive BIOSes in src/machine/cpm; 10: the
// file transfer device; 12: the hardware information device; 68/69 a
// millisecond tick; and the AY-3-8910 sound chip on D8h/D0h, with our
// stereo register 16 (all below).
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
	// 2 4 MB, 3 8 MB hard disk (drives.h). May be NULL: always 0. Until
	// the running system has asked port 8 about a drive (since the last
	// reset), the disk controller refuses hard-disk images on it (status 2,
	// no disk): BIOSes that never ask treat every disk as a floppy.
	uint8_t (*disk_format)(unsigned drive);
	// How many images have been mounted on a drive (mod 256; port 8's
	// second IN), so a BIOS sees another image of the same format. May be
	// NULL: always 0.
	uint8_t (*disk_mounts)(unsigned drive);
	// The file transfer device (port 10, xfer.h): run command cmd
	// (XFER_*) on volume, with buf holding its input and taking its output
	// (XFER_RECORD bytes). Returns an XFER_ST_* status. May be NULL: every
	// command answers XFER_ST_NO_VOLUME.
	uint8_t (*xfer)(uint8_t cmd, uint8_t volume, uint8_t *buf);
	// The hardware information device (port 12): put item's bytes
	// (HWINFO_*, at most HWINFO_MAX) in buf and return how many; 0 for an
	// item it does not know. Items 1 and up only: machine.c answers
	// HWINFO_ID. May be NULL: port 12 reads 0xFF, as with no device there.
	size_t (*hwinfo)(uint8_t item, uint8_t *buf);
	// The wall clock for ports 65/66, in seconds since 31.12.1977 00:00
	// (datetime.h: / 86400 is CP/M 3's day number). May be NULL: 1.1.2026
	// at power-on plus the time since.
	uint32_t (*time_get)(void);
	// Set the wall clock (writes to port 66, below). May be NULL: the
	// clock cannot be set, as on picosim.
	void (*time_set)(uint32_t secs);
	// The AY-3-8910 (below): write or read a register (the chip ignores
	// 17-255 and reads 0xFF there; 16 is ours, MACHINE_AY_STEREO). Both or neither; NULL: its ports read
	// 0xFF, as with no chip fitted.
	void (*sound_write)(uint8_t reg, uint8_t value);
	uint8_t (*sound_read)(uint8_t reg);
} machine_platform_t;

// The AY-3-8910 (bd 1ls.18.4): ports as on Ed Brindley's RC2014 YM2149
// board: OUT D8h selects a register, OUT D0h writes it, IN D0h reads it.
// Reset (machine_reset) writes 0 to its registers: silence, and the
// layout back to SET-UP's. Not the MSX's
// A0h-A2h: A0h is picosim's hardware control port (160).
#define MACHINE_AY_SELECT 0xd8
#define MACHINE_AY_DATA 0xd0
// Register 16, ours (bd 1ls.18.9; a real AY ignores it): the stereo
// layout. Write 0 for SET-UP's Stereo setting, 1 mono (A, B and C in both
// ears), 2 ABC (A left, B center, C right), 3 ACB (A left, C center, B
// right); bits 2-7 are ignored. Read: what was written in bits 0-1, the
// layout in use (1-3, as written) in bits 4-5, so 0xFF (older firmware, or
// a real chip) means no stereo.
#define MACHINE_AY_STEREO 16

// Ports 68/69, ours: a free-running millisecond count of the platform's
// clock, for timing without interrupts (F83, AYTEST): IN 68 its low byte,
// latching the high byte for IN 69, which reads it; wraps every 65.536 s.
#define MACHINE_TICK_LOW 68
#define MACHINE_TICK_HIGH 69

// Ports 65/66, the clock, as picosim's: OUT 65 selects a register, IN 66
// reads it: 0 seconds, 1 minutes, 2 hours, 3/4 the day number (low, high:
// days since 31.12.1977, CP/M 3's), 5 the day of the month, 6 the month
// 0-11, 7 the year - 1900; in BCD, or in binary after OUT 65,255 (which
// toggles it; IN 65 reads 1 for binary). Days are always binary. Ours:
// OUT 66 sets the selected register through the platform's time_set: 0-2
// (BCD or binary as read) replace that field of the current time, with the
// seconds' fraction zeroed; a write to 3 is kept for the next write to 4,
// which sets the day number from both (the time of day kept). Values out of
// range, and registers 5-7, are ignored. CP/M 3's BIOS TIME (bnkbios3.asm)
// writes the day number, then hours, minutes, seconds.

// Port 10, the file transfer device: files in a folder of a FAT volume,
// read and written a 128-byte record at a time by DMA, in the style of the
// disk controller (port 4). OUT 10 a command: the high nibble the command,
// the low nibble the volume (0 the SD card, 1 a USB stick); IN 10 the status
// of the last command. XFER_SETDMA takes the DMA address from the next two
// OUTs (low, high); the other commands move their input from, and their
// output to, memory at the DMA address (banking applies, as for the disk
// controller). Names are 11 bytes in CP/M's FCB form ("NAME    EXT",
// blank-padded; ? matches any character in a pattern).
#define XFER_RESET 0x00      // close the open file and the listing
#define XFER_SETDMA 0x10     // the next two OUTs: the DMA address, low then high
#define XFER_FIND_FIRST 0x20 // in: a pattern; out: XFER_ENTRY (name, size)
#define XFER_FIND_NEXT 0x30  // out: the next XFER_ENTRY of the listing
#define XFER_OPEN 0x40       // in: a name; open it to read
#define XFER_CREATE 0x50     // in: a name; a new file to write (not over one)
#define XFER_READ 0x60       // out: the next record, the last padded with ^Z
#define XFER_WRITE 0x70      // in: a record, appended
#define XFER_CLOSE 0x80      // close the open file
#define XFER_CLOSE_TEXT 0x90 // close it, cut at the first ^Z of its last record
#define XFER_DELETE 0xa0     // in: a name

#define XFER_NAME 11     // a name in FCB form
#define XFER_ENTRY 15    // a listing entry: the name, then the size in bytes (32-bit little-endian)
#define XFER_RECORD 128  // a record

#define XFER_ST_OK 0
#define XFER_ST_NOT_FOUND 1 // no such file, or the end of the listing
#define XFER_ST_EXISTS 2    // XFER_CREATE: the file is there
#define XFER_ST_EOF 3       // XFER_READ: no more records
#define XFER_ST_IO_ERROR 4  // a card error, or the volume is full
#define XFER_ST_NO_VOLUME 5 // no card or stick, or no FAT file system on it
#define XFER_ST_BAD 6       // a bad name, an unknown command, or no file open

// Port 12, the hardware information device (bd 1ls.9): what the machine
// runs on, for HWINFO.COM. OUT 12 an item number; IN 12 then reads the
// item's bytes in turn, and 0 after its end. Without the device (older
// firmware, picosim) IN 12 reads 0xFF: check HWINFO_ID first.
//
// Items 1 and 2 are text ending in 0. Items HWINFO_RECORD and up are one
// piece of hardware each, at a fixed number (programs can look one up):
// a status byte (HWINFO_ST_*), its name and a description, each text
// ending in 0. Status 0: no record under that number (the numbers in
// between may be used later, so read them all).
#define HWINFO_ID 0       // 'H', 'W', HWINFO_VERSION
#define HWINFO_FIRMWARE 1 // the firmware's version and build date
#define HWINFO_BOARD 2    // the board: its header's name (boards/<name>.h)
#define HWINFO_RECORD 16  // the first record

// Records. Hardware the board has but this firmware does not drive yet has
// a record too (HWINFO_ST_PLANNED); what the board does not have has none
// (status 0).
#define HWINFO_CPU 16          // the chip, its clock and flash
#define HWINFO_MEMORY 17       // the Z80's memory: bank 0 and the banks, where they are
#define HWINFO_PSRAM 18        // PSRAM on the QSPI bus
#define HWINFO_VIDEO 19        // the display
#define HWINFO_SERIAL 20       // the serial port: UART, pins, line settings
#define HWINFO_FLOW_CONTROL 21 // RTS/CTS
#define HWINFO_SD_CARD 22      // the SD card slot and the card in it
#define HWINFO_USB_HOST 23     // the USB host port: keyboards, sticks
#define HWINFO_USB_STICK 24    // the stick in use (USB: to the transfer tools)
#define HWINFO_LED 25          // the status LED
#define HWINFO_RTC 26          // a real-time clock (bd 1ls.6.1)
#define HWINFO_BELL 27         // a DAC for BEL (bd 1ls.7)
#define HWINFO_NEOPIXEL 28     // an RGB status LED (planned: bd 1ls.15)
// 29-31 were card detect, user button, battery: dropped (bd 1ls.22); not
// to be reused, older HWINFO users may know them.
#define HWINFO_DRIVES 32       // the disk images on drives A-H
#define HWINFO_SOUND 33        // the AY-3-8910 on the Z80's ports (bd 1ls.18.4)

#define HWINFO_ST_NONE 0      // no record
#define HWINFO_ST_PRESENT 1   // there and in use
#define HWINFO_ST_ABSENT 2    // supported, but nothing is plugged in, or this build does not use it
#define HWINFO_ST_NOT_FOUND 3 // the board should have it, but it did not answer
#define HWINFO_ST_PLANNED 4   // the board has it, this firmware does not support it yet

#define HWINFO_VERSION 1
#define HWINFO_MAX 128 // the longest item

// For the platform's hwinfo: text, or a record, in buf (HWINFO_MAX bytes,
// longer text cut); returns the length.
size_t hwinfo_text(uint8_t *buf, const char *text);
size_t hwinfo_record(uint8_t *buf, uint8_t status, const char *name, const char *detail);

typedef enum {
	MACHINE_RUNNING, // the slice ended; call machine_run again
	MACHINE_STOPPED, // the CPU stopped: machine_stop_reason says why
} machine_state_t;

// The most banks besides bank 0: port 64 reports their number in 4 bits.
#define MACHINE_MAX_BANKS 15
#define MACHINE_BANK_SIZE 49152 // a bank replaces 0000H-BFFFH

// Before machine_init: banks 1 .. count (at most MACHINE_MAX_BANKS) are
// count x MACHINE_BANK_SIZE bytes at memory (e.g. in PSRAM) instead of
// the MACHINE_BANKS built into SRAM; NULL goes back to those.
void machine_set_banks(void *memory, unsigned count);
unsigned machine_banks(void);         // as port 64 reports them
uint8_t *machine_bank(unsigned bank); // bank 1 .. machine_banks(), else NULL

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
