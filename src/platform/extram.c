#include "extram.h"

#if PSRAM_BANKS

#include "hardware/psram.h"

static size_t taken; // bytes handed out by extram_take

size_t extram_retime(void) {
	if (!psram_is_available()) {
		return 0;
	}
	// The SDK's defaults for its APS6404 (133 MHz, 8 us max select, 18 ns
	// deselect), for the clock now: divisor 4 at 400 MHz.
	if (psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ, PICO_DEFAULT_PSRAM_MAX_SELECT,
			PICO_DEFAULT_PSRAM_MIN_DESELECT) != PICO_OK ||
		psram_reinitialize() != PICO_OK) {
		return 0;
	}
	return psram_get_size();
}

size_t extram_size(void) {
	return psram_get_size();
}

uint8_t *extram_base(void) {
	// The CS1 window starts where the largest flash could end.
	return psram_get_size() ? (uint8_t *)(XIP_BASE + flash_devinfo_size_to_bytes(FLASH_DEVINFO_SIZE_MAX)) : NULL;
}

uint8_t *extram_take(size_t size) {
	if (size > extram_left()) {
		return NULL;
	}
	uint8_t *at = extram_base() + taken;
	taken += size;
	return at;
}

size_t extram_left(void) {
	size_t size = psram_get_size();
	return size > taken ? size - taken : 0;
}

#else

size_t extram_retime(void) {
	return 0;
}

size_t extram_size(void) {
	return 0;
}

uint8_t *extram_base(void) {
	return NULL;
}

uint8_t *extram_take(size_t size) {
	(void)size;
	return NULL;
}

size_t extram_left(void) {
	return 0;
}

#endif
