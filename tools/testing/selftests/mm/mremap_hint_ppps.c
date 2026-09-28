// SPDX-License-Identifier: GPL-2.0
/*
 * mremap(MREMAP_MAYMOVE | MREMAP_DONTUNMAP) of a 4K compat mapping honors a
 * free 4K-aligned hint, preserves every process page across the slice shift
 * and leaves the source zero-filled.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP 4
#endif

#define TEST_LENGTH (4 * PROCESS_PAGE_SIZE)
#define SOURCE_ADDRESS ((void *)0x40001000UL)
#define HINT_ADDRESS ((void *)0x50000000UL)

static int run_test(void)
{
	unsigned char *source = SOURCE_ADDRESS;
	unsigned char *hint = HINT_ADDRESS;
	unsigned char *moved;
	bool moved_data_ok = true;
	bool source_zeroed = true;
	unsigned int i;

	ksft_print_header();
	ksft_set_plan(3);

	source = mmap(source, TEST_LENGTH, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (source == MAP_FAILED)
		ksft_exit_fail_msg("source mmap failed: %s\n", strerror(errno));
	for (i = 0; i < TEST_LENGTH; i++)
		source[i] = i / PROCESS_PAGE_SIZE + 0x31;

	/* Reserve and release the range to prove the requested hint is free. */
	hint = mmap(hint, TEST_LENGTH, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (hint == MAP_FAILED)
		ksft_exit_fail_msg("hint mmap failed: %s\n", strerror(errno));
	if (munmap(hint, TEST_LENGTH))
		ksft_exit_fail_msg("hint munmap failed: %s\n", strerror(errno));

	moved = mremap(source, TEST_LENGTH, TEST_LENGTH,
		       MREMAP_MAYMOVE | MREMAP_DONTUNMAP, hint);
	if (moved == MAP_FAILED)
		ksft_exit_fail_msg("mremap failed: %s\n", strerror(errno));
	ksft_test_result(moved == hint,
			 "MREMAP_DONTUNMAP honors an available hint\n");

	for (i = 0; i < TEST_LENGTH; i++) {
		if (moved[i] != (unsigned char)(i / PROCESS_PAGE_SIZE + 0x31))
			moved_data_ok = false;
		if (source[i])
			source_zeroed = false;
	}
	ksft_test_result(moved_data_ok,
			 "cross-slice move preserves all process pages\n");
	ksft_test_result(source_zeroed,
			 "MREMAP_DONTUNMAP leaves zero-filled source pages\n");

	munmap(moved, TEST_LENGTH);
	munmap(source, TEST_LENGTH);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
