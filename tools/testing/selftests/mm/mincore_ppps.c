// SPDX-License-Identifier: GPL-2.0
/*
 * mincore() reports process-page-sized vectors in both native and compat
 * processes.  Check alignment, rounding, cache lookup without PTEs and the
 * anonymous mapping residency lifecycle without assuming one fault populates
 * exactly one process page.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAPPING_SIZE	(4 * NATIVE_PAGE_SIZE)
#define SENTINEL	0xa5

static size_t page_size;

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static bool entries_are_resident(const unsigned char *vec, unsigned int nr)
{
	unsigned int i;

	for (i = 0; i < nr; i++) {
		if (vec[i] == SENTINEL || !(vec[i] & 1))
			return false;
	}
	return vec[nr] == SENTINEL;
}

static bool test_4k_only_alignment(unsigned char *large_page_aligned)
{
	unsigned char vec = SENTINEL;
	void *start = large_page_aligned + PROCESS_PAGE_SIZE;
	int ret;

	errno = 0;
	ret = mincore(start, PROCESS_PAGE_SIZE, &vec);
	if (page_size == NATIVE_PAGE_SIZE)
		return ret == -1 && errno == EINVAL && vec == SENTINEL;
	if (ret) {
		ksft_print_msg("mincore(4K-only aligned) failed: %s\n",
			       strerror(errno));
		return false;
	}
	return vec & 1;
}

static bool test_vector_granularity(unsigned char *large_page_aligned)
{
	unsigned char vec[8];

	memset(vec, SENTINEL, sizeof(vec));
	if (mincore(large_page_aligned, NATIVE_PAGE_SIZE, vec)) {
		ksft_print_msg("mincore(16K) failed: %s\n", strerror(errno));
		return false;
	}
	ksft_print_msg("16K vector: %02x,%02x,%02x,%02x next=%02x\n",
		       vec[0], vec[1], vec[2], vec[3], vec[4]);
	return entries_are_resident(vec, NATIVE_PAGE_SIZE / page_size);
}

static bool test_partial_length_rounding(unsigned char *large_page_aligned)
{
	const size_t length = page_size + 1;
	unsigned char vec[8];

	memset(vec, SENTINEL, sizeof(vec));
	if (mincore(large_page_aligned, length, vec)) {
		ksft_print_msg("mincore(partial length) failed: %s\n",
			       strerror(errno));
		return false;
	}
	ksft_print_msg("partial vector: %02x,%02x,%02x,%02x next=%02x\n",
		       vec[0], vec[1], vec[2], vec[3], vec[4]);
	return entries_are_resident(vec, 2);
}

static bool test_file_page_cache_index(void)
{
	unsigned char vec[8];
	unsigned char *contents = NULL;
	unsigned char *mapping = MAP_FAILED;
	bool passed = false;
	ssize_t written;
	int fd = -1;

	fd = memfd_create("mincore-ppps", MFD_CLOEXEC);
	if (fd < 0) {
		ksft_print_msg("memfd_create failed: %s\n", strerror(errno));
		goto out;
	}
	contents = malloc(NATIVE_PAGE_SIZE);
	if (!contents) {
		ksft_print_msg("malloc failed\n");
		goto out;
	}
	memset(contents, 0x6d, NATIVE_PAGE_SIZE);
	written = pwrite(fd, contents, NATIVE_PAGE_SIZE, 0);
	if (written != (ssize_t)NATIVE_PAGE_SIZE) {
		ksft_print_msg("pwrite returned %zd: %s\n", written,
			       strerror(errno));
		goto out;
	}

	/* Leave the PTEs absent so mincore() has to query the page cache. */
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		ksft_print_msg("file mmap failed: %s\n", strerror(errno));
		goto out;
	}
	memset(vec, SENTINEL, sizeof(vec));
	if (mincore(mapping, NATIVE_PAGE_SIZE, vec)) {
		ksft_print_msg("file mincore failed: %s\n", strerror(errno));
		goto out;
	}
	ksft_print_msg("file vector: %02x,%02x,%02x,%02x next=%02x\n",
		       vec[0], vec[1], vec[2], vec[3], vec[4]);
	passed = entries_are_resident(vec, NATIVE_PAGE_SIZE / page_size);

out:
	if (mapping != MAP_FAILED)
		munmap(mapping, NATIVE_PAGE_SIZE);
	free(contents);
	if (fd >= 0)
		close(fd);
	return passed;
}

static bool entries_are_absent(const unsigned char *vec, size_t nr)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		if (vec[i] == SENTINEL || (vec[i] & 1))
			return false;
	}
	return vec[nr] == SENTINEL;
}

static void test_residency(void)
{
	unsigned char vec[MAPPING_SIZE / PROCESS_PAGE_SIZE + 1];
	size_t nr = MAPPING_SIZE / page_size;
	unsigned char *map;
	bool pass;
	size_t i;
	int ret;

	map = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("residency mmap: %s\n", strerror(errno));
	/* Keep readahead/THP policy out of this base-page API test. */
	if (madvise(map, MAPPING_SIZE, MADV_NOHUGEPAGE) && errno != EINVAL)
		ksft_exit_fail_msg("MADV_NOHUGEPAGE: %s\n", strerror(errno));

	memset(vec, SENTINEL, sizeof(vec));
	pass = !mincore(map, MAPPING_SIZE, vec) && entries_are_absent(vec, nr);
	ksft_test_result(pass, "fresh anonymous mapping is not resident\n");

	map[0] = 0x5a;
	memset(vec, SENTINEL, sizeof(vec));
	pass = !mincore(map, MAPPING_SIZE, vec) && vec[0] != SENTINEL &&
	       (vec[0] & 1) &&
	       !(vec[nr - 1] & 1) && vec[nr] == SENTINEL;
	ksft_test_result(pass, "one fault leaves the distant native page absent\n");
	for (i = 0; i < MAPPING_SIZE; i += page_size)
		map[i] = 0x5a;
	memset(vec, SENTINEL, sizeof(vec));
	pass = !mincore(map, MAPPING_SIZE, vec) && entries_are_resident(vec, nr);
	ksft_test_result(pass, "touching every process page populates the vector\n");

	errno = 0;
	vec[0] = SENTINEL;
	ret = mincore(map + 1, page_size, vec);
	ksft_test_result(ret == -1 && errno == EINVAL && vec[0] == SENTINEL,
			 "byte-unaligned address is rejected without writing the vector\n");

	if (mprotect(map, MAPPING_SIZE, PROT_NONE))
		ksft_exit_fail_msg("mprotect: %s\n", strerror(errno));
	memset(vec, SENTINEL, sizeof(vec));
	pass = !mincore(map, MAPPING_SIZE, vec) && entries_are_resident(vec, nr);
	ksft_test_result(pass, "PROT_NONE retains residency without a data access\n");
	if (mprotect(map, MAPPING_SIZE, PROT_READ | PROT_WRITE) ||
	    madvise(map, MAPPING_SIZE, MADV_DONTNEED))
		ksft_exit_fail_msg("restore/discard mapping: %s\n", strerror(errno));
	memset(vec, SENTINEL, sizeof(vec));
	pass = !mincore(map, MAPPING_SIZE, vec) && entries_are_absent(vec, nr);
	ksft_test_result(pass, "MADV_DONTNEED clears the entire anonymous vector\n");
	pass = true;
	for (i = 0; i < MAPPING_SIZE; i++)
		pass &= map[i] == 0;
	ksft_test_result(pass, "discarded anonymous data refaults as zero\n");
	vec[0] = SENTINEL;
	ksft_test_result(!mincore(map, 0, vec) && vec[0] == SENTINEL,
			 "zero-length request leaves the vector unchanged\n");
	if (munmap(map, MAPPING_SIZE))
		ksft_exit_fail_msg("munmap: %s\n", strerror(errno));
}

static int run_test(void)
{
	unsigned char *large_page_aligned;
	unsigned char *mapping;
	unsigned int i;

	page_size = getpagesize();
	ksft_print_header();
	ksft_set_plan(12);
	ksft_print_msg("process page size: %zu bytes\n", page_size);

	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("anonymous mmap failed: %s\n", strerror(errno));
	large_page_aligned = (unsigned char *)align_up((uintptr_t)mapping,
							NATIVE_PAGE_SIZE);
	if (large_page_aligned + 2 * NATIVE_PAGE_SIZE > mapping + MAPPING_SIZE)
		ksft_exit_fail_msg("aligned range exceeds mapping\n");

	for (i = 0; i < 2 * NATIVE_PAGE_SIZE; i += page_size)
		large_page_aligned[i] = 0x40;

	ksft_test_result(test_4k_only_alignment(large_page_aligned),
			 "4K-only alignment follows the process page size\n");
	ksft_test_result(test_vector_granularity(large_page_aligned),
			 "mincore returns one byte per process page\n");
	ksft_test_result(test_partial_length_rounding(large_page_aligned),
			 "mincore rounds partial lengths at process granularity\n");
	ksft_test_result(test_file_page_cache_index(),
			 "file pages use the correct cache index without present PTEs\n");

	munmap(mapping, MAPPING_SIZE);
	test_residency();
	ksft_finished();
}

int main(int argc, char **argv)
{
	return ppps_geometry_main(argc, argv, run_test);
}
