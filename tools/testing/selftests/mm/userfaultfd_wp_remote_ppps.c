// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) userfaultfd handler write-protects one 4K page of a compat
 * owner, receives the owner's WP fault at the 4K address, and lifts the
 * protection so the owner's write completes with the requested contents.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE		(3 * PROCESS_PAGE_SIZE)
#define OWNER_RESERVE_ADDRESS	((1UL << 28) + 3 * PROCESS_PAGE_SIZE)
#define INITIAL_VALUE		0x5a
#define WRITTEN_VALUE		0xa5

struct wp_setup_report {
	long page_size;
	int result;
	int error;
};

struct wp_fault_report {
	long poll_result;
	ssize_t read_result;
	__u64 event;
	__u64 flags;
	unsigned long address;
	int unprotect_result;
	int unprotect_error;
};

static int run_native_handler(int uffd, unsigned long destination,
			      int report_fd)
{
	struct wp_setup_report setup = {
		.page_size = sysconf(_SC_PAGESIZE),
		.result = -1,
	};
	struct wp_fault_report fault = {
		.poll_result = -1,
		.read_result = -1,
		.unprotect_result = -1,
	};
	struct uffdio_writeprotect wp = {
		.range.start = destination,
		.range.len = PROCESS_PAGE_SIZE,
		.mode = UFFDIO_WRITEPROTECT_MODE_WP,
	};
	struct pollfd pollfd = {
		.fd = uffd,
		.events = POLLIN,
	};
	struct uffd_msg message = {};

	errno = 0;
	setup.result = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);
	setup.error = errno;
	if (!write_full(report_fd, &setup, sizeof(setup)))
		return EXIT_FAILURE;

	if (!setup.result) {
		fault.poll_result = poll(&pollfd, 1, 1000);
		if (fault.poll_result > 0 && pollfd.revents & POLLIN) {
			fault.read_result = read(uffd, &message, sizeof(message));
			if (fault.read_result == sizeof(message)) {
				fault.event = message.event;
				fault.flags = message.arg.pagefault.flags;
				fault.address = message.arg.pagefault.address;
			}
		}
		wp.mode = 0;
		errno = 0;
		fault.unprotect_result = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);
		fault.unprotect_error = errno;
	}

	if (!write_full(report_fd, &fault, sizeof(fault)))
		return EXIT_FAILURE;
	return setup.result == 0 && fault.poll_result > 0 &&
	       fault.read_result == sizeof(message) &&
	       fault.event == UFFD_EVENT_PAGEFAULT &&
	       fault.flags & UFFD_PAGEFAULT_FLAG_WP &&
	       fault.address == destination && !fault.unprotect_result ?
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
		.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP,
	};
	struct wp_setup_report setup = {};
	struct wp_fault_report fault = {};
	unsigned char *destination;
	unsigned char *reservation;
	int report_pipe[2];
	int handler_status = 0;
	bool setup_ok;
	bool setup_reported;
	bool fault_reported;
	bool write_completed = false;
	pid_t handler;
	int uffd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(9);

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
		registration.range.start = (unsigned long)destination;
		registration.range.len = PROCESS_PAGE_SIZE;
		registration.mode = UFFDIO_REGISTER_MODE_WP;
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
	*destination = INITIAL_VALUE;

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
	setup_reported = read_full(report_pipe[0], &setup, sizeof(setup));
	ksft_test_result(setup_reported &&
			 (setup.page_size == PROCESS_PAGE_SIZE ||
			  setup.page_size == 4 * PROCESS_PAGE_SIZE),
			 "start a native UFFD WP handler\n");
	ksft_print_msg("owner page size=%ld, handler page size=%ld\n",
		       sysconf(_SC_PAGESIZE), setup.page_size);
	ksft_test_result(setup_reported && !setup.result,
			 "native handler write-protects one owner page (%s)\n",
			 setup.result ? strerror(setup.error) : "ok");

	if (setup_reported && !setup.result) {
		*destination = WRITTEN_VALUE;
		write_completed = true;
	}
	fault_reported = read_full(report_pipe[0], &fault, sizeof(fault));
	close(report_pipe[0]);
	waitpid(handler, &handler_status, 0);

	ksft_test_result(fault_reported && fault.poll_result > 0 &&
			 fault.read_result == sizeof(struct uffd_msg) &&
			 fault.event == UFFD_EVENT_PAGEFAULT,
			 "native handler receives the owner WP fault\n");
	ksft_test_result(fault_reported &&
			 fault.flags & UFFD_PAGEFAULT_FLAG_WP,
			 "fault carries UFFD_PAGEFAULT_FLAG_WP\n");
	ksft_test_result(fault_reported &&
			 fault.address == (unsigned long)destination,
			 "fault address belongs to the 4K owner\n");
	ksft_test_result(fault_reported && !fault.unprotect_result,
			 "native handler removes owner write protection (%s)\n",
			 fault.unprotect_result ?
			 strerror(fault.unprotect_error) : "ok");
	ksft_test_result(write_completed && *destination == WRITTEN_VALUE,
			 "owner write resumes with the requested contents\n");
	ksft_test_result(WIFEXITED(handler_status) &&
			 WEXITSTATUS(handler_status) == EXIT_SUCCESS,
			 "native handler completes cleanly\n");

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
