// SPDX-License-Identifier: GPL-2.0
/*
 * When the gup_retry fixture's mmap success hook rejects a new 4K VMA of a
 * compat process, the failed MAP_FIXED mmap leaves exactly its 4K range
 * unmapped and the surrounding 4K slices of the native page intact.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE	(5 * NATIVE_PAGE_SIZE)

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static bool range_is_mapped(void *address, size_t length)
{
	unsigned char vec[4];

	return !mincore(address, length, vec);
}

static int run_test(void)
{
	unsigned char *mapping;
	unsigned char *target;
	void *result;
	int fd;

	ksft_print_header();
	ksft_set_plan(6);

	fd = ppps_open_fixture_or_skip("/dev/gup_retry_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open mmap cleanup test device\n");

	mapping = mmap(NULL, RESERVE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("reserve mapping failed: %s\n",
				   strerror(errno));
	target = (unsigned char *)align_up((uintptr_t)mapping,
					    NATIVE_PAGE_SIZE) +
		 NATIVE_PAGE_SIZE;
	ksft_test_result(!((uintptr_t)target % NATIVE_PAGE_SIZE) &&
			 target - PROCESS_PAGE_SIZE >= mapping &&
			 target + NATIVE_PAGE_SIZE <= mapping + RESERVE_SIZE,
			 "choose a native-aligned 4K replacement range\n");

	memset(target - PROCESS_PAGE_SIZE, 0x11, PROCESS_PAGE_SIZE);
	memset(target + PROCESS_PAGE_SIZE, 0x22,
	       NATIVE_PAGE_SIZE - PROCESS_PAGE_SIZE);
	errno = 0;
	result = mmap(target, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, 0);
	ksft_test_result(result == MAP_FAILED && errno == EIO,
			 "mmap success hook rejects the new 4K VMA\n");
	ksft_print_msg("mmap result=%p errno=%d\n", result, errno);

	errno = 0;
	ksft_test_result(!range_is_mapped(target, PROCESS_PAGE_SIZE) &&
			 errno == ENOMEM,
			 "failed mmap leaves its 4K range unmapped\n");
	ksft_test_result(range_is_mapped(target - PROCESS_PAGE_SIZE,
					 PROCESS_PAGE_SIZE),
			 "failed mmap preserves the preceding 4K page\n");
	ksft_test_result(range_is_mapped(target + PROCESS_PAGE_SIZE,
					 NATIVE_PAGE_SIZE - PROCESS_PAGE_SIZE),
			 "failed mmap preserves the following 12K range\n");

	munmap(mapping, RESERVE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
