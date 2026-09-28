// SPDX-License-Identifier: GPL-2.0
/* MOVE keeps its completed prefix when a swapped source needs a lock drop. */
#define _GNU_SOURCE
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define PAGEOUT_POLLS 1000
#define TEST_LENGTH (3 * NATIVE_PAGE_SIZE)

static bool pageout_reaches_swap(void *address, unsigned long length,
				 unsigned long minimum)
{
	unsigned long swap;
	unsigned int attempt;

	if (madvise(address, length, MADV_PAGEOUT))
		return false;
	for (attempt = 0; attempt < PAGEOUT_POLLS; attempt++) {
		if (!ppps_smaps_bytes(address, length, "Swap", &swap))
			return false;
		if (swap >= minimum)
			return true;
		usleep(10000);
	}
	return false;
}

static unsigned char *map_exact_edge(size_t size,
				     unsigned char **reservation)
{
	unsigned char *mapping, *base;
	size_t length = size + 2 * NATIVE_PAGE_SIZE;

	mapping = mmap(NULL, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
		       -1, 0);
	if (mapping == MAP_FAILED)
		return MAP_FAILED;
	base = (unsigned char *)(((uintptr_t)mapping + 2 * NATIVE_PAGE_SIZE - 1) &
				 ~(NATIVE_PAGE_SIZE - 1));
	if (mprotect(base, size, PROT_READ | PROT_WRITE)) {
		munmap(mapping, length);
		return MAP_FAILED;
	}
	*reservation = mapping;
	return base;
}

static bool check_bytes(const unsigned char *base, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (base[i] != (unsigned char)(0x31 + i / NATIVE_PAGE_SIZE))
			return false;
	return true;
}

static void test_move(int uffd, bool conflict)
{
	struct uffdio_register reg = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_move move = {};
	struct uffdio_range range;
	unsigned char *src_reservation, *dst_reservation;
	unsigned char *src, *dst;
	unsigned long page = sysconf(_SC_PAGESIZE);
	uint64_t entry;
	bool ok;
	int ret;

	src = map_exact_edge(TEST_LENGTH, &src_reservation);
	dst = map_exact_edge(TEST_LENGTH, &dst_reservation);
	if (src == MAP_FAILED || dst == MAP_FAILED)
		ksft_exit_fail_msg("mapping setup failed: %s\n", strerror(errno));
	for (size_t i = 0; i < TEST_LENGTH; i++)
		src[i] = 0x31 + i / NATIVE_PAGE_SIZE;
	if (conflict)
		memset(dst + 2 * NATIVE_PAGE_SIZE, 0xa5, NATIVE_PAGE_SIZE);
	reg.range.start = (uintptr_t)dst;
	reg.range.len = TEST_LENGTH;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg))
		ksft_exit_fail_msg("register failed: %s\n", strerror(errno));

	ok = pageout_reaches_swap(src + NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE,
				  NATIVE_PAGE_SIZE);
	ksft_test_result(ok, "%s: page out the middle source folio\n",
			 conflict ? "partial" : "complete");
	if (!ok)
		ksft_exit_fail_msg("could not establish swapped-source precondition\n");
	ok = true;
	for (size_t off = NATIVE_PAGE_SIZE; off < 2 * NATIVE_PAGE_SIZE;
	     off += page) {
		ok &= ppps_pagemap_entry(src + off, &entry) &&
		      (entry & (1ULL << 62)) && !(entry & PAGEMAP_PRESENT);
	}
	ksft_test_result(ok, "source PTEs are swapped before MOVE\n");
	if (!ok)
		ksft_exit_fail_msg("source was not swapped\n");

	move.src = (uintptr_t)src;
	move.dst = (uintptr_t)dst;
	move.len = TEST_LENGTH;
	errno = 0;
	ret = ioctl(uffd, UFFDIO_MOVE, &move);
	ok = conflict ? ret == -1 && errno == EAGAIN &&
		move.move == 2 * NATIVE_PAGE_SIZE :
		!ret && move.move == TEST_LENGTH;
	ksft_test_result(ok, "%s MOVE reports progress across the swap retry\n",
			 conflict ? "partial" : "complete");
	if (!ok)
		ksft_exit_fail_msg("MOVE ret=%d errno=%d moved=%lld\n", ret, errno,
				   (long long)move.move);

	ksft_test_result(check_bytes(dst, conflict ? 2 * NATIVE_PAGE_SIZE :
				    TEST_LENGTH), "moved bytes preserve their source order\n");
	if (conflict) {
		ok = true;
		for (size_t i = 0; i < NATIVE_PAGE_SIZE; i++)
			ok &= src[2 * NATIVE_PAGE_SIZE + i] == 0x33 &&
			      dst[2 * NATIVE_PAGE_SIZE + i] == 0xa5;
	} else {
		ok = true;
		for (size_t off = 0; off < TEST_LENGTH; off += page)
			ok &= ppps_pagemap_entry(src + off, &entry) &&
			      !(entry & (PAGEMAP_PRESENT | (1ULL << 62)));
	}
	ksft_test_result(ok, "%s remains intact after MOVE\n",
			 conflict ? "unmoved suffix" : "empty source");

	range.start = (uintptr_t)dst;
	range.len = TEST_LENGTH;
	if (ioctl(uffd, UFFDIO_UNREGISTER, &range))
		ksft_exit_fail_msg("unregister failed\n");
	munmap(src_reservation, TEST_LENGTH + 2 * NATIVE_PAGE_SIZE);
	munmap(dst_reservation, TEST_LENGTH + 2 * NATIVE_PAGE_SIZE);
}

static int run_test(void)
{
	struct uffdio_api api = { .api = UFFD_API };
	FILE *swaps;
	char line[256];
	bool have_swap;
	int uffd;

	ksft_print_header();
	swaps = fopen("/proc/swaps", "re");
	have_swap = swaps && fgets(line, sizeof(line), swaps) &&
		fgets(line, sizeof(line), swaps);
	if (swaps)
		fclose(swaps);
	if (!have_swap)
		ksft_exit_skip("active swap is required\n");
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0)
		ksft_exit_skip("userfaultfd unavailable: %s\n", strerror(errno));
	if (ioctl(uffd, UFFDIO_API, &api))
		ksft_exit_fail_msg("UFFDIO_API failed\n");
	ksft_set_plan(10);
	test_move(uffd, false);
	test_move(uffd, true);
	close(uffd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (sysconf(_SC_PAGESIZE) != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
