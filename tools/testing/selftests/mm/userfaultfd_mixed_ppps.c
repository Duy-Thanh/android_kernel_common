// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat userfaultfd handler resolves a native (16K) owner's missing
 * faults with a one-native-page UFFDIO_COPY and UFFDIO_ZEROPAGE, and the
 * owner reads back all four source slices and the zeroed page.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TEST_SIZE		(2 * NATIVE_PAGE_SIZE)
#define SOURCE_ADDRESS		(0x40000000UL + PROCESS_PAGE_SIZE)
#define TEST_VALUE		0x6b

struct handler_report {
	long page_size;
	long long copied;
	long long zeroed;
	int copy_result;
	int copy_error;
	int zero_result;
	int zero_error;
};

static bool buffer_is_value(const unsigned char *buffer, size_t length,
			    unsigned char value)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (buffer[i] != value)
			return false;
	}
	return true;
}

static int run_compat_handler(int uffd, unsigned long destination,
			      int report_fd)
{
	struct handler_report report = {
		.page_size = sysconf(_SC_PAGESIZE),
		.copy_result = -1,
		.zero_result = -1,
	};
	struct uffdio_copy copy = {
		.dst = destination,
		.len = NATIVE_PAGE_SIZE,
	};
	struct uffdio_zeropage zero = {
		.range = {
			.start = destination + NATIVE_PAGE_SIZE,
			.len = NATIVE_PAGE_SIZE,
		},
	};
	unsigned char *source;

	source = mmap((void *)SOURCE_ADDRESS, NATIVE_PAGE_SIZE,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (source != MAP_FAILED) {
		memset(source, TEST_VALUE, NATIVE_PAGE_SIZE);
		copy.src = (unsigned long)source;
		report.copy_result = ioctl(uffd, UFFDIO_COPY, &copy);
		report.copy_error = errno;
		report.copied = copy.copy;
		if (!report.copy_result) {
			report.zero_result = ioctl(uffd, UFFDIO_ZEROPAGE, &zero);
			report.zero_error = errno;
			report.zeroed = zero.zeropage;
		}
		munmap(source, NATIVE_PAGE_SIZE);
	} else {
		report.copy_error = errno;
	}
	if (!write_full(report_fd, &report, sizeof(report)))
		return EXIT_FAILURE;
	return !report.copy_result && report.copied == NATIVE_PAGE_SIZE &&
	       !report.zero_result && report.zeroed == NATIVE_PAGE_SIZE ?
		EXIT_SUCCESS : EXIT_FAILURE;
}

static int exec_compat_handler(int uffd, unsigned long destination,
			       int report_fd)
{
	char destination_arg[32];
	char report_fd_arg[16];
	char uffd_arg[16];

	snprintf(uffd_arg, sizeof(uffd_arg), "%d", uffd);
	snprintf(destination_arg, sizeof(destination_arg), "%lu", destination);
	snprintf(report_fd_arg, sizeof(report_fd_arg), "%d", report_fd);
	ppps_execl(true, NULL, "--handler", uffd_arg, destination_arg,
		   report_fd_arg, NULL);
	return EXIT_FAILURE;
}

static int run_native_owner(void)
{
	struct uffdio_register registration = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
	};
	struct handler_report report = {};
	unsigned char residency[2] = {};
	unsigned char *destination;
	int report_pipe[2];
	int handler_status = 0;
	pid_t handler;
	int uffd;

	ksft_print_header();
	ksft_set_plan(9);
	ksft_test_result(sysconf(_SC_PAGESIZE) == NATIVE_PAGE_SIZE,
			 "owner process uses native 16K pages\n");
	destination = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uffd = syscall(SYS_userfaultfd, O_NONBLOCK | UFFD_USER_MODE_ONLY);
	if (uffd < 0) {
		if (errno == EPERM || errno == EOPNOTSUPP)
			ksft_exit_skip("userfaultfd unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd failed: %s\n", strerror(errno));
	}
	if (destination == MAP_FAILED || ioctl(uffd, UFFDIO_API, &api))
		ksft_exit_fail_msg("userfaultfd setup failed: %s\n",
				   strerror(errno));
	registration.range.start = (unsigned long)destination;
	registration.range.len = TEST_SIZE;
	ksft_test_result(!ioctl(uffd, UFFDIO_REGISTER, &registration),
			 "register two native-16K missing pages\n");

	if (pipe(report_pipe))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));
	handler = fork();
	if (handler < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!handler) {
		close(report_pipe[0]);
		_exit(exec_compat_handler(uffd, (unsigned long)destination,
					  report_pipe[1]));
	}
	close(report_pipe[1]);
	if (!read_full(report_pipe[0], &report, sizeof(report)))
		ksft_exit_fail_msg("failed to read handler report\n");
	close(report_pipe[0]);
	waitpid(handler, &handler_status, 0);
	ksft_print_msg("owner page size=%ld, handler page size=%ld\n",
		       sysconf(_SC_PAGESIZE), report.page_size);
	ksft_test_result(report.page_size == PROCESS_PAGE_SIZE,
			 "start a 4K compat userfaultfd handler\n");
	ksft_test_result(!report.copy_result &&
			 report.copied == NATIVE_PAGE_SIZE,
			 "4K handler copies one native page (%lld, %s)\n",
			 report.copied,
			 report.copy_result ? strerror(report.copy_error) : "ok");
	ksft_test_result(!mincore(destination, TEST_SIZE, residency) &&
			 (residency[0] & 1),
			 "UFFDIO_COPY populates the native owner page\n");
	ksft_test_result(!report.copy_result &&
			 buffer_is_value(destination, NATIVE_PAGE_SIZE,
					 TEST_VALUE),
			 "native owner reads all four source 4K slices\n");
	ksft_test_result(!report.zero_result &&
			 report.zeroed == NATIVE_PAGE_SIZE,
			 "4K handler zeroes one native page (%lld, %s)\n",
			 report.zeroed,
			 report.zero_result ? strerror(report.zero_error) : "ok");
	ksft_test_result(!report.zero_result && (residency[1] & 1) &&
			 buffer_is_value(destination + NATIVE_PAGE_SIZE,
					 NATIVE_PAGE_SIZE, 0),
			 "native owner reads the zeroed page\n");
	ksft_test_result(WIFEXITED(handler_status) &&
			 WEXITSTATUS(handler_status) == EXIT_SUCCESS,
			 "mixed userfaultfd handler exits cleanly\n");
	close(uffd);
	munmap(destination, TEST_SIZE);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_native(argv[0], "--owner", NULL);
	if (argc == 2 && !strcmp(mode, "--owner"))
		return run_native_owner();
	if (argc == 5 && !strcmp(mode, "--handler"))
		return run_compat_handler(atoi(argv[2]),
					  strtoul(argv[3], NULL, 10),
					  atoi(argv[4]));
	return EXIT_FAILURE;
}
