#ifndef CRC32_H
#define CRC32_H

// CRC-32 (IEEE 802.3, reflected, as zlib's crc32). Portable C.

#include <stddef.h>
#include <stdint.h>

// Continue crc (0 to start) over data.
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif
