// SPDX-License-Identifier: GPL-2.0
/* Verify cross-native-boundary mremap data placement and later COW. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

static unsigned char *reserve_aligned(size_t usable, void **reservation,
				      size_t *reservation_len)
{
	uintptr_t aligned;
	void *raw;

	*reservation_len = usable + 2 * NATIVE_PAGE_SIZE;
	raw = mmap(NULL, *reservation_len, PROT_NONE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (raw == MAP_FAILED)
		return MAP_FAILED;
	aligned = ((uintptr_t)raw + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	*reservation = raw;
	return (unsigned char *)aligned;
}

static bool check_slices(const unsigned char *p, unsigned int nr_slices,
			 unsigned int hole)
{
	unsigned int slice;

	for (slice = 0; slice < nr_slices; slice++) {
		unsigned char expected = slice == hole ? 0 : 0x40 + slice;

		if (p[slice * PROCESS_PAGE_SIZE] != expected ||
		    p[(slice + 1) * PROCESS_PAGE_SIZE - 1] != expected)
			return false;
	}
	return true;
}

static bool move_and_cow(unsigned int nr_slices, unsigned int hole)
{
	const size_t length = nr_slices * PROCESS_PAGE_SIZE;
	void *src_res = NULL, *dst_res = NULL;
	size_t src_res_len = 0, dst_res_len = 0;
	unsigned char *src, *dst_base, *moved;
	unsigned int slice;
	bool ok = false;
	int status;
	pid_t child;

	src = reserve_aligned(length, &src_res, &src_res_len);
	dst_base = reserve_aligned(length + NATIVE_PAGE_SIZE, &dst_res,
				   &dst_res_len);
	if (src == MAP_FAILED || dst_base == MAP_FAILED)
		goto out;
	if (mprotect(src, length, PROT_READ | PROT_WRITE))
		goto out;
	for (slice = 0; slice < nr_slices; slice++)
		memset(src + slice * PROCESS_PAGE_SIZE, 0x40 + slice,
		       PROCESS_PAGE_SIZE);
	if (hole < nr_slices && madvise(src + hole * PROCESS_PAGE_SIZE,
					PROCESS_PAGE_SIZE, MADV_DONTNEED))
		goto out;

	moved = mremap(src, length, length, MREMAP_MAYMOVE | MREMAP_FIXED,
		       dst_base + PROCESS_PAGE_SIZE);
	if (moved == MAP_FAILED || !check_slices(moved, nr_slices, hole))
		goto out;

	child = fork();
	if (child < 0)
		goto out;
	if (!child) {
		unsigned int target = nr_slices - 1;

		moved[target * PROCESS_PAGE_SIZE] = 0xa5;
		_exit(moved[target * PROCESS_PAGE_SIZE] != 0xa5 ||
		      !check_slices(moved, target, hole));
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status) || !check_slices(moved, nr_slices, hole))
		goto out;
	ok = true;
out:
	if (dst_res)
		munmap(dst_res, dst_res_len);
	if (src_res)
		munmap(src_res, src_res_len);
	return ok;
}

static bool move_private_file_cow(void)
{
	const size_t length = NATIVE_PAGE_SIZE;
	void *src_res = NULL, *dst_res = NULL;
	size_t src_res_len = 0, dst_res_len = 0;
	unsigned char *src_base, *src, *dst_base, *moved;
	unsigned char fill[NATIVE_PAGE_SIZE];
	unsigned int slice;
	bool ok = false;
	int fd = -1, status;
	pid_t child;

	src_base = reserve_aligned(length, &src_res, &src_res_len);
	dst_base = reserve_aligned(length + NATIVE_PAGE_SIZE, &dst_res,
				   &dst_res_len);
	if (src_base == MAP_FAILED || dst_base == MAP_FAILED)
		goto out;
	fd = memfd_create("mremap-slice-cow", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, length))
		goto out;
	memset(fill, 0x20, sizeof(fill));
	if (write(fd, fill, sizeof(fill)) != sizeof(fill))
		goto out;
	src = mmap(src_base, length, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_FIXED, fd, 0);
	if (src == MAP_FAILED)
		goto out;
	/* Dirty every process page so every present PTE is a private COW page. */
	for (slice = 0; slice < PPPS_SLICES; slice++)
		memset(src + slice * PROCESS_PAGE_SIZE, 0x60 + slice,
		       PROCESS_PAGE_SIZE);
	moved = mremap(src, length, length, MREMAP_MAYMOVE | MREMAP_FIXED,
		       dst_base + PROCESS_PAGE_SIZE);
	if (moved == MAP_FAILED)
		goto out;
	for (slice = 0; slice < PPPS_SLICES; slice++)
		if (moved[slice * PROCESS_PAGE_SIZE] != 0x60 + slice ||
		    moved[(slice + 1) * PROCESS_PAGE_SIZE - 1] != 0x60 + slice)
			goto out;
	child = fork();
	if (child < 0)
		goto out;
	if (!child) {
		moved[PROCESS_PAGE_SIZE] = 0xa5;
		_exit(moved[0] != 0x60 || moved[PROCESS_PAGE_SIZE] != 0xa5 ||
		      moved[2 * PROCESS_PAGE_SIZE] != 0x62);
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status) || moved[PROCESS_PAGE_SIZE] != 0x61)
		goto out;
	ok = true;
out:
	if (fd >= 0)
		close(fd);
	if (dst_res)
		munmap(dst_res, dst_res_len);
	if (src_res)
		munmap(src_res, src_res_len);
	return ok;
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(3);
	ksft_test_result(move_and_cow(PPPS_SLICES, PPPS_SLICES),
			 "cross-slice mremap preserves data across fork/COW\n");
	ksft_test_result(move_and_cow(2 * PPPS_SLICES, 2),
			 "cross-slice mremap preserves sparse PTE layout\n");
	ksft_test_result(move_private_file_cow(),
			 "cross-slice mremap preserves private file COW pages\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
