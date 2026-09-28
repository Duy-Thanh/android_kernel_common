// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest_ppps.h"

struct counts {
	unsigned long swap;
	unsigned long swap_pss;
	unsigned long rss;
};

static void die(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static struct counts read_counts(void *start, size_t len)
{
	unsigned long first = (uintptr_t)start, last = first + len, a, b;
	struct counts total = {};
	char line[512];
	int active = 0;
	FILE *file = fopen("/proc/self/smaps", "re");

	if (!file)
		die("smaps");
	while (fgets(line, sizeof(line), file)) {
		unsigned long value;

		if (sscanf(line, "%lx-%lx", &a, &b) == 2) {
			active = a < last && b > first;
			continue;
		}
		if (!active)
			continue;
		if (sscanf(line, "Swap: %lu kB", &value) == 1)
			total.swap += value;
		else if (sscanf(line, "SwapPss: %lu kB", &value) == 1)
			total.swap_pss += value;
		else if (sscanf(line, "Rss: %lu kB", &value) == 1)
			total.rss += value;
	}
	fclose(file);
	return total;
}

static unsigned char *alloc_tuple(unsigned char **area_out)
{
	unsigned char *area, *page;
	long page_size = sysconf(_SC_PAGESIZE);
	unsigned long i;

	area = mmap(NULL, 4 * NATIVE_PAGE_SIZE, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area == MAP_FAILED)
		die("mmap");
	page = (void *)(((uintptr_t)area + 2 * NATIVE_PAGE_SIZE - 1) &
			~(NATIVE_PAGE_SIZE - 1));
	if (mprotect(page, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE))
		die("prepare mapping");
	/* Compat VMAs reject THP advice because they are non-THP. */
	if (madvise(page, NATIVE_PAGE_SIZE, MADV_NOHUGEPAGE) && errno != EINVAL)
		die("disable THP");
	for (i = 0; i < NATIVE_PAGE_SIZE; i++)
		page[i] = 1 + i / page_size;
	*area_out = area;
	return page;
}

static int wait_for_swap(void *page, size_t len, unsigned long expected_pss)
{
	struct counts counts;
	int i;

	if (madvise(page, len, MADV_PAGEOUT))
		die("MADV_PAGEOUT");
	for (i = 0; i < 300; i++) {
		counts = read_counts(page, NATIVE_PAGE_SIZE);
		if (counts.swap == len / 1024)
			break;
		usleep(10000);
	}
	if (counts.swap != len / 1024) {
		fprintf(stderr, "unable to swap tuple (Swap=%lu Rss=%lu)\n",
			counts.swap, counts.rss);
		return KSFT_SKIP;
	}
	if (counts.swap_pss != expected_pss) {
		fprintf(stderr, "Swap=%lu kB SwapPss=%lu kB, expected %lu kB\n",
			counts.swap, counts.swap_pss, expected_pss);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int test_exclusive(void)
{
	unsigned char *area, *page = alloc_tuple(&area);
	unsigned long i;
	int ret;

	printf("exclusive\n");
	ret = wait_for_swap(page, NATIVE_PAGE_SIZE, 16);

	if (ret)
		return ret;
	for (i = 0; i < NATIVE_PAGE_SIZE; i++)
		if (page[i] != 1 + i / 4096) {
			fprintf(stderr, "swap-in data mismatch at %lu\n", i);
			return EXIT_FAILURE;
		}
	munmap(area, 4 * NATIVE_PAGE_SIZE);
	return EXIT_SUCCESS;
}

static int test_partial_unmap(void)
{
	unsigned char *area, *page = alloc_tuple(&area);
	int ret;

	printf("partial-unmap\n");
	if (munmap(page + 3 * 4096, 4096))
		die("partial munmap");
	ret = wait_for_swap(page, 3 * 4096, 12);
	munmap(area, 4 * NATIVE_PAGE_SIZE);
	return ret;
}

static int test_fork(void)
{
	unsigned char *area, *page = alloc_tuple(&area);
	struct counts counts;
	int ready[2], done[2], status, ret;
	char byte;
	pid_t child;

	printf("fork-shared\n");
	ret = wait_for_swap(page, NATIVE_PAGE_SIZE, 16);
	if (ret)
		return ret;
	if (pipe(ready) || pipe(done))
		die("pipe");
	child = fork();
	if (child < 0)
		die("fork");
	if (!child) {
		close(ready[0]);
		close(done[1]);
		if (write(ready[1], "r", 1) != 1 || read(done[0], &byte, 1) != 1)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	}
	close(ready[1]);
	close(done[0]);
	if (read(ready[0], &byte, 1) != 1)
		die("child ready");
	counts = read_counts(page, NATIVE_PAGE_SIZE);
	if (counts.swap != 16 || counts.swap_pss != 8) {
		fprintf(stderr, "fork-shared: Swap=%lu kB SwapPss=%lu kB\n",
			counts.swap, counts.swap_pss);
		ret = EXIT_FAILURE;
	}
	if (write(done[1], "d", 1) != 1)
		die("child done");
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		ret = EXIT_FAILURE;
	counts = read_counts(page, NATIVE_PAGE_SIZE);
	if (counts.swap != 16 || counts.swap_pss != 16) {
		fprintf(stderr, "fork-cleanup: Swap=%lu kB SwapPss=%lu kB\n",
			counts.swap, counts.swap_pss);
		ret = EXIT_FAILURE;
	}
	munmap(area, 4 * NATIVE_PAGE_SIZE);
	return ret;
}

static int test_split_vma_partial_swapin(void)
{
	unsigned char *area, *page = alloc_tuple(&area);
	struct counts swapped;
	int ret;

	printf("split-vma-partial-swapin\n");
	ret = wait_for_swap(page, NATIVE_PAGE_SIZE, 16);
	if (ret)
		return ret;
	if (mprotect(page + NATIVE_PAGE_SIZE / 2, NATIVE_PAGE_SIZE / 2,
		     PROT_READ))
		die("split swapped tuple with mprotect");

	/* Swap in only the first VMA; the second VMA keeps two swap PTEs. */
	if (page[0] != 1) {
		fprintf(stderr, "split-vma-partial-swapin: data mismatch\n");
		return EXIT_FAILURE;
	}
	swapped = read_counts(page + NATIVE_PAGE_SIZE / 2,
			      NATIVE_PAGE_SIZE / 2);
	if (swapped.swap != 8 || swapped.swap_pss != 8) {
		fprintf(stderr,
			"split-vma-partial-swapin: Swap=%lu kB SwapPss=%lu kB\n",
			swapped.swap, swapped.swap_pss);
		ret = EXIT_FAILURE;
	}
	munmap(area, 4 * NATIVE_PAGE_SIZE);
	return ret;
}

static int run_test(void)
{
	int ret;

	ppps_require_compat();
	ret = test_exclusive();
	if (ret)
		return ret;
	ret = test_partial_unmap();
	if (ret)
		return ret;
	ret = test_split_vma_partial_swapin();
	if (ret)
		return ret;
	return test_fork();
}

PPPS_COMPAT_MAIN(run_test)
