// The file transfer device's files (xfer.c) on a RAM volume, without the
// Z80: test_machine runs port 10 itself.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "diskio_ram.h"
#include "ff.h"
#include "machine.h"
#include "test.h"
#include "xfer.h"

#define VOLUME_SECTORS 2048 // 1 MB: FAT12, small enough to fill

static FATFS fs;
static uint8_t buf[XFER_RECORD];

// Each test on a blank, mounted volume with nothing open.
static void fresh_volume(void) {
	xfer_reset();
	f_unmount("");
	diskio_ram_free();
	diskio_ram_init(VOLUME_SECTORS);
	static uint8_t work[4096];
	const MKFS_PARM opt = {.fmt = FM_FAT | FM_SFD};
	CHECK_EQ(f_mkfs("", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
}

// An xfer command on volume 0, with name (11 bytes, FCB form) as its
// input if given.
static uint8_t cmd(uint8_t c, const char *name) {
	if (name)
		memcpy(buf, name, XFER_NAME);
	return xfer_command(c, 0, buf);
}

static uint32_t fat_size(const char *path) {
	FILINFO info;
	return f_stat(path, &info) == FR_OK ? (uint32_t)info.fsize : UINT32_MAX;
}

// A FAT file of size bytes of `byte`, made directly.
static void put_file(const char *path, unsigned size, uint8_t byte) {
	f_mkdir("/XFER");
	FIL f;
	CHECK_EQ(f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE), FR_OK);
	for (unsigned i = 0; i < size; ++i) {
		UINT put;
		CHECK_EQ(f_write(&f, &byte, 1, &put), FR_OK);
	}
	f_close(&f);
}

// A file written through the device: records of `byte`, then close (text:
// CLOSE TEXT).
static void write_records(const char *name, unsigned records, uint8_t byte, bool text) {
	CHECK_EQ(cmd(XFER_CREATE, name), XFER_ST_OK);
	for (unsigned r = 0; r < records; ++r) {
		memset(buf, byte, XFER_RECORD);
		CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	}
	CHECK_EQ(cmd(text ? XFER_CLOSE_TEXT : XFER_CLOSE, NULL), XFER_ST_OK);
}

static uint32_t entry_size(void) {
	return buf[11] | buf[12] << 8 | buf[13] << 16 | (uint32_t)buf[14] << 24;
}

// Every name the listing gives for pattern, joined by commas.
static const char *listing(const char *pattern) {
	static char out[512];
	size_t n = 0;
	out[0] = '\0';
	for (uint8_t st = cmd(XFER_FIND_FIRST, pattern); st == XFER_ST_OK; st = cmd(XFER_FIND_NEXT, NULL)) {
		if (n)
			out[n++] = ',';
		memcpy(out + n, buf, XFER_NAME);
		n += XFER_NAME;
		out[n] = '\0';
	}
	return out;
}

static uint8_t binary_byte(unsigned i) {
	return (uint8_t)(i * 7 + i / 128); // ^Z (0x1A) included
}

// A text and a binary file through the folder and back.
static void test_round_trip(void) {
	fresh_volume();
	CHECK_EQ(cmd(XFER_FIND_FIRST, "???????????"), XFER_ST_NOT_FOUND); // no /XFER yet
	// Text: 200 characters and CP/M's ^Z padding; CLOSE TEXT cuts it there.
	CHECK_EQ(cmd(XFER_CREATE, "NOTES   TXT"), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER"), 0); // the folder is made by the first CREATE
	memset(buf, 'a', XFER_RECORD);
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	memset(buf, 0x1a, XFER_RECORD);
	memset(buf, 'b', 72);
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CLOSE_TEXT, NULL), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER/NOTES.TXT"), 200);
	// Binary: whole records, ^Z bytes and all.
	CHECK_EQ(cmd(XFER_CREATE, "DATA    BIN"), XFER_ST_OK);
	for (unsigned r = 0; r < 3; ++r) {
		for (unsigned i = 0; i < XFER_RECORD; ++i)
			buf[i] = binary_byte(r * XFER_RECORD + i);
		CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	}
	CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER/DATA.BIN"), 3 * XFER_RECORD);

	// Back: the text padded with ^Z to whole records, the binary as written.
	CHECK_EQ(cmd(XFER_OPEN, "NOTES   TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK(buf[0] == 'a' && buf[XFER_RECORD - 1] == 'a');
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK(buf[71] == 'b' && buf[72] == 0x1a && buf[XFER_RECORD - 1] == 0x1a);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF); // and stays there
	CHECK_EQ(cmd(XFER_OPEN, "DATA    BIN"), XFER_ST_OK);
	unsigned bad = 0;
	for (unsigned r = 0; r < 3; ++r) {
		CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
		for (unsigned i = 0; i < XFER_RECORD; ++i)
			bad += buf[i] != binary_byte(r * XFER_RECORD + i);
	}
	CHECK_EQ(bad, 0);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
	CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
}

// Short files: padded to a record; an empty one has none.
static void test_record_padding(void) {
	fresh_volume();
	put_file("/XFER/EMPTY.TXT", 0, 'x');
	put_file("/XFER/ONE.TXT", 1, 'x');
	put_file("/XFER/EXACT.BIN", 2 * XFER_RECORD, 'x');
	CHECK_EQ(cmd(XFER_OPEN, "EMPTY   TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
	CHECK_EQ(cmd(XFER_OPEN, "ONE     TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK(buf[0] == 'x' && buf[1] == 0x1a && buf[XFER_RECORD - 1] == 0x1a);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
	CHECK_EQ(cmd(XFER_OPEN, "EXACT   BIN"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK_EQ(buf[XFER_RECORD - 1], 'x'); // no padding record
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
}

// CLOSE TEXT cuts only at the first ^Z of the last record.
static void test_close_text(void) {
	fresh_volume();
	write_records("NOEOF   TXT", 2, 'x', true);
	CHECK_EQ(fat_size("/XFER/NOEOF.TXT"), 2 * XFER_RECORD); // no ^Z: as is
	write_records("ALLEOF  TXT", 2, 0x1a, true);
	CHECK_EQ(fat_size("/XFER/ALLEOF.TXT"), XFER_RECORD); // the last record's first byte
	write_records("EMPTY   TXT", 0, 'x', true);
	CHECK_EQ(fat_size("/XFER/EMPTY.TXT"), 0);
	// ^Z in an earlier record is data.
	CHECK_EQ(cmd(XFER_CREATE, "EARLY   TXT"), XFER_ST_OK);
	memset(buf, 0x1a, XFER_RECORD);
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	memset(buf, 'y', XFER_RECORD);
	buf[100] = 0x1a;
	buf[110] = 0x1a;
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CLOSE_TEXT, NULL), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER/EARLY.TXT"), XFER_RECORD + 100);
	// On a file being read, CLOSE TEXT only closes.
	CHECK_EQ(cmd(XFER_OPEN, "ALLEOF  TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CLOSE_TEXT, NULL), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER/ALLEOF.TXT"), XFER_RECORD);
	CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK); // nothing open: fine
}

// Patterns, sizes, and what the listing leaves out.
static void test_listing(void) {
	fresh_volume();
	put_file("/XFER/A.TXT", 5, 'x');
	put_file("/XFER/ABCDEFGH.TXT", 300, 'x');
	put_file("/XFER/B.COM", 0, 'x');
	put_file("/XFER/README", 1, 'x'); // no type
	put_file("/XFER/A_B.TXT", 1, 'x'); // _ is a CP/M delimiter
	CHECK_EQ(f_mkdir("/XFER/SUB.TXT"), FR_OK); // a folder
	put_file("/OUTSIDE.TXT", 1, 'x');

	CHECK(strcmp(listing("????????TXT"), "A       TXT,ABCDEFGHTXT") == 0);
	CHECK(strcmp(listing("A???????TXT"), "A       TXT,ABCDEFGHTXT") == 0);
	CHECK(strcmp(listing("A       ???"), "A       TXT") == 0);
	CHECK(strcmp(listing("????????   "), "README     ") == 0);
	CHECK(strcmp(listing("???????????"), "A       TXT,ABCDEFGHTXT,B       COM,README     ") == 0);
	CHECK(strcmp(listing("NONE    ???"), "") == 0);
	// Names and sizes in the entries.
	CHECK_EQ(cmd(XFER_FIND_FIRST, "ABCDEFGHTXT"), XFER_ST_OK);
	CHECK(memcmp(buf, "ABCDEFGHTXT", XFER_NAME) == 0);
	CHECK_EQ(entry_size(), 300);
	CHECK_EQ(cmd(XFER_FIND_FIRST, "B       COM"), XFER_ST_OK);
	CHECK_EQ(entry_size(), 0);
	// The end of the listing stays the end; FIND FIRST starts again.
	CHECK_EQ(cmd(XFER_FIND_NEXT, NULL), XFER_ST_NOT_FOUND);
	CHECK_EQ(cmd(XFER_FIND_NEXT, NULL), XFER_ST_NOT_FOUND);
	CHECK_EQ(cmd(XFER_FIND_FIRST, "B       COM"), XFER_ST_OK);
	// FIND NEXT with no listing started.
	xfer_reset();
	CHECK_EQ(cmd(XFER_FIND_NEXT, NULL), XFER_ST_NOT_FOUND);
}

// A listing keeps its place while its files are opened and read (XPIP
// copies as it lists).
static void test_listing_while_copying(void) {
	fresh_volume();
	put_file("/XFER/ONE.TXT", 10, '1');
	put_file("/XFER/TWO.TXT", 10, '2');
	put_file("/XFER/THREE.TXT", 10, '3');
	unsigned seen = 0;
	uint8_t name[XFER_NAME];
	for (uint8_t st = cmd(XFER_FIND_FIRST, "???????????"); st == XFER_ST_OK; st = cmd(XFER_FIND_NEXT, NULL)) {
		memcpy(name, buf, XFER_NAME);
		CHECK_EQ(cmd(XFER_OPEN, (const char *)name), XFER_ST_OK);
		CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
		seen |= 1u << (buf[0] - '1');
		CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
		CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
	}
	CHECK_EQ(seen, 7);
}

// Names: the characters CP/M takes, upper-cased, attribute bits dropped;
// blanks only as padding; ? only in patterns.
static void test_names(void) {
	fresh_volume();
	static const char ok_chars[] = "!#$%&'()-@^`{}~0Z";
	for (const char *c = ok_chars; *c; ++c) {
		char name[XFER_NAME + 1] = "X       TXT";
		name[1] = *c;
		CHECK_EQ(cmd(XFER_CREATE, name), XFER_ST_OK);
		CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
		CHECK_EQ(cmd(XFER_FIND_FIRST, name), XFER_ST_OK);
		CHECK(memcmp(buf, name, XFER_NAME) == 0);
	}
	static const char bad_chars[] = ".,;:=<>[]_*|+/\\\"";
	for (const char *c = bad_chars; *c; ++c) {
		char name[XFER_NAME + 1] = "X       TXT";
		name[1] = *c;
		CHECK_EQ(cmd(XFER_CREATE, name), XFER_ST_BAD);
	}
	CHECK_EQ(cmd(XFER_CREATE, "X\x01      TXT"), XFER_ST_BAD); // a control character
	// Full length, and no type.
	CHECK_EQ(cmd(XFER_CREATE, "ABCDEFGHXYZ"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CREATE, "NOTYPE     "), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
	CHECK(fat_size("/XFER/ABCDEFGH.XYZ") != UINT32_MAX);
	CHECK(fat_size("/XFER/NOTYPE") != UINT32_MAX);
	// Lower case and CP/M's attribute bits (high bits) are the same name.
	CHECK_EQ(cmd(XFER_OPEN, "abcdefghx\xd9\xda"), XFER_ST_OK);
	// Blanks inside, a blank name, a blank first character of the type.
	CHECK_EQ(cmd(XFER_CREATE, "AB CD   TXT"), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_CREATE, "        TXT"), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_CREATE, "ABC      XT"), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_FIND_FIRST, "        ???"), XFER_ST_BAD);
	// Wildcards only in FIND.
	CHECK_EQ(cmd(XFER_OPEN, "????????TXT"), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_CREATE, "A?      TXT"), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_DELETE, "A?      TXT"), XFER_ST_BAD);
}

// One file at a time: opening or creating another closes the first, which
// keeps what was written.
static void test_one_file_at_a_time(void) {
	fresh_volume();
	CHECK_EQ(cmd(XFER_CREATE, "FIRST   TXT"), XFER_ST_OK);
	memset(buf, 'f', XFER_RECORD);
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_CREATE, "SECOND  TXT"), XFER_ST_OK);
	CHECK_EQ(fat_size("/XFER/FIRST.TXT"), XFER_RECORD);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_BAD); // SECOND is being written
	CHECK_EQ(cmd(XFER_OPEN, "FIRST   TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_WRITE, NULL), XFER_ST_BAD); // FIRST is being read
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK_EQ(buf[0], 'f');
	// Deleting another file leaves the open one open.
	CHECK_EQ(cmd(XFER_DELETE, "SECOND  TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_EOF);
	// Deleting the open file closes it first.
	CHECK_EQ(cmd(XFER_OPEN, "FIRST   TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_DELETE, "FIRST   TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_BAD);
	CHECK_EQ(fat_size("/XFER/FIRST.TXT"), UINT32_MAX);
	CHECK_EQ(cmd(XFER_DELETE, "FIRST   TXT"), XFER_ST_NOT_FOUND);
	CHECK_EQ(cmd(XFER_OPEN, "FIRST   TXT"), XFER_ST_NOT_FOUND);
	// CREATE does not replace a file.
	write_records("KEEP    TXT", 1, 'k', false);
	CHECK_EQ(cmd(XFER_CREATE, "KEEP    TXT"), XFER_ST_EXISTS);
	CHECK_EQ(fat_size("/XFER/KEEP.TXT"), XFER_RECORD);
	// RESET closes the file and the listing.
	CHECK_EQ(cmd(XFER_OPEN, "KEEP    TXT"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_FIND_FIRST, "???????????"), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_RESET, NULL), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_BAD);
	CHECK_EQ(cmd(XFER_FIND_NEXT, NULL), XFER_ST_NOT_FOUND);
	CHECK_EQ(cmd(0xf0, NULL), XFER_ST_BAD); // no such command
}

// A full volume is an I/O error; the file keeps what fitted and can go.
static void test_volume_full(void) {
	fresh_volume();
	CHECK_EQ(cmd(XFER_CREATE, "BIG     BIN"), XFER_ST_OK);
	memset(buf, 'z', XFER_RECORD);
	unsigned records = 0;
	uint8_t st;
	while ((st = cmd(XFER_WRITE, NULL)) == XFER_ST_OK && records < 2 * VOLUME_SECTORS * 4)
		++records;
	CHECK_EQ(st, XFER_ST_IO_ERROR);
	CHECK(records > VOLUME_SECTORS * 4 / 2); // most of 1 MB
	CHECK_EQ(cmd(XFER_CLOSE, NULL), XFER_ST_OK);
	CHECK(fat_size("/XFER/BIG.BIN") >= records * XFER_RECORD);
	CHECK_EQ(cmd(XFER_DELETE, "BIG     BIN"), XFER_ST_OK);
	write_records("AFTER   TXT", 1, 'a', false); // room again
}

// No volume: nothing mounted, or a volume FatFs does not have (a USB stick
// before qur.3).
static void test_no_volume(void) {
	fresh_volume();
	memcpy(buf, "???????????", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_FIND_FIRST, 1, buf), XFER_ST_NO_VOLUME);
	memcpy(buf, "NEW     TXT", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_CREATE, 1, buf), XFER_ST_NO_VOLUME);
	f_unmount("");
	CHECK_EQ(cmd(XFER_FIND_FIRST, "???????????"), XFER_ST_NO_VOLUME);
	CHECK_EQ(cmd(XFER_CREATE, "NEW     TXT"), XFER_ST_NO_VOLUME);
	CHECK_EQ(cmd(XFER_OPEN, "NEW     TXT"), XFER_ST_NO_VOLUME);
	CHECK_EQ(cmd(XFER_DELETE, "NEW     TXT"), XFER_ST_NO_VOLUME);
	CHECK_EQ(cmd(XFER_RESET, NULL), XFER_ST_OK);
	CHECK_EQ(f_mount(&fs, "", 1), FR_OK);
}

// Volume 1 (a USB stick): its own /XFER; going away drops only what is
// open on it.
static void test_usb_volume(void) {
	fresh_volume();
	static FATFS usb_fs;
	static uint8_t work[4096];
	diskio_ram_init_drive(1, VOLUME_SECTORS);
	const MKFS_PARM opt = {.fmt = FM_FAT | FM_SFD};
	CHECK_EQ(f_mkfs("1:", &opt, work, sizeof work), FR_OK);
	CHECK_EQ(f_mount(&usb_fs, "1:", 1), FR_OK);
	put_file("/XFER/CARD.TXT", 10, 'c');

	memcpy(buf, "STICK   TXT", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_CREATE, 1, buf), XFER_ST_OK);
	memset(buf, 's', XFER_RECORD);
	CHECK_EQ(xfer_command(XFER_WRITE, 1, buf), XFER_ST_OK);
	CHECK_EQ(xfer_command(XFER_CLOSE, 1, buf), XFER_ST_OK);
	CHECK_EQ(fat_size("1:/XFER/STICK.TXT"), XFER_RECORD);
	CHECK_EQ(fat_size("/XFER/STICK.TXT"), UINT32_MAX);
	memcpy(buf, "???????????", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_FIND_FIRST, 1, buf), XFER_ST_OK);
	CHECK(memcmp(buf, "STICK   TXT", XFER_NAME) == 0);
	CHECK_EQ(xfer_command(XFER_FIND_NEXT, 1, buf), XFER_ST_NOT_FOUND);
	CHECK(strcmp(listing("???????????"), "CARD    TXT") == 0); // volume 0

	// The stick goes with a listing open on it and a file open on the card:
	// the listing is dropped, the file stays.
	memcpy(buf, "???????????", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_FIND_FIRST, 1, buf), XFER_ST_OK);
	CHECK_EQ(cmd(XFER_OPEN, "CARD    TXT"), XFER_ST_OK);
	xfer_volume_gone(1);
	CHECK_EQ(xfer_command(XFER_FIND_NEXT, 1, buf), XFER_ST_NOT_FOUND);
	CHECK_EQ(cmd(XFER_READ, NULL), XFER_ST_OK);
	CHECK_EQ(buf[0], 'c');
	// A file open on the stick is dropped.
	memcpy(buf, "STICK   TXT", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_OPEN, 1, buf), XFER_ST_OK);
	xfer_volume_gone(1);
	CHECK_EQ(xfer_command(XFER_READ, 1, buf), XFER_ST_BAD);
	xfer_volume_gone(0); // nothing open there now: fine
	CHECK_EQ(f_unmount("1:"), FR_OK);
	memcpy(buf, "???????????", XFER_NAME);
	CHECK_EQ(xfer_command(XFER_FIND_FIRST, 1, buf), XFER_ST_NO_VOLUME);
	diskio_ram_free_drive(1);
}

int main(void) {
	RUN(test_round_trip);
	RUN(test_record_padding);
	RUN(test_close_text);
	RUN(test_listing);
	RUN(test_listing_while_copying);
	RUN(test_names);
	RUN(test_one_file_at_a_time);
	RUN(test_volume_full);
	RUN(test_no_volume);
	RUN(test_usb_volume);
	xfer_reset();
	f_unmount("");
	diskio_ram_free();
	return tests_done();
}
