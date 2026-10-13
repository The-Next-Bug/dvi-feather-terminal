/*
 * z80pack configuration for the terminal's Z80 machine (machine.c), in
 * place of a z80pack machine's sim.h. Based on picosim's (z80pack,
 * Copyright (C) 2024-2025 by Udo Munk & Thomas Eberhardt, MIT licence).
 */

#ifndef SIM_INC
#define SIM_INC

#define DEF_CPU Z80	/* Z80 only: 8080 software runs on it too */
#define EXCLUDE_I8080
#define CPU_SPEED 0	/* unlimited unless machine_set_speed() */
#define UNDOC_INST	/* undocumented instructions */

#ifndef PRIu64
#define PRIu64 "llu"
#endif

#endif /* !SIM_INC */
