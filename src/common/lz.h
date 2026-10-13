#ifndef LZ_H
#define LZ_H

// Decompressor for the disk images embedded in flash (diskimg.h). Portable
// C, host-tested; src/disks/diskimg_gen.py is the compressor.
//
// The data is an LZ4 block: sequences of a token (high nibble literal count,
// low nibble match length - 4, 15 meaning more length bytes follow, each
// added until one is below 255), the literals, then a 16-bit little-endian
// match offset; the last sequence has literals only. Offsets are at most
// LZ_WINDOW - 1, so the output can stream through a LZ_WINDOW-byte window
// instead of being held whole.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LZ_WINDOW 4096 // a power of two (scratch.h holds it)
#define LZ_MAX_OFFSET (LZ_WINDOW - 1)

// Receives the output in order, in chunks of up to LZ_WINDOW / 2 bytes.
// Returns false to stop decoding.
typedef bool (*lz_sink_t)(void *ctx, const uint8_t *data, size_t len);

// Decode src[0 .. len) into exactly out_size bytes, through window
// (LZ_WINDOW bytes of scratch). Returns false if the data is corrupt, does
// not make out_size bytes, or the sink stops it.
bool lz_decode(const uint8_t *src, size_t len, size_t out_size, uint8_t *window, lz_sink_t sink, void *ctx);

#endif
