// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) userfaultfd handler poisons one 4K page of a compat owner
 * with UFFDIO_POISON; the owner can unregister the range and a reader of the
 * poisoned page is killed with SIGBUS.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE		(3 * PROCESS_PAGE_SIZE)
#define OWNER_RESERVE_ADDRESS	((1UL << 28) + 15 * PROCESS_PAGE_SIZE)

struct handler_report {
	long page_size;
	int result;
	int error;
	__s64 updated;
};

static int run_native_handler(int uffd, unsigned long destination,
			      int report_fd)
{
	struct uffdio_poison poison = {
		.range.start = destination,
		.range.len = PROCESS_PAGE_SIZE,
	};
	struct handler_report report = {
		.page_size = sysconf(_SC_PAGESIZE),
		.result = -1,
	};

	errno = 0;
	report.result = ioctl(uffd, UFFDIO_POISON, &poison);
	report.error = errno;
	report.updated = poison.updated;
	if (!write_full(report_fd, &report, sizeof(report)))
		return EXIT_FAILURE;
	return !report.result && report.updated == PROCESS_PAGE_SIZE ?
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
	struct uffdio_register registration = {
		.range.len = PROCESS_PAGE_SIZE,
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_POISON,
	};
	struct handler_report report = {};
	struct uffdio_range unregister_range;
	unsigned char *destination;
	unsigned char *reservation;
	bool report_ok;
	bool setup_ok;
	int handler_status = 0;
	int reader_status = 0;
	int report_pipe[2];
	pid_t handler;
	pid_t reader;
	int uffd;
	int unregister_result;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(6);

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
		   api.features & UFFD_FEATURE_POISON;
	if (setup_ok) {
		registration.range.start = (unsigned long)destination;
		setup_ok = !ioctl(uffd, UFFDIO_REGISTER, &registration) &&
			   registration.ioctls & (1ULL << _UFFDIO_POISON);
	}
	ksft_test_result(setup_ok, "register one 4K page for UFFD poison\n");
	if (!setup_ok) {
		if (uffd < 0 && errno == EPERM)
			ksft_exit_skip("userfaultfd unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd poison setup failed: %s\n",
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
	report_ok = read_full(report_pipe[0], &report, sizeof(report));
	close(report_pipe[0]);
	waitpid(handler, &handler_status, 0);
	ksft_print_msg("owner page size=%ld, handler page size=%ld\n",
		       sysconf(_SC_PAGESIZE), report.page_size);
	ksft_test_result(report_ok &&
			 (report.page_size == PROCESS_PAGE_SIZE ||
			  report.page_size == 4 * PROCESS_PAGE_SIZE),
			 "start a native UFFD poison handler\n");
	ksft_test_result(report_ok && !report.result &&
			 report.updated == PROCESS_PAGE_SIZE,
			 "native handler poisons one owner page (%s, %lld bytes)\n",
			 report.result ? strerror(report.error) : "ok",
			 (long long)report.updated);
	ksft_test_result(WIFEXITED(handler_status) &&
			 WEXITSTATUS(handler_status) == EXIT_SUCCESS,
			 "native handler completes cleanly\n");

	unregister_range.start = (unsigned long)destination;
	unregister_range.len = PROCESS_PAGE_SIZE;
	unregister_result = ioctl(uffd, UFFDIO_UNREGISTER, &unregister_range);
	ksft_test_result(!unregister_result,
			 "owner unregisters the poisoned range\n");
	close(uffd);

	reader = fork();
	if (reader < 0)
		ksft_exit_fail_msg("reader fork failed: %s\n", strerror(errno));
	if (!reader)
		_exit(!!*destination);
	waitpid(reader, &reader_status, 0);
	ksft_test_result(WIFSIGNALED(reader_status) &&
			 WTERMSIG(reader_status) == SIGBUS,
			 "reader receives SIGBUS from the poisoned owner page\n");

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
