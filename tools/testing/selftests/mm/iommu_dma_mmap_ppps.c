// SPDX-License-Identifier: GPL-2.0
/*
 * A noncontiguous IOMMU DMA buffer exported by the iommu_dma_mmap_ppps
 * fixture maps completely into a 4K compat process and preserves all four
 * process-page slices.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define BUFFER_SIZE (4 * PROCESS_PAGE_SIZE)

static int run_test(void)
{
	unsigned char *mapping;
	bool contents_ok = true;
	size_t offset;
	int fd;

	ksft_print_header();
	ksft_set_plan(3);

	fd = ppps_open_fixture_or_skip("/dev/iommu_dma_mmap_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open IOMMU DMA test device\n");

	mapping = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a complete noncontiguous DMA buffer\n");
	if (mapping == MAP_FAILED) {
		ksft_test_result_skip("DMA buffer mapping is unavailable\n");
		close(fd);
		ksft_finished();
	}

	for (offset = 0; offset < BUFFER_SIZE; offset += PROCESS_PAGE_SIZE) {
		if (mapping[offset] != 0x51 + offset / PROCESS_PAGE_SIZE) {
			contents_ok = false;
			break;
		}
	}
	ksft_test_result(contents_ok,
			 "preserve all four process-page DMA slices\n");

	munmap(mapping, BUFFER_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
