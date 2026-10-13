#ifndef SCRATCH_H
#define SCRATCH_H

// One scratch buffer for the jobs that need a few KB now and then and never
// run together (bd 3of): the monitor's disk image installs (the LZ window)
// and blank disks, and the firmware update (a UF2 block while checking and
// staging, a flash sector's copy while committing). Portable C.

#include <stdint.h>

#define SCRATCH_SIZE 4096

// Borrow the buffer for one job: NULL while another job holds it.
uint8_t *scratch_acquire(void);

// Give it back (the pointer scratch_acquire returned).
void scratch_release(uint8_t *buf);

#endif
