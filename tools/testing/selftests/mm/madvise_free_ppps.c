// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_FREE over an isolated 32-page anonymous VMA of a 4K compat process
 * marks the pages lazy-free and they keep their data until reclaim.  Lazy
 * freeing is a property of the native 16K page backing a packed tuple: a
 * range covering only some of a tuple's 4K slices leaves it resident.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define TEST_PAGES	32
#define MAPPING_SIZE	(TEST_PAGES * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(MAPPING_SIZE + 2 * NATIVE_PAGE_SIZE)

static bool vma_span(const void *address, unsigned long *span)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end;
	char *line = NULL;
	size_t capacity = 0;
	bool found = false;
	FILE *maps;

	maps = fopen("/proc/self/maps", "re");
	if (!maps)
		return false;
	while (getline(&line, &capacity, maps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) != 2)
			continue;
		if (target >= start && target < end) {
			*span = end - start;
			found = true;
			break;
		}
	}
	free(line);
	fclose(maps);
	return found;
}

static int run_test(void)
{
	unsigned long lazyfree = 0;
	unsigned long span = 0;
	unsigned char *mapping;
	unsigned char *reservation;
	uintptr_t base;
	bool preserved = true;
	unsigned int i;
	int ret;

	ksft_print_header();
	ksft_set_plan(6);

	/* A native-aligned VMA with guard pages on both sides. */
	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	base = ((uintptr_t)reservation + 2 * NATIVE_PAGE_SIZE - 1) &
	       ~(uintptr_t)(NATIVE_PAGE_SIZE - 1);
	mapping = reservation == MAP_FAILED ? MAP_FAILED :
		mmap((void *)base, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapping != MAP_FAILED) {
		madvise(mapping, MAPPING_SIZE, MADV_NOHUGEPAGE);
		for (i = 0; i < TEST_PAGES; i++)
			mapping[i * PROCESS_PAGE_SIZE] = 0x40 + i;
	}
	ksft_test_result(mapping != MAP_FAILED,
			 "map and populate anonymous process pages\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	ksft_test_result(vma_span(mapping, &span) && span == MAPPING_SIZE,
			 "target is an isolated %lu-byte VMA (%lu bytes)\n",
			 MAPPING_SIZE, span);

	ret = madvise(mapping, PROCESS_PAGE_SIZE, MADV_FREE);
	ksft_test_result(!ret && ppps_smaps_bytes(mapping, 1, "LazyFree", &lazyfree) &&
			 lazyfree == 0,
			 "MADV_FREE on one slice leaves its 16K tuple resident (%lu bytes)\n",
			 lazyfree);

	ret = madvise(mapping, MAPPING_SIZE, MADV_FREE);
	ksft_test_result(!ret, "mark the process pages MADV_FREE\n");
	ksft_test_result(ppps_smaps_bytes(mapping, 1, "LazyFree", &lazyfree) &&
			 lazyfree >= MAPPING_SIZE - PROCESS_PAGE_SIZE,
			 "account lazy-free process pages (%lu bytes)\n",
			 lazyfree);

	for (i = 0; i < TEST_PAGES; i++) {
		if (mapping[i * PROCESS_PAGE_SIZE] != (unsigned char)(0x40 + i)) {
			preserved = false;
			break;
		}
	}
	ksft_test_result(preserved,
			 "lazy-free pages retain data before reclaim\n");

	munmap(reservation, RESERVE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
