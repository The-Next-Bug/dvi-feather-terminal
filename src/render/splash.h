#ifndef SPLASH_H
#define SPLASH_H

// The startup logo: an image made by src/render/logo_gen.py, decoded a scanline at
// a time into the renderer's pixels (video.c shows it in place of the text
// rows). Portable C (no pico-sdk), host-tested.
//
// An image has one plane of 2-bit levels, run-length encoded (bytes
// `level << 6 | (run - 1)`, the rows one after another, runs crossing row
// ends); it is shown in each colour plane of `planes`, the others black. So
// a logo is one hue (or grey) in 4 levels, like the green of the default,
// and the tint (splash_tint) gives each level its colour.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "raster.h"

// The logo covers the text rows; the status line stays below it.
#define SPLASH_HEIGHT (RASTER_ROW_HEIGHT * (SCREEN_ROWS - 1))

typedef struct {
	uint16_t width, height; // width even
	uint8_t planes;         // bit n: shown in plane n (0 blue, 1 green, 2 red)
	const uint8_t *data;
	size_t size;
} splash_image_t;

// Whether img fits (at most RASTER_ACTIVE_WIDTH x SPLASH_HEIGHT, width
// even) and its runs cover exactly width x height pixels.
bool splash_check(const splash_image_t *img);

// Decoding state. Copy a fresh one (from splash_decoder_init) at the start
// of each frame, then compose scanlines 0, 1, ... in order: the runs are
// read through once per frame, with no index of where rows start.
typedef struct {
	const uint8_t *data, *end;
	uint16_t width, height, x0, y0; // the image, centred
	unsigned level, left;           // the current run
} splash_decoder_t;

// A decoder for img (checked) reading its runs from data: img->data or a
// copy of it, so the decoder never touches img itself.
splash_decoder_t splash_decoder_init(const splash_image_t *img, const uint8_t *data);

// Scanline y (0 .. SPLASH_HEIGHT - 1, called in order) into
// RASTER_ACTIVE_WIDTH pixels: colours[level] inside the image, 0 (black)
// around it.
void splash_compose(uint16_t *px, splash_decoder_t *d, unsigned y, const uint16_t colours[4]);

// How the image's levels are shown per plane (0 blue, 1 green, 2 red):
// SPLASH_PLANE_OFF (black), or lowered by `shift` steps (0: as encoded).
#define SPLASH_PLANE_OFF 0xff

typedef struct {
	uint8_t shift[3];
} splash_tint_t;

// The tint that shows img in the hue of text colour fg (RGB222): fg's
// brightest plane at the image's own levels, the others lower in the same
// proportion, unlit planes off. A grey fg leaves img in its own planes (a
// full-colour scheme), or with mono shows it in grey.
splash_tint_t splash_tint(const splash_image_t *img, uint8_t fg, bool mono);

// The colour (RGB222) a tint gives image level `level` (0-3).
uint8_t splash_tint_colour(const splash_tint_t *t, unsigned level);

// The logo built into the firmware (generated from LOGO_IMAGE).
extern const splash_image_t splash_logo;

#endif
