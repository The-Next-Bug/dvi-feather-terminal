// The Z80 CPU against the instruction exercisers on the Z80TESTS disk, run
// under CP/M 2.2 on the host machine: EXZ80DOC (documented flags), EXZ80ALL
// (all flags, the undocumented X/Y too) and CPUTEST (SuperSoft Diagnostics
// II).
// Slow (minutes): not part of the default run; scripts/test.sh runs it with
// CPU_TESTS=1, or run build/test/test_cpu [program...] directly (any
// program on B:, only reporting how it ended). The console is echoed as the
// programs run; CPU_TEST_MINUTES limits each (default 30).
//
// PRELIM is not run: its LDIR with BC = 0 overwrites its own opcode, which
// stops it on a real Z80 (the opcode is fetched again every iteration) and
// on this core, so the LDDR after it shifts page zero and the BDOS up a
// byte. It passes only on z80pack's cpmsim, built with FAST_BLOCK (each
// block instruction in one go).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diskimg.h"
#include "diskio_ram.h"
#include "drives.h"
#include "ff.h"
#include "lz.h"
#include "machine.h"
#include "machine_host.h"
#include "test.h"

#define VOLUME_SECTORS (40u * 1024 * 1024 / 512) // FAT32 needs 65525+ clusters

static FATFS fs;

// A card with CPM22 on A: and Z80TESTS on B:, booted to the A> prompt.
static bool boot(void) {
	static uint8_t work[4096], window[LZ_WINDOW];
	const MKFS_PARM opt = {.fmt = FM_FAT32, .au_size = 512};
	diskio_ram_init(VOLUME_SECTORS);
	if (f_mkfs("", &opt, work, sizeof work) != FR_OK || f_mount(&fs, "", 1) != FR_OK)
		return false;
	for (unsigned i = 0; i < diskimg_count; ++i)
		if (diskimg_install(&diskimg_list[i], false, window, NULL, NULL) != DISKIMG_WRITTEN)
			return false;
	if (drives_mount(0, "CPM22") != DRIVES_OK || drives_mount(1, "Z80TESTS") != DRIVES_OK)
		return false;
	clear_screen();
	machine_init(&host_platform);
	return run_until("A>", 10) == MACHINE_RUNNING;
}

// Run B:<name> to the next A> prompt, or to CPUTEST's failure report (it
// waits there); at most minutes. False if the CPU stopped (an unknown
// instruction, a HALT) or it timed out.
static bool run_program(const char *name, double minutes) {
	char command[32];
	snprintf(command, sizeof command, "B:%s\r", name);
	// From a fresh CP/M: the last program may have stopped the CPU.
	machine_reset();
	if (run_until("A>", 10) != MACHINE_RUNNING)
		return false;
	clear_screen();
	uint64_t start = host_clock_us();
	if (!type(command))
		return false;
	static const char *const ends[] = {"\r\nA>", "TEST NUMBER", NULL};
	int which;
	machine_state_t st = run_until_any(ends, minutes * 60, &which);
	if (st == MACHINE_RUNNING && which == 1)
		run_until("\r\n", 1); // the rest of the report
	printf("\n[%s: %.1f s]\n", name, (double)(host_clock_us() - start) / 1e6);
	if (st != MACHINE_RUNNING) {
		printf("[%s: CPU stopped or timed out: %s]\n", name, machine_stop_reason());
		return false;
	}
	return true;
}

int main(int argc, char **argv) {
	const char *limit = getenv("CPU_TEST_MINUTES");
	double minutes = limit ? atof(limit) : 30;
	CHECK(boot());
	host_echo = true;
	if (argc > 1) {
		for (int i = 1; i < argc; ++i)
			CHECK(run_program(argv[i], minutes));
	} else {
		// The exercisers' line per instruction group ends in "OK" or
		// "ERROR ****".
		static const char *const exercisers[] = {"EXZ80DOC", "EXZ80ALL"};
		for (unsigned i = 0; i < sizeof exercisers / sizeof exercisers[0]; ++i) {
			CHECK(run_program(exercisers[i], minutes));
			CHECK(strstr(screen, "All tests successful") != NULL);
			CHECK(strstr(screen, "ERROR") == NULL);
		}
		CHECK(run_program("CPUTEST", minutes));
		CHECK(strstr(screen, "CPU IS Z80") != NULL);
		CHECK(strstr(screen, "CPU TESTS OK") != NULL);
	}
	drives_unmount_all();
	f_unmount("");
	diskio_ram_free();
	return tests_done();
}
