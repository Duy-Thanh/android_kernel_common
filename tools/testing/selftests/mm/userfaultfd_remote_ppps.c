// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) userfaultfd handler resolves one 4K page of a compat owner
 * with UFFDIO_COPY from a tagged source and UFFDIO_ZEROPAGE, and the owner
 * sees exactly those 4K pages populated.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TEST_SIZE	(2 * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(TEST_SIZE + 2 * PROCESS_PAGE_SIZE)
#define OWNER_RESERVE_ADDRESS	((1UL << 28) + 3 * PROCESS_PAGE_SIZE)
#define NATIVE_SOURCE_ADDRESS	(1UL << 40)
#define TEST_VALUE	0x5a

struct handler_report {
	long page_size;
	long long copied;
	long long zeroed;
	int result;
	int error;
	int zero_result;
	int zero_error;
};

static bool buffer_is_value(const unsigned char *buffer, size_t size,
			    unsigned char value)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (buffer[i] != value)
			return false;
	}
	return true;
}

static int run_native_handler(int uffd, unsigned long destination,
			      int report_fd)
{
	struct handler_report report = {
		.page_size = sysconf(_SC_PAGESIZE),
		.result = -1,
		.zero_result = -1,
	};
	struct uffdio_copy copy = {
		.dst = destination,
		.len = PROCESS_PAGE_SIZE,
	};
	struct uffdio_zeropage zeropage = {
		.range.start = destination + PROCESS_PAGE_SIZE,
		.range.len = PROCESS_PAGE_SIZE,
	};
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
	void *mmap_address = NULL;
	bool source_ready = true;
	unsigned char *source;

	if (report.page_size > (long)PROCESS_PAGE_SIZE) {
		mmap_address = (void *)NATIVE_SOURCE_ADDRESS;
		mmap_flags |= MAP_FIXED_NOREPLACE;
	}
	source = mmap(mmap_address, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      mmap_flags, -1, 0);
	if (source != MAP_FAILED) {
		memset(source, TEST_VALUE, PROCESS_PAGE_SIZE);
		copy.src = (unsigned long)source;
#ifdef __aarch64__
		if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0)) {
			report.result = -1;
			report.error = errno;
			source_ready = false;
		}
		copy.src |= 0xabUL << 56;
#endif
		if (source_ready) {
			report.result = ioctl(uffd, UFFDIO_COPY, &copy);
			report.error = errno;
			report.copied = copy.copy;
			if (!report.result) {
				report.zero_result = ioctl(uffd, UFFDIO_ZEROPAGE,
							   &zeropage);
				report.zero_error = errno;
				report.zeroed = zeropage.zeropage;
			}
		}
		munmap(source, PROCESS_PAGE_SIZE);
	} else {
		report.result = -1;
		report.error = errno;
	}
	if (!write_full(report_fd, &report, sizeof(report)))
		return EXIT_FAILURE;
	return report.result == 0 && report.copied == PROCESS_PAGE_SIZE &&
	       report.zero_result == 0 && report.zeroed == PROCESS_PAGE_SIZE ?
		EXIT_SUCCESS : EXIT_FAILURE;
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
	struct uffdio_register registration = {};
	struct uffdio_api api = {
		.api = UFFD_API,
	};
	struct handler_report report = {};
	unsigned char *destination;
	unsigned char *reservation;
	unsigned char residency[2] = {};
	int report_pipe[2];
	int handler_status = 0;
	bool handler_reported;
	bool setup_ok;
	bool copy_resident;
	bool copy_contents_ok = false;
	bool zero_resident;
	bool zero_contents_ok = false;
	pid_t handler;
	int uffd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(7);

	reservation = mmap((void *)OWNER_RESERVE_ADDRESS, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			   -1, 0);
	destination = reservation == MAP_FAILED ? MAP_FAILED :
		mmap(reservation + PROCESS_PAGE_SIZE, TEST_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	uffd = syscall(SYS_userfaultfd, O_NONBLOCK);
	setup_ok = destination != MAP_FAILED && uffd >= 0 &&
		   !ioctl(uffd, UFFDIO_API, &api);
	if (setup_ok) {
		registration.range.start = (unsigned long)destination;
		registration.range.len = TEST_SIZE;
		registration.mode = UFFDIO_REGISTER_MODE_MISSING;
		setup_ok = !ioctl(uffd, UFFDIO_REGISTER, &registration);
	}
	ksft_test_result(setup_ok, "register two isolated missing 4K pages\n");
	if (!setup_ok) {
		if (uffd < 0 && errno == EPERM)
			ksft_exit_skip("userfaultfd unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd setup failed: %s\n",
				   strerror(errno));
	}
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
	handler_reported = read_full(report_pipe[0], &report, sizeof(report));
	close(report_pipe[0]);
	waitpid(handler, &handler_status, 0);
	ksft_test_result(handler_reported &&
			 (report.page_size == PROCESS_PAGE_SIZE ||
			  report.page_size == 4 * PROCESS_PAGE_SIZE),
			 "start a native userfaultfd handler\n");
	ksft_print_msg("owner page size=%ld, handler page size=%ld\n",
		       sysconf(_SC_PAGESIZE), report.page_size);
	ksft_test_result(handler_reported && report.result == 0 &&
			 report.copied == PROCESS_PAGE_SIZE,
			 "native handler copies one 4K page from a tagged source (%lld, %s)\n",
			 report.copied,
			 report.result ? strerror(report.error) : "ok");

	if (mincore(destination, TEST_SIZE, residency)) {
		residency[0] = 0;
		residency[1] = 0;
	}
	copy_resident = residency[0] & 1;
	zero_resident = residency[1] & 1;
	ksft_test_result(copy_resident,
			 "UFFDIO_COPY populates the owner address\n");
	if (copy_resident)
		copy_contents_ok = buffer_is_value(destination, PROCESS_PAGE_SIZE,
						   TEST_VALUE);
	ksft_test_result(copy_contents_ok,
			 "owner reads the copied 4K page contents\n");
	ksft_test_result(handler_reported && report.zero_result == 0 &&
			 report.zeroed == PROCESS_PAGE_SIZE,
			 "native handler zeroes one 4K page (%lld, %s)\n",
			 report.zeroed,
			 report.zero_result ? strerror(report.zero_error) : "ok");
	if (zero_resident)
		zero_contents_ok = buffer_is_value(destination + PROCESS_PAGE_SIZE,
						   PROCESS_PAGE_SIZE, 0);
	ksft_test_result(zero_resident && zero_contents_ok &&
			 WIFEXITED(handler_status) &&
			 WEXITSTATUS(handler_status) == EXIT_SUCCESS,
			 "owner reads the zeroed 4K page contents\n");

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
