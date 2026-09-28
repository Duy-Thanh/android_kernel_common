// SPDX-License-Identifier: GPL-2.0
/*
 * munmap of three 4K process pages inside one native page of a compat
 * process's anonymous mapping removes exactly those pages and leaves the
 * adjacent fourth page mapped with its contents.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAPPING_SIZE		(8 * PROCESS_PAGE_SIZE)
#define UNMAP_SIZE		(3 * PROCESS_PAGE_SIZE)

static bool is_mapped(void *address)
{
	unsigned char vector;

	errno = 0;
	return !mincore(address, PROCESS_PAGE_SIZE, &vector);
}

static int run_test(void)
{
	unsigned char *mapping;
	unsigned char *aligned;
	unsigned int page;
	bool first_three_unmapped = true;

	ksft_print_header();
	ksft_set_plan(4);

	mapping = mmap(NULL, MAPPING_SIZE + NATIVE_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		       -1, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "create an anonymous mapping\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	aligned = (unsigned char *)(((uintptr_t)mapping +
				    NATIVE_PAGE_SIZE - 1) &
				   ~(NATIVE_PAGE_SIZE - 1));
	for (page = 0; page < 4; page++)
		aligned[page * PROCESS_PAGE_SIZE] = 0x40 + page;

	ksft_test_result(!munmap(aligned, UNMAP_SIZE),
			 "unmap exactly three 4K process pages\n");

	for (page = 0; page < 3; page++)
		first_three_unmapped &= !is_mapped(aligned +
						  page * PROCESS_PAGE_SIZE);
	ksft_test_result(first_three_unmapped,
			 "the requested three pages are unmapped\n");

	ksft_test_result(is_mapped(aligned + 3 * PROCESS_PAGE_SIZE) &&
			 aligned[3 * PROCESS_PAGE_SIZE] == 0x43,
			 "munmap does not remove the adjacent fourth page\n");

	munmap(mapping, MAPPING_SIZE + NATIVE_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
