// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) userfaultfd handler unregisters a write-protected 4K page of
 * a compat owner; the owner's pagemap UFFD-WP bit clears and the page becomes
 * writable again.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE		(3 * PROCESS_PAGE_SIZE)
#define OWNER_RESERVE_ADDRESS	((1UL << 28) + 11 * PROCESS_PAGE_SIZE)
#define INITIAL_VALUE		0x39
#define WRITTEN_VALUE		0x93

struct handler_report {
	long page_size;
	int result;
	int error;
};

static bool pagemap_uffd_wp(const void *address, bool *write_protected)
{
	uint64_t entry;

	if (!ppps_pagemap_entry(address, &entry))
		return false;
	*write_protected = entry & PAGEMAP_UFFD_WP;
	return true;
}

static int run_native_handler(int uffd, unsigned long destination,
			      int report_fd)
{
	struct uffdio_range range = {
		.start = destination,
		.len = PROCESS_PAGE_SIZE,
	};
	struct handler_report report = {
		.page_size = sysconf(_SC_PAGESIZE),
		.result = -1,
	};

	errno = 0;
	report.result = ioctl(uffd, UFFDIO_UNREGISTER, &range);
	report.error = errno;
	if (!write_full(report_fd, &report, sizeof(report)))
		return EXIT_FAILURE;
	return report.result ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int exec_native_handler(int uffd, unsigned long destination,
			       int report_fd)
{
	char destination_arg[32];
	char report_fd_arg[16];
	char uffd_arg[16];

	snprintf(uffd_arg, sizeof(uffd_arg), "%d", uffd);
	snprintf(destination_arg, sizeof(destination_arg), "%lu", destination);
	snprintf(report_fd_arg, sizeof(report_fd_arg), "%d", report_fd);
	ppps_execl(false, NULL, "--handler", uffd_arg, destination_arg,
		   report_fd_arg, NULL);
	return EXIT_FAILURE;
}

static int run_compat_owner(void)
{
	struct uffdio_writeprotect writeprotect = {
		.range.len = PROCESS_PAGE_SIZE,
		.mode = UFFDIO_WRITEPROTECT_MODE_WP,
	};
	struct uffdio_register registration = {
		.range.len = PROCESS_PAGE_SIZE,
		.mode = UFFDIO_REGISTER_MODE_WP,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP,
	};
	struct handler_report report = {};
	unsigned char *destination;
	unsigned char *reservation;
	bool page_is_wp = false;
	bool pagemap_ok;
	bool report_ok;
	bool setup_ok;
	int handler_status = 0;
	int report_pipe[2];
	pid_t handler;
	int uffd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(8);

	reservation = mmap((void *)OWNER_RESERVE_ADDRESS, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			   -1, 0);
	destination = reservation == MAP_FAILED ? MAP_FAILED :
		mmap(reservation + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	uffd = syscall(SYS_userfaultfd, O_NONBLOCK);
	setup_ok = destination != MAP_FAILED && uffd >= 0 &&
		   !ioctl(uffd, UFFDIO_API, &api) &&
		   api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP;
	if (setup_ok) {
		*destination = INITIAL_VALUE;
		registration.range.start = (unsigned long)destination;
		setup_ok = !ioctl(uffd, UFFDIO_REGISTER, &registration) &&
			   registration.ioctls & (1ULL << _UFFDIO_WRITEPROTECT);
	}
	ksft_test_result(setup_ok, "register one 4K page for UFFD WP\n");
	if (!setup_ok) {
		if (uffd < 0 && errno == EPERM)
			ksft_exit_skip("userfaultfd unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd WP setup failed: %s\n",
				   strerror(errno));
	}

	writeprotect.range.start = (unsigned long)destination;
	ksft_test_result(!ioctl(uffd, UFFDIO_WRITEPROTECT, &writeprotect),
			 "owner write-protects its page\n");
	pagemap_ok = pagemap_uffd_wp(destination, &page_is_wp);
	ksft_test_result(pagemap_ok && page_is_wp,
			 "pagemap reports the owner UFFD WP bit\n");

	if (pipe(report_pipe))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));
	handler = fork();
	if (handler < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!handler) {
		close(report_pipe[0]);
		_exit(exec_native_handler(uffd, (unsigned long)destination,
					  report_pipe[1]));
	}
	close(report_pipe[1]);
	report_ok = read_full(report_pipe[0], &report, sizeof(report));
	close(report_pipe[0]);
	waitpid(handler, &handler_status, 0);
	ksft_print_msg("owner page size=%ld, handler page size=%ld\n",
		       sysconf(_SC_PAGESIZE), report.page_size);
	ksft_test_result(report_ok &&
			 (report.page_size == PROCESS_PAGE_SIZE ||
			  report.page_size == 4 * PROCESS_PAGE_SIZE),
			 "start a native UFFD unregister handler\n");
	ksft_test_result(report_ok && !report.result,
			 "native handler unregisters the owner range (%s)\n",
			 report.result ? strerror(report.error) : "ok");
	ksft_test_result(WIFEXITED(handler_status) &&
			 WEXITSTATUS(handler_status) == EXIT_SUCCESS,
			 "native handler completes cleanly\n");

	page_is_wp = true;
	pagemap_ok = pagemap_uffd_wp(destination, &page_is_wp);
	ksft_test_result(pagemap_ok && !page_is_wp,
			 "unregister clears the owner UFFD WP bit\n");
	if (pagemap_ok && !page_is_wp)
		*destination = WRITTEN_VALUE;
	ksft_test_result(*destination == WRITTEN_VALUE,
			 "owner can write after unregister\n");

	close(uffd);
	munmap(reservation, RESERVE_SIZE);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], "--owner", NULL);
	if (argc == 2 && !strcmp(mode, "--owner"))
		return run_compat_owner();
	if (argc == 5 && !strcmp(mode, "--handler"))
		return run_native_handler(atoi(argv[2]), strtoul(argv[3], NULL, 10),
					  atoi(argv[4]));
	return EXIT_FAILURE;
}
