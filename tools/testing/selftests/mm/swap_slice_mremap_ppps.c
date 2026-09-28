// SPDX-License-Identifier: GPL-2.0
/* A moved swap PTE must retain its original 4K offset in the native folio. */
#define _GNU_SOURCE
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define LENGTH (2 * NATIVE_PAGE_SIZE)

static unsigned char *map_aligned(size_t length, unsigned char **reservation)
{
	unsigned char *mapping, *base;

	mapping = mmap(NULL, length + NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return MAP_FAILED;
	base = (unsigned char *)(((uintptr_t)mapping + NATIVE_PAGE_SIZE - 1) &
				 ~((uintptr_t)NATIVE_PAGE_SIZE - 1));
	*reservation = mapping;
	return base;
}

static bool swapped_range(const unsigned char *base)
{
	unsigned int attempt;
	size_t off;

	for (attempt = 0; attempt < 1000; attempt++) {
		for (off = 0; off < LENGTH; off += PROCESS_PAGE_SIZE)
			if (!ppps_page_swapped(base + off))
				break;
		if (off == LENGTH)
			return true;
		usleep(10000);
	}
	return false;
}

static bool test_swap_slice_mremap(void)
{
	unsigned char *src_mapping = NULL, *dst_mapping = NULL;
	unsigned char *src, *dst, *moved;
	bool ok = false;
	size_t i;

	src = map_aligned(LENGTH, &src_mapping);
	dst = map_aligned(LENGTH + NATIVE_PAGE_SIZE, &dst_mapping);
	if (src == MAP_FAILED || dst == MAP_FAILED)
		goto out;
	for (i = 0; i < LENGTH; i++)
		src[i] = 0x40 + i / PROCESS_PAGE_SIZE;
	if (madvise(src, LENGTH, MADV_PAGEOUT) || !swapped_range(src))
		goto out;
	moved = mremap(src, LENGTH, LENGTH, MREMAP_MAYMOVE | MREMAP_FIXED,
		       dst + PROCESS_PAGE_SIZE);
	if (moved == MAP_FAILED)
		goto out;
	for (i = 0; i < LENGTH; i++)
		if (moved[i] != 0x40 + i / PROCESS_PAGE_SIZE)
			goto out;
	ok = true;
out:
	if (dst_mapping)
		munmap(dst_mapping, LENGTH + 2 * NATIVE_PAGE_SIZE);
	if (src_mapping)
		munmap(src_mapping, LENGTH + NATIVE_PAGE_SIZE);
	return ok;
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(1);
	ksft_test_result(test_swap_slice_mremap(),
			 "swapped 4K slices survive a 4K-offset mremap\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
