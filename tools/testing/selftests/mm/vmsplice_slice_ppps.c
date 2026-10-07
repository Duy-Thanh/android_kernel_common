// SPDX-License-Identifier: GPL-2.0
/*
 * vmsplice(2) of a 4K compat process's anonymous memory into a pipe must
 * queue exactly the bytes of the iovec, whatever 4K slice of its 16K native
 * page the buffer lives in: the pipe then holds the requested length and
 * reading it back returns the same bytes.  Buffers in slices 1-3 have a
 * non-zero offset inside the native page that backs them.
 *
 * WARNING: on an affected kernel this test can oops the kernel (a pipe
 * buffer referring to a NULL page); run it last.
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>

#include "ppps_tuple_test.h"

static bool splice_and_check(unsigned char *base, unsigned long offset,
			     size_t len, const char *what)
{
	struct iovec iov = { .iov_base = base + offset, .iov_len = len };
	unsigned char *readback = malloc(len);
	int pipefd[2], queued = -1;
	ssize_t ret;
	bool ok = true;

	if (!readback || pipe(pipefd))
		ksft_exit_fail_msg("setup: %s\n", strerror(errno));
	if (fcntl(pipefd[1], F_SETPIPE_SZ, 1 << 20) < 0)
		ksft_print_msg("F_SETPIPE_SZ: %s\n", strerror(errno));
	ksft_print_msg("%s: vmsplice %zu bytes at window offset %lu\n", what,
		       len, offset);
	fflush(stdout);
	ret = vmsplice(pipefd[1], &iov, 1, 0);
	if (ret != (ssize_t)len) {
		ksft_print_msg("%s: vmsplice returned %zd (%s)\n", what, ret,
			       ret < 0 ? strerror(errno) : "short");
		ok = false;
	}
	if (ioctl(pipefd[0], FIONREAD, &queued) || queued != (int)len) {
		ksft_print_msg("%s: pipe holds %d bytes, expected %zu\n", what,
			       queued, len);
		ok = false;
	}
	if (ok) {
		if (!read_full(pipefd[0], readback, len) ||
		    memcmp(readback, base + offset, len)) {
			ksft_print_msg("%s: pipe contents differ from the buffer\n",
				       what);
			ok = false;
		}
	}
	close(pipefd[0]);
	close(pipefd[1]);
	free(readback);
	return ok;
}

static int run_test(void)
{
	unsigned char *reservation, *base;
	unsigned int slice;
	char what[64];
	unsigned long i;

	ksft_print_header();
	ksft_set_plan(PPPS_SLICES + 2);
	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	for (i = 0; i < 2 * NATIVE_PAGE_SIZE; i++)
		base[i] = (unsigned char)(i * 7 + i / PROCESS_PAGE_SIZE);

	for (slice = 0; slice < PPPS_SLICES; slice++) {
		snprintf(what, sizeof(what), "slice %u", slice);
		ksft_test_result(splice_and_check(base, slice * PROCESS_PAGE_SIZE,
						  PROCESS_PAGE_SIZE, what),
				 "vmsplice of a whole 4K page in slice %u\n", slice);
		fflush(stdout);
	}
	ksft_test_result(splice_and_check(base, PROCESS_PAGE_SIZE + 100,
					  2 * PROCESS_PAGE_SIZE, "unaligned"),
			 "vmsplice of an unaligned 8K range starting in slice 1\n");
	fflush(stdout);
	ksft_test_result(splice_and_check(base, 3 * PROCESS_PAGE_SIZE,
					  NATIVE_PAGE_SIZE, "crossing"),
			 "vmsplice of 16K crossing a native page boundary\n");
	munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
