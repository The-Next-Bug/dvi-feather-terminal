#include "scratch.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

// In .bss, so in SRAM: the firmware commit copies flash sectors through it
// and must read nothing from flash (fwupdate.h).
static uint8_t buffer[SCRATCH_SIZE] __attribute__((aligned(4)));
static bool held;

uint8_t *scratch_acquire(void) {
	if (held)
		return NULL;
	held = true;
	return buffer;
}

void scratch_release(uint8_t *buf) {
	assert(buf == buffer && held);
	(void)buf;
	held = false;
}
