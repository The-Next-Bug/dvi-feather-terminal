#ifndef SPLASH_H
#define SPLASH_H

// The startup logo: an image made by src/logo_gen.py, unpacked into rows the
// DVI encoder reads directly (video.c shows them in place of the text rows).
// Portable C (no pico-sdk), host-tested.
//
// An image has one plane of 2-bit levels, run-length encoded (bytes
// `level << 6 | (run - 1)`, row by row), with at most two levels per 8-pixel
// group; it is shown in each colour plane of `planes`, the others black. So
// a logo is one hue (or grey) in 4 levels, like the green of the default.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raster.h"

// The logo covers the text rows; the status line stays below it.
#define SPLASH_HEIGHT (RASTER_ROW_HEIGHT * (SCREEN_ROWS - 1))

typedef struct {
	uint16_t width, height; // width a multiple of 8
	uint8_t planes;         // bit n: shown in plane n (0 blue, 1 green, 2 red)
	const uint8_t *data;
	size_t size;
} splash_image_t;

// One scanline for the encoder: 800 pixels of set/clear bits plus the 4
// bytes it reads past them, and a level pair per 8-pixel group (packed as in
// raster_line_t).
typedef struct {
	uint8_t bits[RASTER_BYTES + 4];
	uint32_t colour[RASTER_COLOUR_WORDS];
} splash_row_t;

// Unpack img, centred, into rows[SPLASH_HEIGHT]; everything else is black.
// Returns false (and leaves the rows black) if img does not fit or its data
// is malformed.
bool splash_unpack(splash_row_t *rows, const splash_image_t *img);

// The logo built into the firmware (generated from LOGO_IMAGE).
extern const splash_image_t splash_logo;

#endif
