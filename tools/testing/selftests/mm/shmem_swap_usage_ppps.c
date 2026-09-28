// SPDX-License-Identifier: GPL-2.0
/*
 * smaps "Swap:" of a 4K compat process's shmem VMA counts only the swapped
 * 4K slices the VMA itself maps, for shared and private mappings alike.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define VMA_SIZE	(4 * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(16 * PROCESS_PAGE_SIZE)
#define FILE_SIZE	(16 * PROCESS_PAGE_SIZE)
#define POLL_ATTEMPTS	100

static bool swap_info(unsigned long *total_bytes, unsigned long *free_bytes)
{
	unsigned long total_kb = 0;
	unsigned long free_kb = 0;
	char *line = NULL;
	size_t capacity = 0;
	FILE *meminfo;

	meminfo = fopen("/proc/meminfo", "re");
	if (!meminfo)
		return false;
	while (getline(&line, &capacity, meminfo) >= 0) {
		if (sscanf(line, "SwapTotal: %lu kB", &total_kb) == 1)
			continue;
		if (sscanf(line, "SwapFree: %lu kB", &free_kb) == 1)
			continue;
	}
	free(line);
	fclose(meminfo);
	*total_bytes = total_kb * 1024;
	*free_bytes = free_kb * 1024;
	return true;
}

static bool vma_swap_bytes(const void *address, unsigned long *swap_bytes)
{
	return ppps_smaps_bytes(address, 1, "Swap", swap_bytes);
}

/*
 * Page out @mapping and wait until its smaps Swap: shows the swapped bytes.
 * (SwapFree is not a usable signal: the per-CPU swap slot caches take slots
 * from the free count long before a page is written to them.)
 */
static bool page_out_mapping(void *mapping, unsigned long *swap_bytes)
{
	unsigned int attempt;

	if (madvise(mapping, VMA_SIZE, MADV_PAGEOUT))
		return false;
	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!vma_swap_bytes(mapping, swap_bytes))
			return false;
		if (*swap_bytes)
			return true;
		usleep(10000);
	}
	return false;
}

static int run_test(void)
{
	unsigned long total_swap;
	unsigned long free_swap;
	unsigned long outside_swap = 0;
	unsigned long target_swap = 0;
	unsigned long slice_swap = 0;
	unsigned long private_swap = 0;
	unsigned char *reservation;
	unsigned char *outside;
	unsigned char *target;
	bool paged_out;
	size_t offset;
	int fd;

	ksft_print_header();
	ksft_set_plan(6);
	if (!swap_info(&total_swap, &free_swap))
		ksft_exit_fail_msg("could not read /proc/meminfo\n");
	if (!total_swap)
		ksft_exit_skip("no swap device is active\n");
	ksft_test_result(true, "swap is active\n");

	fd = memfd_create("shmem-swap-usage-ppps", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, FILE_SIZE))
		ksft_exit_fail_msg("create memfd failed: %s\n", strerror(errno));
	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("reserve address range failed: %s\n",
				   strerror(errno));
	target = mmap(reservation, VMA_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, 0);
	outside = mmap(reservation + 2 * VMA_SIZE, VMA_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
		       fd, VMA_SIZE);
	if (target == MAP_FAILED || outside == MAP_FAILED)
		ksft_exit_fail_msg("map shmem ranges failed: %s\n",
				   strerror(errno));
	ksft_test_result(true, "map separate inside and outside shmem ranges\n");

	for (offset = 0; offset < VMA_SIZE; offset += PROCESS_PAGE_SIZE)
		outside[offset] = 0x40 + offset / PROCESS_PAGE_SIZE;
	paged_out = page_out_mapping(outside, &outside_swap);
	ksft_test_result(paged_out, "page out only the range outside the target VMA\n");
	if (!vma_swap_bytes(target, &target_swap))
		ksft_exit_fail_msg("could not read target VMA swap usage\n");
	ksft_test_result(paged_out && !target_swap,
			 "exclude swapped shmem pages outside the target VMA\n");
	ksft_print_msg("outside Swap=%lu target Swap=%lu bytes\n",
		       outside_swap, target_swap);

	munmap(outside, VMA_SIZE);
	outside = mmap(reservation + 2 * VMA_SIZE, PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
		       fd, VMA_SIZE);
	if (outside == MAP_FAILED)
		ksft_exit_fail_msg("map swapped shmem slice failed: %s\n",
				   strerror(errno));
	if (!vma_swap_bytes(outside, &slice_swap))
		ksft_exit_fail_msg("could not read shmem slice swap usage\n");
	ksft_test_result(slice_swap == PROCESS_PAGE_SIZE,
			 "account only the mapped process-page shmem slice\n");
	ksft_print_msg("single-slice Swap=%lu bytes\n", slice_swap);

	munmap(outside, PROCESS_PAGE_SIZE);
	outside = mmap(reservation + 2 * VMA_SIZE, PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED,
		       fd, VMA_SIZE);
	if (outside == MAP_FAILED)
		ksft_exit_fail_msg("map private swapped shmem slice failed: %s\n",
				   strerror(errno));
	if (!vma_swap_bytes(outside, &private_swap))
		ksft_exit_fail_msg("could not read private shmem swap usage\n");
	ksft_test_result(private_swap == PROCESS_PAGE_SIZE,
			 "account a private process-page shmem hole\n");
	ksft_print_msg("private single-slice Swap=%lu bytes\n", private_swap);

	munmap(reservation, RESERVE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
