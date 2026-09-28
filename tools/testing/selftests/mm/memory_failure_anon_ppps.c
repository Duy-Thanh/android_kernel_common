// SPDX-License-Identifier: GPL-2.0
/*
 * A packed anonymous folio remains discoverable by memory-failure rmap after
 * its first process-page slice is unmapped and the surviving VMA therefore
 * starts at a later tuple index.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "anon_exclusive_probe_ppps.h"
#include "kselftest_ppps.h"

#ifndef MADV_HWPOISON
#define MADV_HWPOISON 100
#endif

struct child_setup {
	int error;
	bool packed;
};

static void signal_exit(int signal)
{
	_exit(signal);
}

static void poison_later_slice(int status_fd)
{
	struct sigaction action = {
		.sa_handler = signal_exit,
	};
	struct anon_exclusive_probe_ppps probe = {};
	struct child_setup setup = {};
	unsigned char *mapping;
	int fixture_fd;
	unsigned int i;

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL) ||
	    sigaction(SIGALRM, &action, NULL)) {
		setup.error = errno;
		goto report;
	}

	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		setup.error = errno;
		goto report;
	}
	for (i = 0; i < NATIVE_PAGE_SIZE; i += PROCESS_PAGE_SIZE)
		mapping[i] = 0x40 + i / PROCESS_PAGE_SIZE;

	fixture_fd = open("/dev/anon_exclusive_probe_ppps", O_RDWR | O_CLOEXEC);
	if (fixture_fd < 0) {
		setup.error = errno;
		goto unmap;
	}
	probe.address = (uintptr_t)mapping;
	if (ioctl(fixture_fd, ANON_EXCLUSIVE_PROBE_PPPS_IOCTL, &probe)) {
		setup.error = errno;
		close(fixture_fd);
		goto unmap;
	}
	close(fixture_fd);
	setup.packed = probe.packed;

	if (munmap(mapping, PROCESS_PAGE_SIZE)) {
		setup.error = errno;
		goto unmap_tail;
	}
	if (!write_full(status_fd, &setup, sizeof(setup)))
		_exit(3);
	close(status_fd);

	/* A fixed collect_procs_anon() finds this later-slice VMA and SIGBUSes. */
	if (madvise(mapping + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		    MADV_HWPOISON))
		_exit(errno == EINVAL || errno == EOPNOTSUPP ? 2 : 4);
	/* Give an asynchronously queued machine-check signal time to arrive. */
	alarm(2);
	for (;;)
		pause();

unmap:
	munmap(mapping, NATIVE_PAGE_SIZE);
	goto report;
unmap_tail:
	munmap(mapping + PROCESS_PAGE_SIZE,
	       NATIVE_PAGE_SIZE - PROCESS_PAGE_SIZE);
report:
	write_full(status_fd, &setup, sizeof(setup));
	_exit(3);
}

static int run_test(void)
{
	struct child_setup setup = {};
	int pipefd[2];
	int status;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(4);

	ksft_test_result(!pipe2(pipefd, O_CLOEXEC),
			 "create memory-failure status pipe\n");
	child = fork();
	if (!child) {
		close(pipefd[0]);
		poison_later_slice(pipefd[1]);
	}
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	close(pipefd[1]);
	ksft_test_result(read_full(pipefd[0], &setup, sizeof(setup)) &&
			 !setup.error,
			 "prepare a resident packed anonymous tuple\n");
	close(pipefd[0]);
	ksft_test_result(setup.packed,
			 "fixture confirms packed anonymous ownership\n");
	if (waitpid(child, &status, 0) != child)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	if (WIFEXITED(status) && WEXITSTATUS(status) == 2)
		ksft_exit_skip("MADV_HWPOISON is unavailable\n");
	ksft_test_result(WIFEXITED(status) && WEXITSTATUS(status) == SIGBUS,
			 "memory failure finds a VMA containing only later slices\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
