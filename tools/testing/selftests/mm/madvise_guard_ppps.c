// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_GUARD_INSTALL / MADV_GUARD_REMOVE operate on individual 4K process
 * pages of a compat process: guarded pages fault, unadvised siblings stay
 * readable and removed guards fault back in as zero pages.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#ifndef MADV_GUARD_INSTALL
#define MADV_GUARD_INSTALL 102
#endif
#ifndef MADV_GUARD_REMOVE
#define MADV_GUARD_REMOVE 103
#endif

#define MAPPING_SIZE	(4 * PROCESS_PAGE_SIZE)

static bool read_faults(const unsigned char *address)
{
	pid_t child = fork();
	int status;

	if (child < 0)
		return false;
	if (!child) {
		unsigned char value = *address;

		_exit(value == 0xff ? EXIT_FAILURE : EXIT_SUCCESS);
	}
	if (waitpid(child, &status, 0) != child)
		return false;
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

static int run_test(void)
{
	unsigned char *mapping;
	bool installed;
	bool removed;

	ksft_print_header();
	ksft_set_plan(5);

	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	memset(mapping, 0x5a, MAPPING_SIZE);

	installed = !madvise(mapping + PROCESS_PAGE_SIZE, 3 * PROCESS_PAGE_SIZE,
			     MADV_GUARD_INSTALL);
	ksft_test_result(installed,
			 "install three process-page guard markers\n");
	ksft_test_result(mapping[0] == 0x5a,
			 "unadvised sibling remains accessible\n");
	ksft_test_result(read_faults(mapping + 2 * PROCESS_PAGE_SIZE),
			 "middle process-page guard faults\n");

	removed = !madvise(mapping + PROCESS_PAGE_SIZE, 3 * PROCESS_PAGE_SIZE,
			   MADV_GUARD_REMOVE);
	ksft_test_result(removed, "remove three process-page guards\n");
	ksft_test_result(mapping[PROCESS_PAGE_SIZE] == 0 &&
			 mapping[2 * PROCESS_PAGE_SIZE] == 0 &&
			 mapping[3 * PROCESS_PAGE_SIZE] == 0,
			 "removed guards fault back as zero pages\n");

	munmap(mapping, MAPPING_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
