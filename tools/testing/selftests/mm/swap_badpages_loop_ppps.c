// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include "kselftest_ppps.h"

/* Only formats a new, test-owned file, never an existing block device. */
int main(int argc, char **argv)
{
	unsigned char header[NATIVE_PAGE_SIZE] = {};
	char path[] = "/data/local/tmp/ppps-badpages-XXXXXX", dev[64];
	const unsigned int bytes = 32 * 1024 * 1024;
	unsigned int size = NATIVE_PAGE_SIZE, nr = 3, i;
	bool swapped = false;
	int ctl, loop, fd, index, ret, error;
	struct loop_info64 info = { .lo_flags = LO_FLAGS_AUTOCLEAR };

	ksft_print_header();
	ksft_set_plan(1);
	if (argc > 1 && !strcmp(argv[1], "4k"))
		size = PROCESS_PAGE_SIZE;
	if (argc > 2 && !strcmp(argv[2], "swab"))
		swapped = true;
	if (argc > 2 && !strcmp(argv[2], "control"))
		nr = 0;
	fd = mkstemp(path);
	ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
	if (ctl < 0)
		ctl = open("/dev/block/loop-control", O_RDWR | O_CLOEXEC);
	if (fd < 0 || ctl < 0)
		ksft_exit_skip("test file or loop-control unavailable: %s\n", strerror(errno));
	unlink(path);
	if (ftruncate(fd, bytes))
		ksft_exit_fail_msg("ftruncate: %s\n", strerror(errno));
	for (i = 0; i < 3 + nr; i++) {
		uint32_t value;
		size_t offset;

		if (i < 3) {
			offset = 1024 + 4 * i;
			value = i == 0 ? 1 : i == 1 ? bytes / size - 1 : nr;
		} else {
			offset = 1536 + 4 * (i - 3);
			value = (i - 2) * (NATIVE_PAGE_SIZE / size);
		}
		if (swapped)
			value = __builtin_bswap32(value);
		memcpy(header + offset, &value, sizeof(value));
	}
	memcpy(header + size - 10, "SWAPSPACE2", 10);
	if (pwrite(fd, header, sizeof(header), 0) != sizeof(header) || fsync(fd))
		ksft_exit_fail_msg("write header: %s\n", strerror(errno));
	index = ioctl(ctl, LOOP_CTL_GET_FREE);
	if (index < 0)
		ksft_exit_skip("no free loop device\n");
	snprintf(dev, sizeof(dev), "/dev/block/loop%d", index);
	loop = open(dev, O_RDWR | O_CLOEXEC);
	/* Android ueventd creates a newly allocated loop node asynchronously. */
	for (i = 0; loop < 0 && errno == ENOENT && i < 20; i++) {
		usleep(50000);
		loop = open(dev, O_RDWR | O_CLOEXEC);
	}
	if (loop < 0) {
		snprintf(dev, sizeof(dev), "/dev/loop%d", index);
		loop = open(dev, O_RDWR | O_CLOEXEC);
	}
	if (loop < 0 || ioctl(loop, LOOP_SET_FD, fd))
		ksft_exit_skip("cannot attach owned loop: %s\n", strerror(errno));
	if (ioctl(loop, LOOP_SET_STATUS64, &info)) {
		ioctl(loop, LOOP_CLR_FD, 0);
		ksft_exit_fail_msg("loop flags: %s\n", strerror(errno));
	}
	ksft_print_msg("swapon owned %s header=%u badpages=%u swab=%u\n",
		       dev, size, nr, swapped);
	fflush(stdout);
	ret = syscall(SYS_swapon, dev, 0);
	error = errno;
	if (!ret && syscall(SYS_swapoff, dev))
		ksft_exit_fail_msg("swapoff owned loop: %s\n", strerror(errno));
	ioctl(loop, LOOP_CLR_FD, 0);
	close(loop); close(ctl); close(fd);
	ksft_test_result(!ret, "valid block swap header accepted (ret=%d errno=%d)\n",
			 ret, ret ? error : 0);
	ksft_finished();
}
