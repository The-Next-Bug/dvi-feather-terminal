/*
 * z80pack memory for the terminal's Z80 machine (machine.c): picosim's
 * banked memory (z80pack, Copyright (C) 2024 by Udo Munk, Copyright (C)
 * 2024-2025 by Thomas Eberhardt, MIT licence). Bank 0 is 64 KB; banks
 * 1 .. MACHINE_BANKS replace its lower SEGSIZ bytes when selected (MMU port
 * 64), the rest is common. The top page holds the boot ROM and ignores
 * writes.
 */

#ifndef SIMMEM_INC
#define SIMMEM_INC

#include "sim.h"
#include "simdefs.h"

#ifndef MACHINE_BANKS
#define MACHINE_BANKS 2
#endif
#define NUMSEG MACHINE_BANKS
#define SEGSIZ 49152
#define ROM_PAGE 0xff00

extern BYTE bnk0[65536], bnks[NUMSEG][SEGSIZ];
extern BYTE selbnk, *curbnk;

static inline void memwrt(WORD addr, BYTE data)
{
	if ((selbnk == 0) || (addr >= SEGSIZ)) {
		if (addr < ROM_PAGE)
			bnk0[addr] = data;
	} else {
		curbnk[addr] = data;
	}
}

static inline BYTE memrdr(WORD addr)
{
	if ((selbnk == 0) || (addr >= SEGSIZ))
		return bnk0[addr];
	else
		return curbnk[addr];
}

/* DMA devices and the simulation frame see the same memory. */
static inline void dma_write(WORD addr, BYTE data) { memwrt(addr, data); }
static inline BYTE dma_read(WORD addr) { return memrdr(addr); }
static inline void putmem(WORD addr, BYTE data) { memwrt(addr, data); }
static inline BYTE getmem(WORD addr) { return memrdr(addr); }

#endif /* !SIMMEM_INC */
