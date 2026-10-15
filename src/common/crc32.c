#include "crc32.h"

// Bitwise: a table would cost 1 KB of RAM for the few kilobytes a second
// this checks (settings records, disk images being installed).
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
	crc = ~crc;
	while (len--) {
		crc ^= *data++;
		for (int i = 0; i < 8; ++i) {
			crc = crc >> 1 ^ (0xedb88320u & -(crc & 1));
		}
	}
	return ~crc;
}
