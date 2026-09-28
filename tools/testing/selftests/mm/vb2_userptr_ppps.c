// SPDX-License-Identifier: GPL-2.0
/*
 * videobuf2 USERPTR buffers of a 4K compat process: through the
 * vb2_userptr_ppps fixture, the vmalloc and DMA-SG backends consume and write
 * exactly the two selected 4K pages and leave the adjacent 4K pages of the
 * same native page unchanged.
 */
#define _GNU_SOURCE

#include <linux/ioctl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define TEST_LENGTH (2 * PROCESS_PAGE_SIZE)
#define VB2_USERPTR_VMALLOC 1
#define VB2_USERPTR_DMA_SG 2
#define VB2_USERPTR_CAPTURE 3

struct vb2_userptr_request {
	uint64_t user_addr;
	uint32_t length;
	uint32_t backend;
};

#define VB2_USERPTR_PPPS_RUN \
	_IOW('V', 0x71, struct vb2_userptr_request)

static void *map_test_area(void **reservation_out)
{
	uintptr_t aligned;
	void *reservation;
	void *mapping;

	reservation = mmap(NULL, 4 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return MAP_FAILED;
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	mapping = mmap((void *)aligned, NATIVE_PAGE_SIZE,
		       PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapping == MAP_FAILED) {
		munmap(reservation, 4 * NATIVE_PAGE_SIZE);
		return MAP_FAILED;
	}
	*reservation_out = reservation;
	return mapping;
}

static bool run_backend(int fd, unsigned char *region, unsigned int backend,
			unsigned char first, unsigned char second)
{
	struct vb2_userptr_request req = {
		.user_addr = (uintptr_t)region,
		.length = TEST_LENGTH,
		.backend = backend,
	};

	region[0] = first;
	region[PROCESS_PAGE_SIZE] = second;
	errno = 0;
	return ioctl(fd, VB2_USERPTR_PPPS_RUN, &req) == 0;
}

static int run_test(void)
{
	void *reservation = MAP_FAILED;
	unsigned char *mapping;
	unsigned char *region;
	bool vmalloc_ok = false;
	bool dma_sg_ok = false;
	int fd;

	ksft_print_header();
	ksft_set_plan(8);
	mapping = map_test_area(&reservation);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a native-aligned four-page test area\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mapping failed: %s\n", strerror(errno));
	region = mapping + PROCESS_PAGE_SIZE;
	mapping[0] = 0xa0;
	mapping[3 * PROCESS_PAGE_SIZE] = 0xb0;

	fd = open("/dev/vb2_userptr_ppps", O_RDWR | O_CLOEXEC);
	ksft_test_result(fd >= 0, "open videobuf2 USERPTR fixture\n");
	if (fd >= 0)
		vmalloc_ok = run_backend(fd, region, VB2_USERPTR_VMALLOC,
					 0x11, 0x22);
	ksft_test_result(vmalloc_ok,
			 "vmalloc backend consumes selected 4K pages (errno=%d)\n",
			 vmalloc_ok ? 0 : errno);
	ksft_test_result(vmalloc_ok && region[0] == 0x31 &&
			 region[PROCESS_PAGE_SIZE] == 0x42,
			 "vmalloc backend writes both USERPTR pages\n");

	if (fd >= 0)
		dma_sg_ok = run_backend(fd, region, VB2_USERPTR_DMA_SG,
					0x51, 0x62);
	ksft_test_result(dma_sg_ok,
			 "DMA-SG backend consumes selected 4K pages (errno=%d)\n",
			 dma_sg_ok ? 0 : errno);
	ksft_test_result(dma_sg_ok && region[0] == 0x71 &&
			 region[PROCESS_PAGE_SIZE] == 0x82,
			 "DMA-SG backend writes both USERPTR pages\n");
	{
		bool capture_ok;
		size_t i;

		memset(region, 0x96, TEST_LENGTH);
		capture_ok = fd >= 0 && run_backend(fd, region,
					VB2_USERPTR_CAPTURE, 0x96, 0x96);
		for (i = 0; i < TEST_LENGTH; i++) {
			unsigned char expected = i == 0 ? 0x31 :
				(i == PROCESS_PAGE_SIZE ? 0x5a : 0x96);

			if (region[i] != expected)
				capture_ok = false;
		}
		ksft_test_result(capture_ok,
				 "short and cancelled captures preserve unwritten bytes\n");
	}
	ksft_test_result(mapping[0] == 0xa0 &&
			 mapping[3 * PROCESS_PAGE_SIZE] == 0xb0,
			 "both backends leave adjacent 4K pages unchanged\n");

	if (fd >= 0)
		close(fd);
	munmap(reservation, 4 * NATIVE_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
