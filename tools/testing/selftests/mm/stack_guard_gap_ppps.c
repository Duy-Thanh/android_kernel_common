// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process can place a one-page mapping exactly 2 MiB below a
 * grow-down VMA: the stack guard gap accepts a 4K-granular mmap hint.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define TEST_GAP	(2UL * 1024 * 1024)
#define RESERVE_SIZE	(TEST_GAP + 2 * PROCESS_PAGE_SIZE)

static int run_test(void)
{
	unsigned char *reservation;
	unsigned char *stack_addr;
	void *mapping;
	void *stack;
	bool reserved;

	ksft_print_header();
	ksft_set_plan(4);

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	reserved = reservation != MAP_FAILED;
	ksft_test_result(reserved, "reserve an isolated address range\n");
	if (!reserved)
		ksft_exit_fail_msg("reservation mmap failed: %s\n",
				   strerror(errno));
	stack_addr = reservation + TEST_GAP + PROCESS_PAGE_SIZE;
	if (munmap(reservation, RESERVE_SIZE))
		ksft_exit_fail_msg("reservation munmap failed: %s\n",
				   strerror(errno));

	stack = mmap(stack_addr, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE |
		     MAP_GROWSDOWN, -1, 0);
	ksft_test_result(stack == stack_addr, "create a grow-down VMA\n");
	if (stack == MAP_FAILED || stack != stack_addr)
		ksft_exit_fail_msg("grow-down mmap failed: %s\n",
				   strerror(errno));

	mapping = mmap(reservation, PROCESS_PAGE_SIZE, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map below the grow-down VMA\n");
	ksft_test_result(mapping == reservation,
			 "2 MiB gap accepts a 4K process mmap hint\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);
	munmap(stack, PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
