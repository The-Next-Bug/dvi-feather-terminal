#ifndef DRIVES_H
#define DRIVES_H

// The Z80 machine's disk drives, A-H: disk images in /DISKS80 on the FatFs
// volume, mounted by name. The format of an image is told by its size:
//
//   DRIVES_FLOPPY  8" SSSD, 77 tracks x 26 sectors     256256 bytes (picosim)
//   DRIVES_HD4     4 MB, 255 tracks x 128 sectors     4177920 bytes (z80pack's)
//   DRIVES_HD8     8 MB, 256 tracks x 255 sectors     8355840 bytes
//
// all of 128-byte sectors, stored track by track. The BIOS (src/cpm) reads
// a drive's format from the controller to pick its disk parameters.
//
// The image files stay open while mounted; drives_poll flushes written
// data a quarter of a second after the first unflushed write, drives_flush
// at once. The assignments are kept in /DISKS80/DRIVES.CFG. Portable C
// (FatFs), host-tested.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DRIVES 8
#define DRIVES_NAME_MAX 8 // 8.3 base names
#define DRIVES_SECTOR 128
#define DRIVES_LIST_MAX 64 // images drives_list returns at most

typedef enum {
	DRIVES_NONE,   // nothing mounted (or not an image)
	DRIVES_FLOPPY,
	DRIVES_HD4,
	DRIVES_HD8,
	DRIVES_FORMATS
} drives_format_t;

typedef struct {
	const char *name;       // "8\" floppy", "4 MB disk", "8 MB disk"
	const char *short_name; // "FD", "4MB", "8MB"
	unsigned tracks, sectors; // sectors numbered from 1
	uint32_t size;
} drives_geometry_t;

// The geometry of format f (DRIVES_FLOPPY .. DRIVES_HD8), else NULL.
const drives_geometry_t *drives_geometry(drives_format_t f);

// The format of an image of size bytes, DRIVES_NONE if none.
drives_format_t drives_format_of_size(uint32_t size);

typedef enum {
	DRIVES_OK,
	DRIVES_NOT_FOUND,  // no such image
	DRIVES_BAD_IMAGE,  // not one of the image sizes
	DRIVES_IN_USE,     // mounted on another drive
	DRIVES_EXISTS,     // drives_create: the name is taken
	DRIVES_IO_ERROR,
} drives_result_t;

// Mount /DISKS80/<name>.DSK (name case-insensitive, without the extension)
// on drive (0-7), unmounting what was there.
drives_result_t drives_mount(unsigned drive, const char *name);
void drives_unmount(unsigned drive);
void drives_unmount_all(void);

// The mounted image's name (upper case), or NULL; its format.
const char *drives_name(unsigned drive);
drives_format_t drives_format(unsigned drive);

// How many images have been mounted on drive (mod 256): it changes with
// every mount, so a system can tell another image from the one it logged
// in, even of the same format (port 8).
uint8_t drives_mounts(unsigned drive);

// Progress of a long write: done of total bytes.
typedef void (*drives_progress_t)(void *ctx, uint32_t done, uint32_t total);

// Create /DISKS80/<name>.DSK as a blank (formatted, empty) disk of format f,
// writing buf_size bytes at a time from buf (scratch; a multiple of 512
// bytes and at least 512: bigger is faster, FatFs passes whole sectors on
// to the card). progress, if not NULL, is called after each write.
drives_result_t drives_create(const char *name, drives_format_t f, uint8_t *buf, size_t buf_size,
	drives_progress_t progress, void *ctx);

// The format of image name in /DISKS80 (by its size), DRIVES_NONE if it is
// not there or not an image.
drives_format_t drives_image_format(const char *name);

// name (an image's, without .DSK) in upper case; false if it cannot be
// one (empty, over 8 characters, or . / \\ : * ? or a blank in it).
bool drives_normalise(const char *name, char upper[DRIVES_NAME_MAX + 1]);

// The images in /DISKS80 (base names, sorted), up to max (drivecfg.c, as
// the configuration and sets below).
unsigned drives_list(char names[][DRIVES_NAME_MAX + 1], unsigned max);

// Sector I/O for the FDC: track and sector within the drive's geometry
// (sectors from 1). Return FDC status codes (sd-fdc.h: 0 = OK).
uint8_t drives_read(unsigned drive, unsigned track, unsigned sector, uint8_t buf[DRIVES_SECTOR]);
uint8_t drives_write(unsigned drive, unsigned track, unsigned sector, const uint8_t buf[DRIVES_SECTOR]);

// Flush written data once it has waited long enough (now_ms: a
// millisecond clock); drives_flush: now. Returns false on a write error.
void drives_poll(uint32_t now_ms);
bool drives_flush(void);

// The drives accessed since the last call, bit d for drive d (for activity
// lights).
uint8_t drives_activity(void);

// /DISKS80/DRIVES.CFG: save the assignments; load them (unmounting all
// first). Without the file, load mounts CPM22 on A and Z80TESTS on B, if
// there, and returns false.
void drives_save_config(void);
bool drives_load_config(void);

// Named drive sets (bd f8w.6): /DISKS80/SETS/<SET>.SET, a set of images
// for drives A-H in DRIVES.CFG's form (A=CPM22, a line a drive, A= for
// none), so they can be written on a PC too. SET-UP's Drives panel picks,
// saves and removes them; the monitor only knows the current mounts.
#define DRIVES_SETS_MAX 16 // sets drives_sets returns at most

// A set name: 1-8 upper-case letters and digits.
bool drives_set_name_ok(const char *set);

// The sets on the card (sorted), up to max.
unsigned drives_sets(char names[][DRIVES_NAME_MAX + 1], unsigned max);

// A set's images ("" for none; names that are not image names are left
// out), whether or not they are on the card. False if it cannot be read.
bool drives_set_read(const char *set, char images[DRIVES][DRIVES_NAME_MAX + 1]);

// Write (create or replace) a set, making /DISKS80/SETS if needed.
bool drives_set_write(const char *set, const char images[DRIVES][DRIVES_NAME_MAX + 1]);

bool drives_set_remove(const char *set);

#endif
