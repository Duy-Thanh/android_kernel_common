// SPDX-License-Identifier: GPL-2.0
/*
 * dma_mmap_attrs() / dma_mmap_pages() map exactly the 4K slices a compat
 * process asks for: a one-slice mapping of the fixture's DMA buffer must not
 * populate the other three slices of the native page, and a mapping at DMA
 * offset 4K must start at the second slice.  The fixture is selected on the
 * command line: "attrs" or "pages".
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kselftest_ppps.h"

struct dma_mmap_case {
	const char *name;
	const char *device;
	unsigned char first_marker;
};

static const struct dma_mmap_case dma_mmap_cases[] = {
	{ "attrs", "/dev/dma_mmap_attrs_ppps", 0x61 },
	{ "pages", "/dev/dma_mmap_pages_ppps", 0x51 },
};

static sigjmp_buf fault_environment;

static void fault_handler(int signal_number)
{
	siglongjmp(fault_environment, signal_number);
}

static bool read_byte(const unsigned char *address, unsigned char *value)
{
	if (sigsetjmp(fault_environment, 1))
		return false;
	*value = *address;
	return true;
}

static int run_test(const struct dma_mmap_case *test)
{
	struct sigaction action = {
		.sa_handler = fault_handler,
	};
	unsigned char *reservation;
	unsigned char *mapping;
	unsigned char value = 0;
	bool guards_fault = true;
	unsigned int guard;
	int fd;

	ksft_print_header();
	ksft_set_plan(7);

	fd = ppps_open_fixture_or_skip(test->device, O_RDWR);
	ksft_test_result(fd >= 0, "open the dma_mmap_%s test device\n",
			 test->name);

	reservation = mmap(NULL, NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(reservation != MAP_FAILED,
			 "reserve one native 16K range as PROT_NONE\n");
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("guard reservation failed: %s\n",
				   strerror(errno));

	mapping = mmap(reservation, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_SHARED | MAP_FIXED, fd, 0);
	ksft_test_result(mapping == reservation,
			 "map only the first 4K slice with dma_mmap_%s\n",
			 test->name);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("dma_mmap_%s mmap failed: %s\n",
				   test->name, strerror(errno));

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGSEGV, &action, NULL) ||
	    sigaction(SIGBUS, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	ksft_test_result(read_byte(mapping, &value) &&
			 value == test->first_marker,
			 "the requested 4K slice is readable\n");

	for (guard = 1; guard < PPPS_SLICES; guard++) {
		if (read_byte(mapping + guard * PROCESS_PAGE_SIZE, &value)) {
			ksft_print_msg("guard %u is readable with value %#x\n",
				       guard, value);
			guards_fault = false;
		}
	}
	ksft_test_result(guards_fault,
			 "dma_mmap_%s does not populate the three guard slices\n",
			 test->name);

	munmap(reservation, NATIVE_PAGE_SIZE);

	reservation = mmap(NULL, NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("offset guard reservation failed: %s\n",
				   strerror(errno));
	mapping = mmap(reservation, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_SHARED | MAP_FIXED, fd, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping == reservation,
			 "map one 4K slice at DMA buffer offset 4K\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("offset mmap failed: %s\n", strerror(errno));
	value = 0;
	ksft_test_result(read_byte(mapping, &value) &&
			 value == test->first_marker + 1,
			 "the offset mapping starts at the second 4K slice\n");

	munmap(reservation, NATIVE_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

static const struct dma_mmap_case *find_case(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dma_mmap_cases); i++)
		if (!strcmp(dma_mmap_cases[i].name, name))
			return &dma_mmap_cases[i];
	return NULL;
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	const struct dma_mmap_case *test = NULL;

	if (mode) {
		if (!strcmp(mode, PPPS_RUN_FLAG) && argc == 3)
			test = find_case(argv[2]);
	} else if (argc == 2) {
		test = find_case(argv[1]);
	}
	if (!test) {
		fprintf(stderr, "usage: %s attrs|pages\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (mode)
		ppps_require_compat();
	else if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, test->name, NULL);
	return run_test(test);
}
