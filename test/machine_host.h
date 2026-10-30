#ifndef MACHINE_HOST_H
#define MACHINE_HOST_H

// The Z80 machine on the host for the tests: the console captured in a
// buffer (optionally echoed to stdout), disks through drives.c on the FatFs
// RAM volume (diskio_ram.c), the second serial port from aux_input.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ay8910.h"
#include "machine.h"

#define HOST_SCREEN_SIZE 65536

extern const machine_platform_t host_platform;

// The console output since clear_screen, without NUL padding bytes
// (NUL-terminated; the oldest half is dropped when it fills); echoed to
// stdout while host_echo is set.
extern char screen[HOST_SCREEN_SIZE];
extern size_t screen_len;
extern bool host_echo;
// When set, the console answers a cursor position request (ESC [ 6 n) with
// this, as a terminal would.
extern const char *host_cpr_reply;
// Records 40 on ("Extra 0", ...) the port 12 device has besides its usual
// ones, for HWINFO's paging.
extern unsigned host_hwinfo_extra;
// When set, every console byte also goes here (e.g. to a terminal, whose
// replies the test types back with machine_key).
extern void (*host_console_hook)(uint8_t c);
// What the CPU sent to the second serial port (the first HOST_AUX_SIZE - 1
// bytes since aux_len was last zeroed; NUL-terminated).
#define HOST_AUX_SIZE 8192
extern char aux_sent[HOST_AUX_SIZE];
extern size_t aux_len;
// Bytes the second serial port receives, one each time the CPU asks, from
// here until the NUL (MP/M's second console; NULL: none).
extern const char *aux_input;

// The wall clock (machine.h time_get/time_set): seconds since 31.12.1977
// are (host_time_offset_us + host_clock_us()) / 1000000; starts at
// 1.1.2026. time_set counts host_time_sets.
extern uint64_t host_time_offset_us;
extern unsigned host_time_sets;

// The AY-3-8910 on the sound ports: ay8910.c's chip, and its register
// writes (register, value) since host_ay_clear, the first HOST_AY_LOG.
#define HOST_AY_LOG 4096
extern ay8910_t host_ay;
extern uint8_t host_ay_log[HOST_AY_LOG][2];
extern size_t host_ay_writes;
void host_ay_clear(void);

uint64_t host_clock_us(void);
void clear_screen(void);

// Run until the console shows text (after what was there already) or the
// machine stops; at most `seconds` (times a slack for a loaded host).
machine_state_t run_until(const char *text, double seconds);

// The same for any of texts (NULL-terminated); *which: its index.
machine_state_t run_until_any(const char *const texts[], double seconds, int *which);

// Queue keys for the CPU; false if the queue was full.
bool type(const char *s);

#endif
