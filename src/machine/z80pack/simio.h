/*
 * z80pack I/O port tables for the terminal's Z80 machine (machine.c).
 * From picosim (z80pack, Copyright (C) 2024-2025 by Udo Munk & Thomas
 * Eberhardt, MIT license).
 */

#ifndef SIMIO_INC
#define SIMIO_INC

#include "sim.h"
#include "simdefs.h"

#define IO_DATA_UNUSED	0xff	/* data returned on unused ports */

extern in_func_t *const port_in[256];
extern out_func_t *const port_out[256];

extern void init_io(void);
extern void exit_io(void);

#endif /* !SIMIO_INC */
