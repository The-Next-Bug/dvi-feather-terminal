/*
 * z80pack platform functions for the terminal's Z80 machine: time and
 * waiting go through machine.c, which also ends the CPU's time slices and
 * raises the 60 Hz timer interrupt from them.
 */

#ifndef SIMPORT_INC
#define SIMPORT_INC

#include <stdint.h>

#include "sim.h"
#include "simdefs.h"

extern uint64_t machine_clock_us(void);
extern void machine_sleep_us(unsigned long time);
extern void machine_halt_wait_ms(unsigned ms);

static inline uint64_t get_clock_us(void) { return machine_clock_us(); }
static inline void sleep_for_us(unsigned long time) { machine_sleep_us(time); }
/* Only HALT waits in milliseconds (without a front panel). */
static inline void sleep_for_ms(unsigned time) { machine_halt_wait_ms(time); }

#endif /* !SIMPORT_INC */
