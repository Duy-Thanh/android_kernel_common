// SPDX-License-Identifier: GPL-2.0
/*
 * Default mmap placements of a 4K compat process are native-page aligned,
 * while an explicit hint at a free address that is only 4K aligned is
 * honored and the resulting mapping is usable.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE	(4 * PROCESS_PAGE_SIZE)
#define ALIGNMENT_TRIALS 32

static bool default_mmaps_are_native_aligned(void)
{
	void *mappings[ALIGNMENT_TRIALS];
	unsigned int mapped = 0;
	bool passed = true;

	for (mapped = 0; mapped < ALIGNMENT_TRIALS; mapped++) {
		mappings[mapped] = mmap(NULL, PROCESS_PAGE_SIZE,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mappings[mapped] == MAP_FAILED) {
			passed = false;
			break;
		}
		if ((uintptr_t)mappings[mapped] & (NATIVE_PAGE_SIZE - 1))
			passed = false;
	}
	while (mapped)
		munmap(mappings[--mapped], PROCESS_PAGE_SIZE);

	return passed;
}

static int run_test(void)
{
	unsigned char *reservation;
	unsigned char *mapping;
	unsigned char *hint;
	bool reservation_ok;
	bool hint_honored;

	ksft_print_header();
	ksft_set_plan(4);
	ksft_test_result(default_mmaps_are_native_aligned(),
			 "default mmap addresses are native-page aligned\n");

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	reservation_ok = reservation != MAP_FAILED;
	ksft_test_result(reservation_ok, "reserve a known free address range\n");
	if (!reservation_ok)
		ksft_exit_fail_msg("reservation mmap failed: %s\n",
				   strerror(errno));
	hint = reservation + PROCESS_PAGE_SIZE;
	if (munmap(reservation, RESERVE_SIZE))
		ksft_exit_fail_msg("reservation munmap failed: %s\n",
				   strerror(errno));

	mapping = mmap(hint, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	hint_honored = mapping == hint;
	ksft_test_result(hint_honored,
			 "mmap honors an available 4K-only-aligned hint\n");
	if (!hint_honored)
		ksft_print_msg("requested hint=%p, received=%p\n", hint, mapping);

	if (mapping != MAP_FAILED) {
		mapping[0] = 0x5a;
		ksft_test_result(mapping[0] == 0x5a,
				 "mapping returned for the hint is usable\n");
		munmap(mapping, PROCESS_PAGE_SIZE);
	} else {
		ksft_test_result_fail("mapping returned for the hint is usable\n");
	}
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
