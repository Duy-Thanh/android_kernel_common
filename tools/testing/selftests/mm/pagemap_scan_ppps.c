// SPDX-License-Identifier: GPL-2.0
/* Validate PAGEMAP_SCAN argument geometry in a 4K PPPS process. */
#define _GNU_SOURCE

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

static void init_scan(struct pm_scan_arg *arg, void *start, size_t length,
		      struct page_region *regions, size_t nr_regions)
{
	memset(arg, 0, sizeof(*arg));
	arg->size = sizeof(*arg);
	arg->start = (uintptr_t)start;
	arg->end = arg->start + length;
	arg->vec = (uintptr_t)regions;
	arg->vec_len = nr_regions;
	arg->category_anyof_mask = PAGE_IS_PRESENT;
	arg->return_mask = PAGE_IS_PRESENT;
}

static int scan(int fd, struct pm_scan_arg *arg)
{
	errno = 0;
	return ioctl(fd, PAGEMAP_SCAN, arg);
}

static int run_test(void)
{
	struct page_region regions[8] = {};
	struct pm_scan_arg arg;
	unsigned char *mapping;
	uintptr_t rounded_end;
	int fd;
	int ret;

	ksft_print_header();
	ksft_set_plan(14);

	fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	ksft_test_result(fd >= 0, "open /proc/self/pagemap\n");
	mapping = mmap(NULL, 4 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(mapping != MAP_FAILED, "map four process pages\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	mapping[0] = 1;
	mapping[2 * PROCESS_PAGE_SIZE] = 2;

	init_scan(&arg, mapping, 3 * PROCESS_PAGE_SIZE + 1,
		  regions, ARRAY_SIZE(regions));
	ret = scan(fd, &arg);
	rounded_end = (uintptr_t)mapping + 4 * PROCESS_PAGE_SIZE;
	ksft_test_result(ret == 1, "report the packed tuple as one range (%d)\n",
			 ret);
	ksft_test_result(arg.walk_end == rounded_end,
			 "round the scan end in 4K process pages (%#llx)\n",
			 (unsigned long long)arg.walk_end);
	ksft_test_result(regions[0].start == (uintptr_t)mapping &&
			 regions[0].end == rounded_end,
			 "return one process-page-aligned tuple range\n");

	init_scan(&arg, mapping + 1, PROCESS_PAGE_SIZE, regions,
		  ARRAY_SIZE(regions));
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject a start not aligned to 4K\n");

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE, regions, ARRAY_SIZE(regions));
	arg.size--;
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject an unknown argument size\n");

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE, regions, ARRAY_SIZE(regions));
	arg.flags = 1ULL << 63;
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject unknown scan flags\n");

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE, regions, ARRAY_SIZE(regions));
	arg.return_mask = 1ULL << 63;
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject unknown page categories\n");

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE, NULL, 1);
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject a non-empty null output vector\n");

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE,
		  (struct page_region *)(uintptr_t)1, 1);
	ret = scan(fd, &arg);
	ksft_test_result(ret == -1 && errno == EFAULT,
			 "reject an inaccessible output vector\n");

	memset(regions, 0, sizeof(regions));
	init_scan(&arg, mapping, 3 * PROCESS_PAGE_SIZE, regions,
		  ARRAY_SIZE(regions));
	arg.max_pages = 1;
	ret = scan(fd, &arg);
	ksft_test_result(ret == 1, "honor a one-page result limit (%d)\n", ret);
	ksft_test_result(arg.walk_end == (uintptr_t)mapping + PROCESS_PAGE_SIZE,
			 "stop the walk at the process-page limit (%#llx)\n",
			 (unsigned long long)arg.walk_end);

	init_scan(&arg, mapping, PROCESS_PAGE_SIZE, NULL, 0);
	ret = scan(fd, &arg);
	ksft_test_result(ret == 0 &&
			 arg.walk_end == (uintptr_t)mapping + PROCESS_PAGE_SIZE,
			 "scan without an output vector\n");

	munmap(mapping, 4 * PROCESS_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
