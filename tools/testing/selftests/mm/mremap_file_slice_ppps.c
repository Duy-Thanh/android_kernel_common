// SPDX-License-Identifier: GPL-2.0
/* Preserve process-page file offsets across fixed and automatic mremap moves. */
#define _GNU_SOURCE

#include <limits.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define PMD_SIZE_4KB	(32UL * 1024 * 1024)

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

static void test_fixed_source_slice(void)
{
	unsigned char page[PROCESS_PAGE_SIZE];
	unsigned long offset;
	uintptr_t aligned;
	void *reservation, *source, *target, *moved;
	int fd;
	unsigned int i;

	fd = syscall(__NR_memfd_create, "p3s-mremap-source-slice", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		ksft_exit_fail_msg("prepare source-slice memfd failed: %s\n",
				   strerror(errno));
	for (i = 0; i < NATIVE_PAGE_SIZE / PROCESS_PAGE_SIZE; i++) {
		memset(page, 0x41 + i, sizeof(page));
		if (pwrite(fd, page, sizeof(page), i * PROCESS_PAGE_SIZE) !=
		    sizeof(page))
			ksft_exit_fail_msg("populate source-slice memfd failed: %s\n",
					   strerror(errno));
	}

	reservation = mmap(NULL, 5 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("reserve source-slice range failed: %s\n",
				   strerror(errno));
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	source = mmap((void *)aligned, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, 0);
	if (source == MAP_FAILED)
		ksft_exit_fail_msg("map source-slice memfd failed: %s\n",
				   strerror(errno));
	target = (void *)(aligned + 2 * NATIVE_PAGE_SIZE);
	/* Move an unfaulted source slice to a different virtual slice. */
	moved = mremap(source + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		       PROCESS_PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED, target);
	ksft_test_result(moved != MAP_FAILED,
			 "move a nonzero file slice to a native-aligned address\n");
	if (moved == MAP_FAILED) {
		ksft_test_result_skip("mremap failed: %s\n", strerror(errno));
		ksft_test_result_skip("mremap failed: %s\n", strerror(errno));
		goto out;
	}
	offset = maps_offset(moved);
	ksft_test_result(offset == PROCESS_PAGE_SIZE,
			 "preserve the source process-page file offset\n");
	if (madvise(moved, PROCESS_PAGE_SIZE, MADV_DONTNEED))
		ksft_exit_fail_msg("drop moved source slice failed: %s\n",
				   strerror(errno));
	ksft_test_result(*(unsigned char *)moved == 0x42,
			 "refault data from the original source slice\n");
out:
	munmap(reservation, 5 * NATIVE_PAGE_SIZE);
	close(fd);
}

static bool set_shmem_thp_force(char previous[32])
{
	char state[256], *left, *right;
	FILE *file;

	file = fopen("/sys/kernel/mm/transparent_hugepage/shmem_enabled", "re");
	if (!file || !fgets(state, sizeof(state), file)) {
		if (file)
			fclose(file);
		return false;
	}
	fclose(file);
	left = strchr(state, '[');
	right = left ? strchr(left, ']') : NULL;
	if (!left || !right || right - left >= 32)
		return false;
	memcpy(previous, left + 1, right - left - 1);
	previous[right - left - 1] = '\0';
	file = fopen("/sys/kernel/mm/transparent_hugepage/shmem_enabled", "we");
	if (!file)
		return false;
	fputs("force", file);
	return fclose(file) == 0;
}

static void restore_shmem_thp(const char *previous)
{
	FILE *file = fopen("/sys/kernel/mm/transparent_hugepage/shmem_enabled", "we");

	if (!file)
		return;
	fputs(previous, file);
	fclose(file);
}

static void test_get_unmapped_area_offset(void)
{
	const size_t old_len = PMD_SIZE_4KB;
	const size_t new_len = 2 * PMD_SIZE_4KB;
	char previous[32];
	uintptr_t aligned;
	void *reservation, *source, *guard, *moved;
	int fd;

	if (!set_shmem_thp_force(previous)) {
		ksft_test_result_skip("cannot force shmem THP alignment\n");
		return;
	}
	fd = syscall(__NR_memfd_create, "p3s-mremap-gua", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, new_len + PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("prepare GUA memfd failed: %s\n", strerror(errno));
	reservation = mmap(NULL, new_len + 2 * PMD_SIZE_4KB, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("reserve GUA range failed: %s\n", strerror(errno));
	aligned = ((uintptr_t)reservation + PMD_SIZE_4KB - 1) &
		  ~(PMD_SIZE_4KB - 1);
	source = mmap((void *)aligned, old_len, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, PROCESS_PAGE_SIZE);
	guard = mmap((void *)(aligned + old_len), PROCESS_PAGE_SIZE, PROT_NONE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (source == MAP_FAILED || guard == MAP_FAILED)
		ksft_exit_fail_msg("map GUA fixture failed: %s\n", strerror(errno));
	moved = mremap(source, old_len, new_len, MREMAP_MAYMOVE);
	ksft_test_result(moved != MAP_FAILED &&
			 ((uintptr_t)moved & (PMD_SIZE_4KB - 1)) == PROCESS_PAGE_SIZE,
			 "pass a process-page file offset to get_unmapped_area\n");
	if (moved != MAP_FAILED)
		munmap(moved, new_len);
	else
		munmap(source, old_len);
	munmap(guard, PROCESS_PAGE_SIZE);
	munmap(reservation, new_len + 2 * PMD_SIZE_4KB);
	close(fd);
	restore_shmem_thp(previous);
}

static int run_test(void)
{
	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(4);
	test_fixed_source_slice();
	test_get_unmapped_area_offset();
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
