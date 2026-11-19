#include "unicode.h"

#include "font.h"

static bool in_ranges(const unicode_range_t *r, unsigned n, uint32_t code) {
	unsigned lo = 0, hi = n;
	while (lo < hi) {
		unsigned mid = (lo + hi) / 2;
		if (code < r[mid].first) {
			hi = mid;
		} else if (code > r[mid].last) {
			lo = mid + 1;
		} else {
			return true;
		}
	}
	return false;
}

unsigned unicode_width(uint32_t code) {
	if (code < 0x300) {
		return code >= 0x80 && code < 0xa0 ? 0 : 1;
	}
	if (in_ranges(unicode_zero, unicode_zero_size, code)) {
		return 0;
	}
	return in_ranges(unicode_wide, unicode_wide_size, code) ? 2 : 1;
}

uint8_t unicode_glyph(uint32_t code) {
	if (code < 0x100) {
		return code >= 0xa0 ? (uint8_t)code : GLYPH_REPLACEMENT;
	}
	if (code >= 0xff01 && code <= 0xff5e) { // Fullwidth ASCII
		return (uint8_t)(code - 0xfee0);
	}
	unsigned lo = 0, hi = unicode_map_size;
	while (lo < hi) {
		unsigned mid = (lo + hi) / 2;
		if (code < unicode_map_cps[mid]) {
			hi = mid;
		} else if (code > unicode_map_cps[mid]) {
			lo = mid + 1;
		} else {
			return unicode_map_glyphs[mid];
		}
	}
	return GLYPH_REPLACEMENT;
}
