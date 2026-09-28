// SPDX-License-Identifier: GPL-2.0
/*
 * With RLIMIT_AS pinned to a 4K compat process's current statm size, a
 * one-process-page mremap expansion is rejected with ENOMEM.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/resource.h>

#include "kselftest_ppps.h"

static unsigned long current_vm_pages(void)
{
	unsigned long pages;
	FILE *statm = fopen("/proc/self/statm", "r");

	if (!statm)
		ksft_exit_fail_msg("open statm failed: %s\n", strerror(errno));
	if (fscanf(statm, "%lu", &pages) != 1)
		ksft_exit_fail_msg("read statm failed\n");
	fclose(statm);
	return pages;
}

static int run_test(void)
{
	struct rlimit limit;
	struct rlimit saved;
	unsigned long vm_pages;
	void *mapping;
	void *result;
	bool limit_set;
	bool rejected;

	ksft_print_header();
	ksft_set_plan(2);

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	vm_pages = current_vm_pages();
	/*
	 * Lower only the soft limit: it is restored before the test prints
	 * or exits, both of which need to allocate.
	 */
	if (getrlimit(RLIMIT_AS, &saved))
		ksft_exit_fail_msg("getrlimit failed: %s\n", strerror(errno));
	limit.rlim_cur = vm_pages * PROCESS_PAGE_SIZE;
	limit.rlim_max = saved.rlim_max;
	limit_set = !setrlimit(RLIMIT_AS, &limit);
	ksft_test_result(limit_set, "limit address space to its current size\n");
	if (!limit_set)
		ksft_exit_fail_msg("setrlimit failed: %s\n", strerror(errno));

	errno = 0;
	result = mremap(mapping, PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
			MREMAP_MAYMOVE);
	rejected = result == MAP_FAILED && errno == ENOMEM;
	if (setrlimit(RLIMIT_AS, &saved))
		ksft_exit_fail_msg("restore RLIMIT_AS failed: %s\n",
				   strerror(errno));
	ksft_test_result(rejected,
			 "reject a one-page mremap expansion at RLIMIT_AS\n");
	ksft_print_msg("statm_pages=%lu limit=%llu mremap=%p errno=%d\n",
		       vm_pages, (unsigned long long)limit.rlim_cur, result, errno);

	if (result != MAP_FAILED)
		munmap(result, 2 * PROCESS_PAGE_SIZE);
	else
		munmap(mapping, PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
