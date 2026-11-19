#ifndef UNICODE_H
#define UNICODE_H

// What a Unicode character shows as (bd cvc): one of the 256 glyphs
// (src/screen/font.h) and how many cells it takes. Portable C, host-tested
// (test_term). The tables (unicode_table.c, written by unicode_gen.py)
// and the lookups are cold, in flash: they run only for characters past
// Latin-1.

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint32_t first, last;
} unicode_range_t;

// Cells the character takes: 0 (a combining mark or a format character:
// dropped; also C1 controls), 1, or 2 (East Asian Wide and Fullwidth).
unsigned unicode_width(uint32_t code);

// Its glyph: Latin-1 as itself, others the closest of the glyph set (box
// drawing, blocks, look-alikes, the base letter), else GLYPH_REPLACEMENT.
// Not for ASCII (the terminal's character sets map that).
uint8_t unicode_glyph(uint32_t code);

// unicode_table.c's tables, sorted.
extern const uint16_t unicode_map_cps[];
extern const uint8_t unicode_map_glyphs[];
extern const unicode_range_t unicode_zero[];
extern const unicode_range_t unicode_wide[];
extern const unsigned unicode_map_size, unicode_zero_size, unicode_wide_size;

#endif
