// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_PAGEOUT of a 4K compat process's anonymous pages installs swap PTEs
 * that preserve their contents, and a swapin after a UFFD write-protect mode
 * change drops the stale protection.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define UFFD_REPRO_MAPPING_SIZE	(16UL * 1024 * 1024)
#define TEST_PAGES	32
#define MAPPING_SIZE	(TEST_PAGES * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(MAPPING_SIZE + 2 * PROCESS_PAGE_SIZE)
#define POLL_ATTEMPTS	200

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

static bool vma_usage_bytes(const void *address, unsigned long *rss_bytes,
			    unsigned long *swap_bytes)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end, value_kb;
	char *line = NULL;
	size_t capacity = 0;
	bool found_rss = false;
	bool found_swap = false;
	bool in_target = false;
	FILE *smaps;

	smaps = fopen("/proc/self/smaps", "re");
	if (!smaps)
		return false;
	while (getline(&line, &capacity, smaps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			if (in_target)
				break;
			in_target = target >= start && target < end;
			continue;
		}
		if (!in_target)
			continue;
		if (sscanf(line, "Rss: %lu kB", &value_kb) == 1) {
			*rss_bytes = value_kb * 1024;
			found_rss = true;
		} else if (sscanf(line, "Swap: %lu kB", &value_kb) == 1) {
			*swap_bytes = value_kb * 1024;
			found_swap = true;
		}
	}
	free(line);
	fclose(smaps);
	return found_rss && found_swap;
}

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

static bool page_out_mapping(void *mapping, unsigned long *rss_bytes,
			     unsigned long *swap_bytes,
			     unsigned long *device_bytes)
{
	unsigned long total;
	unsigned long free_before;
	unsigned long free_after;
	unsigned int attempt;

	if (!swap_info(&total, &free_before))
		return false;
	if (madvise(mapping, MAPPING_SIZE, MADV_PAGEOUT))
		return false;
	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!swap_info(&total, &free_after) ||
		    !vma_usage_bytes(mapping, rss_bytes, swap_bytes))
			return false;
		*device_bytes = free_after < free_before ? free_before - free_after : 0;
		if (*swap_bytes >= MAPPING_SIZE / 2 &&
		    *rss_bytes <= MAPPING_SIZE / 2)
			return true;
		usleep(10000);
	}
	return false;
}

static bool swapin_after_uffd_wp_mode_change(void)
{
	struct uffdio_writeprotect writeprotect = {
		.range.len = PROCESS_PAGE_SIZE,
		.mode = UFFDIO_WRITEPROTECT_MODE_WP,
	};
	struct uffdio_register registration = {
		.range.len = 2 * PROCESS_PAGE_SIZE,
		.mode = UFFDIO_REGISTER_MODE_WP,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP,
	};
	unsigned long rss = 0;
	unsigned long swap = 0;
	size_t mapping_size = UFFD_REPRO_MAPPING_SIZE + NATIVE_PAGE_SIZE;
	unsigned char *mapping = MAP_FAILED;
	unsigned char *base;
	unsigned int attempt;
	bool passed = false;
	const char *failure = "mmap";
	int uffd = -1;

	mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		goto out;
	base = (unsigned char *)(((unsigned long)mapping +
						 NATIVE_PAGE_SIZE - 1) &
					~(NATIVE_PAGE_SIZE - 1)) +
		UFFD_REPRO_MAPPING_SIZE - NATIVE_PAGE_SIZE;

	failure = "mlock/munlock";
	if (mlock(base + PROCESS_PAGE_SIZE, 3 * PROCESS_PAGE_SIZE))
		goto out;
	base[PROCESS_PAGE_SIZE] = 0x41;
	base[2 * PROCESS_PAGE_SIZE] = 0x42;
	base[3 * PROCESS_PAGE_SIZE] = 0x43;
	if (munlock(base + PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE))
		goto out;

	failure = "madvise";
	if (madvise(base, 3 * PROCESS_PAGE_SIZE, MADV_PAGEOUT))
		goto out;
	failure = "swapout";
	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!vma_usage_bytes(base + PROCESS_PAGE_SIZE, &rss, &swap))
			goto out;
		if (swap >= 2 * PROCESS_PAGE_SIZE && rss <= PROCESS_PAGE_SIZE)
			break;
		usleep(10000);
	}
	if (attempt == POLL_ATTEMPTS)
		goto out;

	failure = "UFFDIO_API";
	uffd = syscall(SYS_userfaultfd, O_NONBLOCK | UFFD_USER_MODE_ONLY);
	if (uffd < 0 || ioctl(uffd, UFFDIO_API, &api))
		goto out;
	registration.range.start = (unsigned long)base + PROCESS_PAGE_SIZE;
	failure = "UFFDIO_REGISTER_MODE_WP";
	if (ioctl(uffd, UFFDIO_REGISTER, &registration))
		goto out;
	writeprotect.range.start = registration.range.start;
	failure = "UFFDIO_WRITEPROTECT";
	if (ioctl(uffd, UFFDIO_WRITEPROTECT, &writeprotect))
		goto out;

	registration.range.start = (unsigned long)base;
	registration.range.len = NATIVE_PAGE_SIZE;
	registration.mode = UFFDIO_REGISTER_MODE_MISSING;
	failure = "UFFDIO_REGISTER_MODE_MISSING/mlock";
	if (ioctl(uffd, UFFDIO_REGISTER, &registration) ||
	    mlock(base + PROCESS_PAGE_SIZE, 3 * PROCESS_PAGE_SIZE))
		goto out;
	munlock(base + PROCESS_PAGE_SIZE, 3 * PROCESS_PAGE_SIZE);
	passed = base[PROCESS_PAGE_SIZE] == 0x41 &&
		 base[2 * PROCESS_PAGE_SIZE] == 0x42 &&
		 base[3 * PROCESS_PAGE_SIZE] == 0x43;
	if (passed) {
		base[PROCESS_PAGE_SIZE] = 0xa5;
		passed = base[PROCESS_PAGE_SIZE] == 0xa5;
	}
out:
	if (!passed)
		ksft_print_msg("UFFD mode-change setup failed at %s: %s (Rss: %lu, Swap: %lu)\n",
			       failure, strerror(errno), rss, swap);
	if (uffd >= 0)
		close(uffd);
	if (mapping != MAP_FAILED)
		munmap(mapping, mapping_size);
	return passed;
}

static int run_test(void)
{
	unsigned long total_swap = 0;
	unsigned long free_swap = 0;
	unsigned long resident = 0;
	unsigned long swapped = 0;
	unsigned long device_bytes = 0;
	unsigned long span = 0;
	unsigned char *mapping;
	unsigned char *reservation;
	bool paged_out;
	bool preserved = true;
	unsigned int i;

	ksft_print_header();
	ksft_set_plan(6);
	if (!swap_info(&total_swap, &free_swap))
		ksft_exit_fail_msg("could not read /proc/meminfo\n");
	if (!total_swap)
		ksft_exit_skip("no swap device is active\n");
	ksft_test_result(free_swap, "swap has free space\n");
	ksft_test_result(swapin_after_uffd_wp_mode_change(),
			 "compat swapin drops stale UFFD write protection\n");

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mapping = reservation == MAP_FAILED ? MAP_FAILED :
		mmap(reservation + PROCESS_PAGE_SIZE, MAPPING_SIZE,
		     PROT_READ | PROT_WRITE,
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

	paged_out = page_out_mapping(mapping, &resident, &swapped,
				     &device_bytes);
	ksft_test_result(paged_out,
			 "pageout installs swap PTEs (Swap: %lu, Rss: %lu, device: %lu bytes)\n",
			 swapped, resident, device_bytes);
	if (!paged_out)
		ksft_exit_fail_msg("could not create swap PTEs\n");

	for (i = 0; i < TEST_PAGES; i++) {
		if (mapping[i * PROCESS_PAGE_SIZE] != (unsigned char)(0x40 + i)) {
			preserved = false;
			break;
		}
	}
	ksft_test_result(preserved,
			 "swapped process pages preserve their contents\n");

	munmap(reservation, RESERVE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
