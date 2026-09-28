// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <fcntl.h>
#include <linux/dma-heap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kselftest_ppps.h"

int main(int argc, char **argv)
{
	struct dma_heap_allocation_data allocation = {
		.len = NATIVE_PAGE_SIZE,
		.fd_flags = O_RDWR | O_CLOEXEC,
	};
	unsigned char *mapping;
	const char *mode;
	unsigned int slice;
	int heap_fd;

	mode = ppps_run_mode(argc, argv, NULL);
	if (mode)
		ppps_require_compat();
	else if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);

	ksft_print_header();
	ksft_set_plan(3);

	heap_fd = ppps_open_fixture_or_skip("/dev/dma_heap/system", O_RDWR);
	ksft_test_result(heap_fd >= 0, "open the system DMA heap\n");

	if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &allocation))
		ksft_exit_fail_msg("DMA heap allocation failed\n");
	ksft_test_result(allocation.fd >= 0,
			 "allocate one native page from the system heap\n");

	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, allocation.fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map every process-page slice of the native page\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("system DMA heap mmap failed\n");

	for (slice = 0; slice < PPPS_SLICES; slice++)
		mapping[slice * PROCESS_PAGE_SIZE] = 0x40 + slice;
	for (slice = 0; slice < PPPS_SLICES; slice++) {
		if (mapping[slice * PROCESS_PAGE_SIZE] != 0x40 + slice)
			ksft_exit_fail_msg("slice %u did not retain data\n", slice);
	}

	munmap(mapping, NATIVE_PAGE_SIZE);
	close(allocation.fd);
	close(heap_fd);
	ksft_finished();
}
