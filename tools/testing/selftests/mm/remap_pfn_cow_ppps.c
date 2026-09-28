// SPDX-License-Identifier: GPL-2.0

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"
#include "remap_pfn_cow_ppps.h"

#define TEST_MARKER	0x93
#define TEST_SLICE	2U

static int run_test(void)
{
	struct remap_pfn_cow_ppps_probe probe = {};
	unsigned char *mapping;
	int fd;

	ksft_print_header();
	ksft_set_plan(5);

	fd = ppps_open_fixture_or_skip("/dev/remap_pfn_cow_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open private PFN remap fixture\n");

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE, fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map one private non-zero PFN slice\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	ksft_test_result(mapping[0] == TEST_MARKER,
			 "mapping points at the requested physical slice\n");

	probe.address = (uintptr_t)mapping;
	ksft_test_result(!ioctl(fd, REMAP_PFN_COW_PPPS_IOCTL, &probe),
			 "query the COW PFN VMA offset metadata\n");
	ksft_test_result(probe.slice == TEST_SLICE && probe.offset_matches,
			 "COW PFN VMA stores the complete physical offset\n");

	munmap(mapping, PROCESS_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
