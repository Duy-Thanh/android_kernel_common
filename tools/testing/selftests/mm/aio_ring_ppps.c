// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process's legacy AIO ring is sized, mapped and torn down in
 * 4K process-page units.
 */
#define _GNU_SOURCE

#include <linux/posix_types.h>
#include <linux/aio_abi.h>
#include <poll.h>
#include <sys/syscall.h>
#include <time.h>

#include "kselftest_ppps.h"

#define AIO_RING_MAGIC 0xa10a10a1
#define EVENT_DATA 0x61696f2d70707073ULL

struct aio_ring_user {
	unsigned int id;
	unsigned int nr;
	unsigned int head;
	unsigned int tail;
	unsigned int magic;
	unsigned int compat_features;
	unsigned int incompat_features;
	unsigned int header_length;
};

static unsigned long mapping_size(unsigned long addr)
{
	FILE *maps;
	char line[512];
	unsigned long start, end;

	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return 0;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2 &&
		    start <= addr && addr < end) {
			fclose(maps);
			return end - start;
		}
	}
	fclose(maps);
	return 0;
}

static bool complete_poll(aio_context_t ctx)
{
	struct timespec timeout = { .tv_sec = 1 };
	struct io_event event = {};
	struct iocb iocb = {
		.aio_data = EVENT_DATA,
		.aio_lio_opcode = IOCB_CMD_POLL,
		.aio_buf = POLLIN,
	};
	struct iocb *iocbs[] = { &iocb };
	char byte = 'x';
	int pipefd[2];
	long ret;

	if (pipe(pipefd))
		return false;
	iocb.aio_fildes = pipefd[0];
	ret = syscall(__NR_io_submit, ctx, 1, iocbs);
	if (ret == 1 && write(pipefd[1], &byte, 1) == 1)
		ret = syscall(__NR_io_getevents, ctx, 1, 1, &event, &timeout);
	close(pipefd[0]);
	close(pipefd[1]);
	return ret == 1 && event.data == EVENT_DATA &&
	       event.res > 0 && (event.res & POLLIN);
}

static int run_test(void)
{
	aio_context_t ctx = 0;
	struct aio_ring_user *ring;
	unsigned int expected_nr;
	unsigned long map_size;
	bool poll_completed;

	ksft_print_header();
	ksft_set_plan(6);
	if (syscall(__NR_io_setup, 1, &ctx))
		ksft_exit_fail_msg("io_setup failed: %s\n", strerror(errno));
	ksft_test_result(ctx != 0, "create a legacy AIO context\n");

	ring = (void *)(unsigned long)ctx;
	map_size = mapping_size((unsigned long)ctx);
	ksft_test_result(map_size == PROCESS_PAGE_SIZE,
			 "map one process page for a one-page AIO ring\n");
	ksft_test_result(ring->magic == AIO_RING_MAGIC &&
			 ring->header_length == sizeof(*ring),
			 "publish a valid AIO ring header\n");

	expected_nr = (PROCESS_PAGE_SIZE - sizeof(*ring)) /
		      sizeof(struct io_event);
	ksft_test_result(ring->nr == expected_nr,
			 "report capacity in the process-page ring\n");
	poll_completed = complete_poll(ctx);
	ksft_test_result(poll_completed,
			 "submit and reap an AIO poll completion\n");

	if (syscall(__NR_io_destroy, ctx))
		ksft_exit_fail_msg("io_destroy failed: %s\n", strerror(errno));
	ksft_test_result(mapping_size((unsigned long)ctx) == 0,
			 "destroy unmaps the process-page ring\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
