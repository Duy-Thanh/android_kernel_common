// SPDX-License-Identifier: GPL-2.0
/* Reject io_uring from 4K compat processes, including a transferred ring. */
#define _GNU_SOURCE

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

static int setup_ring(void)
{
	struct io_uring_params params = {};

	return syscall(SYS_io_uring_setup, 2, &params);
}

static int run_compat(int inherited_fd)
{
	struct io_uring_params params = {};
	void *mapping;
	int ret;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(5);
	errno = 0;
	ret = syscall(SYS_io_uring_setup, 2, &params);
	ksft_test_result(ret == -1 && errno == EOPNOTSUPP,
			 "reject io_uring_setup from a 4K compat process\n");
	errno = 0;
	ret = syscall(SYS_io_uring_enter, inherited_fd, 0, 0, 0, NULL, 0);
	ksft_test_result(ret == -1 && errno == EOPNOTSUPP,
			 "reject io_uring_enter on a transferred ring FD\n");
	errno = 0;
	ret = syscall(SYS_io_uring_register, inherited_fd,
		      IORING_UNREGISTER_BUFFERS, NULL, 0);
	ksft_test_result(ret == -1 && errno == EOPNOTSUPP,
			 "reject io_uring_register on a transferred ring FD\n");
	errno = 0;
	ret = syscall(SYS_io_uring_register, -1, IORING_REGISTER_BUFFERS, NULL, 0);
	ksft_test_result(ret == -1 && errno == EOPNOTSUPP,
			 "reject blind io_uring registration\n");
	errno = 0;
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, inherited_fd, IORING_OFF_SQ_RING);
	ksft_test_result(mapping == MAP_FAILED && errno == EOPNOTSUPP,
			 "reject mmap on a transferred ring FD\n");
	close(inherited_fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	char fd_arg[32];
	int fd;

	ppps_argv0 = argv[0];
	if (argc == 3 && !strcmp(argv[1], "--compat"))
		return run_compat(atoi(argv[2]));
	if (argc != 1)
		return EXIT_FAILURE;
#ifndef __aarch64__
	ksft_print_header();
	ksft_exit_skip("requires arm64 16K native / 4K compat geometry\n");
#else
	if (getpagesize() != NATIVE_PAGE_SIZE) {
		ksft_print_header();
		ksft_exit_skip("native exec does not use 16K pages\n");
	}
	fd = setup_ring();
	if (fd < 0) {
		ksft_print_header();
		ksft_exit_skip("native io_uring_setup unavailable: %s\n",
			       strerror(errno));
	}
	if (fcntl(fd, F_SETFD, 0))
		ksft_exit_fail_msg("clear ring FD_CLOEXEC failed: %s\n",
				   strerror(errno));
	snprintf(fd_arg, sizeof(fd_arg), "%d", fd);
	exec_compat(argv[0], "--compat", fd_arg, NULL);
#endif
}
