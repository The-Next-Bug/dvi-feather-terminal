#ifndef MACHINE_HOST_H
#define MACHINE_HOST_H

// The Z80 machine on the host for the tests: the console captured in a
// buffer (optionally echoed to stdout), disks through drives.c on the FatFs
// RAM volume (diskio_ram.c), no serial input.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
// What the CPU sent to the second serial port (the first 256 bytes).
extern char aux_sent[256];
extern size_t aux_len;

uint64_t host_clock_us(void);
void clear_screen(void);

// Run until the console shows text (after what was there already) or the
// machine stops; at most `seconds`.
machine_state_t run_until(const char *text, double seconds);

// The same for any of texts (NULL-terminated); *which: its index.
machine_state_t run_until_any(const char *const texts[], double seconds, int *which);

// Queue keys for the CPU; false if the queue was full.
bool type(const char *s);

#endif
