#ifndef LZ_H
#define LZ_H

// Decompressor for the disk images embedded in flash (diskimg.h). Portable
// C, host-tested; src/disks/diskimg_gen.py is the compressor.
//
// The data is raw deflate (RFC 1951: zlib's wbits -11) with a LZ_DICT-byte
// dictionary, so the output streams through a ring of that size instead of
// being held whole. The caller's LZ_WORK-byte work area (the scratch
// buffer, scratch.h) holds the ring and the Huffman tables: no other RAM.
// Deflate replaced LZ4 blocks (bd pz5): ~1.53 MB of images became ~1.2 MB.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LZ_WORK 4096 // the work area's size (scratch.h holds it)
#define LZ_DICT 2048 // the dictionary: a power of two, diskimg_gen.py's 2^11

// Receives the output in order, in chunks of up to LZ_DICT / 2 bytes.
// Returns false to stop decoding.
typedef bool (*lz_sink_t)(void *ctx, const uint8_t *data, size_t len);

// Decode src[0 .. len) into exactly out_size bytes, using work (LZ_WORK
// bytes, any alignment of 2). Returns false if the data is corrupt, does
// not make out_size bytes, or the sink stops it.
bool lz_decode(const uint8_t *src, size_t len, size_t out_size, uint8_t *work, lz_sink_t sink, void *ctx);

#endif
