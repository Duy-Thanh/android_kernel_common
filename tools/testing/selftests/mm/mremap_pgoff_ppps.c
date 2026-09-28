// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process's shared mapping of the fixture file at a 4K file
 * offset keeps that process-page offset when mremap grows and moves it.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define DEVICE_PATH "/dev/mremap_pgoff_ppps"

static unsigned long maps_offset(void *address)
{
	unsigned long start, end, offset;
	char line[512];
	FILE *maps = fopen("/proc/self/maps", "re");

	if (!maps)
		return ULONG_MAX;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &offset) == 3 &&
		    (unsigned long)address >= start &&
		    (unsigned long)address < end) {
			fclose(maps);
			return offset;
		}
	}
	fclose(maps);
	return ULONG_MAX;
}

static void test_fixed_file_mremap(void)
{
	unsigned char slice0[PROCESS_PAGE_SIZE];
	unsigned char slice2[PROCESS_PAGE_SIZE];
	unsigned char before, after;
	unsigned long offset;
	uintptr_t aligned;
	void *reservation;
	void *source;
	void *target;
	void *moved;
	int fd;

	memset(slice0, 0x11, sizeof(slice0));
	memset(slice2, 0x33, sizeof(slice2));
	fd = syscall(__NR_memfd_create, "p3s-mremap-offset", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE) ||
	    pwrite(fd, slice0, sizeof(slice0), 0) != sizeof(slice0) ||
	    pwrite(fd, slice2, sizeof(slice2), 2 * PROCESS_PAGE_SIZE) !=
		    sizeof(slice2))
		ksft_exit_fail_msg("prepare memfd failed: %s\n", strerror(errno));

	reservation = mmap(NULL, 4 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("reserve mmap failed: %s\n", strerror(errno));
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	source = (void *)aligned;
	target = (void *)(aligned + NATIVE_PAGE_SIZE +
			  2 * PROCESS_PAGE_SIZE);

	source = mmap(source, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, 0);
	if (source == MAP_FAILED)
		ksft_exit_fail_msg("source mmap failed: %s\n", strerror(errno));
	before = *(unsigned char *)source;
	moved = mremap(source, PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	ksft_test_result(moved != MAP_FAILED,
			 "move a file mapping to a different virtual slice\n");
	if (moved == MAP_FAILED) {
		ksft_test_result_skip("mremap failed: %s\n", strerror(errno));
		ksft_test_result_skip("mremap failed: %s\n", strerror(errno));
		goto out;
	}

	offset = maps_offset(moved);
	ksft_test_result(offset == 0,
			 "preserve file offset after fixed mremap\n");
	if (madvise(moved, PROCESS_PAGE_SIZE, MADV_DONTNEED))
		ksft_exit_fail_msg("madvise failed: %s\n", strerror(errno));
	after = *(unsigned char *)moved;
	ksft_test_result(before == 0x11 && after == 0x11,
			 "refault data from the original file slice\n");

out:
	munmap(reservation, 4 * NATIVE_PAGE_SIZE);
	close(fd);
}

static int run_test(void)
{
	void *mapping;
	void *moved;
	void *guard;
	void *reserve;
	int fd;

	ksft_print_header();
	ksft_set_plan(5);

	fd = ppps_open_fixture_or_skip(DEVICE_PATH, O_RDONLY);
	reserve = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reserve == MAP_FAILED)
		ksft_exit_fail_msg("reserve mmap failed: %s\n", strerror(errno));
	guard = reserve + PROCESS_PAGE_SIZE;
	if (munmap(reserve, PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("reserve munmap failed: %s\n", strerror(errno));
	mapping = mmap(reserve, PROCESS_PAGE_SIZE, PROT_READ,
		       MAP_SHARED | MAP_FIXED_NOREPLACE, fd, PROCESS_PAGE_SIZE);
	ksft_test_result(mapping != MAP_FAILED,
			 "map the test file at a 4K offset\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	errno = 0;
	moved = mremap(mapping, PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
		       MREMAP_MAYMOVE);
	ksft_test_result(moved != MAP_FAILED,
			 "preserve the process-page file offset during mremap\n");
	if (moved == MAP_FAILED)
		ksft_print_msg("mremap failed: %s\n", strerror(errno));
	else
		munmap(moved, 2 * PROCESS_PAGE_SIZE);
	if (moved == MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);
	munmap(guard, PROCESS_PAGE_SIZE);
	close(fd);
	test_fixed_file_mremap();
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
