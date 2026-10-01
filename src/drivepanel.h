#ifndef DRIVEPANEL_H
#define DRIVEPANEL_H

// The Drives panel of SET-UP in CP/M mode: what is mounted on drives A-H,
// and a way to change it while CP/M runs. Portable C (FatFs, drives.h),
// host-tested; main.c opens it from SET-UP (D) over the same rows.
//
// Up/Down select a drive, Left/Right or Space step through the disk images
// on the card (and "none"), Return mounts the choices and saves them,
// Escape goes back without changes. CP/M only sees a changed drive after a
// warm boot (^C at the prompt), as with a real floppy swap.

#include <stdbool.h>
#include <stdint.h>

#include "drives.h"
#include "screen.h"

#define DRIVEPANEL_ROWS (DRIVES + 4) // title, the drives, message, help
#define DRIVEPANEL_ESC_TIMEOUT_MS 100

typedef enum {
	DRIVEPANEL_NONE,
	DRIVEPANEL_REDRAW,
	DRIVEPANEL_APPLY,  // Return: drivepanel_apply, then close
	DRIVEPANEL_CANCEL, // Escape
} drivepanel_action_t;

typedef struct {
	char images[DRIVES_LIST_MAX][DRIVES_NAME_MAX + 1]; // on the card, sorted
	unsigned n_images;
	int choice[DRIVES]; // index into images, -1: none
	int mounted[DRIVES]; // what was mounted when opened, as an index
	unsigned drive;      // selected
	uint8_t esc;         // 0, 1 after ESC, 2 inside ESC [ / ESC O
	uint32_t esc_ms;
	const char *message;
} drivepanel_t;

// Read the card's images and the drives' assignments.
void drivepanel_open(drivepanel_t *p);

drivepanel_action_t drivepanel_input(drivepanel_t *p, uint8_t c, uint32_t now_ms);
drivepanel_action_t drivepanel_tick(drivepanel_t *p, uint32_t now_ms);

// Mount the changed drives and save the assignments. Returns the number of
// drives changed; *failed is set if one could not be mounted.
unsigned drivepanel_apply(drivepanel_t *p, bool *failed);

void drivepanel_draw(const drivepanel_t *p, screen_t *s, unsigned top, uint8_t fg, uint8_t bg);

#endif
