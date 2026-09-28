// SPDX-License-Identifier: GPL-2.0
/* Exercise the exported frame-count interface from a 4K PPPS process. */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "frame_vector_ppps.h"
#include "kselftest_ppps.h"

static int run_request(int fd, struct frame_vector_ppps_args *args)
{
	return ioctl(fd, FRAME_VECTOR_PPPS_IOCTL, args);
}

static int run_test(void)
{
	struct frame_vector_ppps_args args = {
		.nr_frames = 2,
		.capacity = 2,
		.write = 1,
	};
	unsigned char *mapping;
	void *stale;
	int fd;
	int rc;

	ksft_print_header();
	ksft_set_plan(13);

	fd = ppps_open_fixture_or_skip("/dev/" FRAME_VECTOR_PPPS_DEVICE_NAME,
				       O_RDWR);
	ksft_test_result(fd >= 0, "open the frame-vector fixture\n");
	errno = 0;
	args.capacity = 1;
	rc = run_request(fd, &args);
	ksft_test_result(rc == -1 && errno == EINVAL,
			 "reject a request larger than fixture capacity\n");
	args.capacity = 2;

	mapping = mmap(NULL, 3 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(mapping != MAP_FAILED, "map three process pages\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	mapping[PROCESS_PAGE_SIZE] = 0x5a;
	mapping[2 * PROCESS_PAGE_SIZE] = 0xa5;

	args.address = (uintptr_t)(mapping + PROCESS_PAGE_SIZE);
	rc = run_request(fd, &args);
	ksft_test_result(rc == 0, "invoke get_vaddr_frames() through ioctl\n");
	ksft_test_result(args.result == 2 && args.count == 2,
			 "pin two 4K process frames (result=%lld count=%u)\n",
			 (long long)args.result, args.count);
	ksft_test_result(args.frame_size == PROCESS_PAGE_SIZE,
			 "report the 4K process-frame size (%u)\n",
			 args.frame_size);
	ksft_test_result(args.offsets[0] == PROCESS_PAGE_SIZE &&
			 args.offsets[1] == 2 * PROCESS_PAGE_SIZE,
			 "preserve both native-page slice offsets (%u, %u)\n",
			 args.offsets[0], args.offsets[1]);
	ksft_test_result(mapping[PROCESS_PAGE_SIZE] == 0x5a &&
			 mapping[2 * PROCESS_PAGE_SIZE] == 0xa5,
			 "write pins preserve both process pages\n");

	memset(&args, 0, sizeof(args));
	args.address = (uintptr_t)mapping;
	args.capacity = 1;
	rc = run_request(fd, &args);
	ksft_test_result(rc == 0 && args.result == 0 && args.count == 0,
			 "zero frame request is a no-op\n");

	memset(&args, 0, sizeof(args));
	args.address = (uintptr_t)mapping;
	args.nr_frames = 1;
	args.capacity = 1;
	rc = run_request(fd, &args);
	ksft_test_result(rc == 0 && args.result == 1 && args.count == 1 &&
			 args.offsets[0] == 0,
			 "pin one process frame for read access\n");

	stale = mapping + 2 * PROCESS_PAGE_SIZE;
	ksft_test_result(munmap(stale, PROCESS_PAGE_SIZE) == 0,
			 "unmap the final process page\n");
	memset(&args, 0, sizeof(args));
	args.address = (uintptr_t)stale;
	args.nr_frames = 1;
	args.capacity = 1;
	rc = run_request(fd, &args);
	ksft_test_result(rc == 0, "query an unmapped process page\n");
	ksft_test_result(args.result == -EFAULT && !args.count,
			 "translate a zero-page pin result to EFAULT\n");

	munmap(mapping, 2 * PROCESS_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
