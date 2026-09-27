// SPDX-License-Identifier: GPL-2.0
/*
 * PR_SET_VMA_ANON_NAME in a 4K compat process accepts a 4K-only-aligned
 * address and a 4K length, and names exactly one 4K page.
 */
#define _GNU_SOURCE

#include <linux/prctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include "kselftest_ppps.h"

#define MAPPING_SIZE	(16 * PROCESS_PAGE_SIZE)

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static bool named_range_is_exact(const char *name, uintptr_t expected_start,
				 uintptr_t expected_end)
{
	char expected_name[128];
	char line[512];
	unsigned long start, end;
	unsigned int matches = 0;
	bool exact = false;
	FILE *maps;

	snprintf(expected_name, sizeof(expected_name), "[anon:%s]", name);
	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		ksft_print_msg("failed to open /proc/self/maps: %s\n",
			       strerror(errno));
		return false;
	}

	while (fgets(line, sizeof(line), maps)) {
		if (!strstr(line, expected_name))
			continue;
		if (sscanf(line, "%lx-%lx", &start, &end) != 2)
			continue;
		matches++;
		exact = start == expected_start && end == expected_end;
		ksft_print_msg("%s range: %lx-%lx (expected %lx-%lx)\n",
			       name, start, end, (unsigned long)expected_start,
			       (unsigned long)expected_end);
	}
	fclose(maps);

	if (!matches)
		ksft_print_msg("no /proc/self/maps entry found for %s\n", name);
	else if (matches != 1)
		ksft_print_msg("found %u /proc/self/maps entries for %s\n",
			       matches, name);

	return matches == 1 && exact;
}

static int name_range(void *start, size_t length, const char *name)
{
	errno = 0;
	return prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
		     (unsigned long)start, length, (unsigned long)name);
}

static bool zero_range(unsigned char *address, unsigned char *start,
		       unsigned char *end, unsigned long expected_offset)
{
	unsigned long first, last, offset;
	char permissions[5], line[512];
	FILE *maps = fopen("/proc/self/maps", "r");
	bool match = false;

	if (!maps)
		return false;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %4s %lx", &first, &last,
			   permissions, &offset) != 4 ||
		    (unsigned long)address < first || (unsigned long)address >= last)
			continue;
		match = first == (unsigned long)start && last == (unsigned long)end &&
			offset == expected_offset;
		break;
	}
	fclose(maps);
	return match;
}

static void test_zero_offsets(void)
{
	int fd = open("/dev/zero", O_RDWR);
	unsigned long len = 2 * NATIVE_PAGE_SIZE;
	unsigned char *map = MAP_FAILED;
	bool split = false, merged = false;

	if (fd >= 0)
		map = mmap(NULL, len, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE, fd, 2 * PROCESS_PAGE_SIZE);
	if (map != MAP_FAILED) {
		for (unsigned long i = 0; i < len; i += PROCESS_PAGE_SIZE)
			map[i] = (unsigned char)(0x51 + i / PROCESS_PAGE_SIZE);
		split = !mprotect(map + PROCESS_PAGE_SIZE, len - PROCESS_PAGE_SIZE,
				  PROT_READ) &&
			zero_range(map + PROCESS_PAGE_SIZE, map + PROCESS_PAGE_SIZE,
				   map + len, 3 * PROCESS_PAGE_SIZE);
		merged = !mprotect(map, len, PROT_READ | PROT_WRITE) &&
			zero_range(map, map, map + len, 2 * PROCESS_PAGE_SIZE);
		for (unsigned long i = 0; i < len; i += PROCESS_PAGE_SIZE)
			merged &= map[i] == (unsigned char)(0x51 + i / PROCESS_PAGE_SIZE);
		munmap(map, len);
	}
	if (fd >= 0)
		close(fd);
	ksft_test_result(split, "/dev/zero split retains byte-accurate offsets\n");
	ksft_test_result(merged, "/dev/zero restores one VMA and preserves all slices\n");
}

static int run_test(void)
{
	const char *alignment_name = "ppps-4k-alignment";
	const char *length_name = "ppps-4k-length";
	unsigned char *large_page_aligned;
	unsigned char *only_4k_aligned;
	unsigned char *mapping;
	bool alignment_succeeded;
	bool length_succeeded;

	ksft_print_header();
	ksft_set_plan(5);

	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("anonymous mmap failed: %s\n", strerror(errno));
	large_page_aligned = (unsigned char *)align_up((uintptr_t)mapping,
							NATIVE_PAGE_SIZE);
	if (large_page_aligned + NATIVE_PAGE_SIZE > mapping + MAPPING_SIZE)
		ksft_exit_fail_msg("aligned range exceeds mapping\n");
	only_4k_aligned = large_page_aligned + PROCESS_PAGE_SIZE;

	alignment_succeeded = !name_range(only_4k_aligned, PROCESS_PAGE_SIZE,
					  alignment_name);
	if (!alignment_succeeded)
		ksft_print_msg("PR_SET_VMA_ANON_NAME at a 4K-only-aligned address failed: %s\n",
			       strerror(errno));
	ksft_test_result(alignment_succeeded,
			 "anon VMA naming accepts a 4K-only-aligned address\n");
	ksft_test_result(alignment_succeeded &&
			 named_range_is_exact(alignment_name,
					      (uintptr_t)only_4k_aligned,
					      (uintptr_t)only_4k_aligned +
					      PROCESS_PAGE_SIZE),
			 "4K-only-aligned naming affects exactly one 4K page\n");

	length_succeeded = !name_range(large_page_aligned, PROCESS_PAGE_SIZE,
				       length_name);
	if (!length_succeeded)
		ksft_print_msg("PR_SET_VMA_ANON_NAME with a 4K length failed: %s\n",
			       strerror(errno));
	ksft_test_result(length_succeeded &&
			 named_range_is_exact(length_name,
					      (uintptr_t)large_page_aligned,
					      (uintptr_t)large_page_aligned +
					      PROCESS_PAGE_SIZE),
			 "anon VMA naming rounds length at 4K granularity\n");

	munmap(mapping, MAPPING_SIZE);
	test_zero_offsets();
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
