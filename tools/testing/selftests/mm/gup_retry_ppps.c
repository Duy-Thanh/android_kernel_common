// SPDX-License-Identifier: GPL-2.0
/*
 * An unlockable GUP over two 4K pages of a compat process, the second one
 * userfaultfd-missing, retries at the missing 4K page (not the native-page
 * shifted address) and ends up pinning both pages.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>

#include "gup_retry_ppps.h"
#include "kselftest_ppps.h"

#define RESERVE_SIZE	(4 * NATIVE_PAGE_SIZE)

#ifdef __BIONIC__
static int pthread_timedjoin_np(pthread_t thread, void **retval,
				const struct timespec *deadline)
{
	(void)deadline;
	return pthread_join(thread, retval);
}
#endif

struct ioctl_thread_args {
	struct gup_retry_ppps_args request;
	int fd;
	int result;
	int error;
};

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(uintptr_t)(alignment - 1);
}

static void *ioctl_thread(void *data)
{
	struct ioctl_thread_args *args = data;

	errno = 0;
	args->result = ioctl(args->fd, GUP_RETRY_PPPS_IOCTL, &args->request);
	args->error = errno;
	return NULL;
}

static int open_userfaultfd(void)
{
	struct uffdio_api api = {
		.api = UFFD_API,
	};
	int uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

	if (uffd < 0)
		return -1;
	if (ioctl(uffd, UFFDIO_API, &api)) {
		close(uffd);
		return -1;
	}
	return uffd;
}

static bool register_missing(int uffd, void *address)
{
	struct uffdio_register registration = {
		.range = {
			.start = (unsigned long)address,
			.len = PROCESS_PAGE_SIZE,
		},
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};

	return !ioctl(uffd, UFFDIO_REGISTER, &registration);
}

static bool wait_for_fault(int uffd, void *address)
{
	struct pollfd pollfd = {
		.fd = uffd,
		.events = POLLIN,
	};
	struct uffd_msg message;

	if (poll(&pollfd, 1, 5000) != 1)
		return false;
	if (read(uffd, &message, sizeof(message)) != sizeof(message))
		return false;
	return message.event == UFFD_EVENT_PAGEFAULT &&
	       (message.arg.pagefault.address & ~(PROCESS_PAGE_SIZE - 1)) ==
	       (unsigned long)address;
}

static bool resolve_fault(int uffd, void *address, void *source)
{
	struct uffdio_copy copy = {
		.src = (unsigned long)source,
		.dst = (unsigned long)address,
		.len = PROCESS_PAGE_SIZE,
	};

	return !ioctl(uffd, UFFDIO_COPY, &copy);
}

static int run_test(void)
{
	struct ioctl_thread_args args = {};
	struct timespec deadline;
	unsigned char *source;
	unsigned char *target;
	unsigned char *reserve;
	pthread_t thread;
	bool faulted;
	bool joined;
	bool resolved;
	bool started;
	int device_fd;
	int uffd;

	ksft_print_header();
	ksft_set_plan(8);

	device_fd = ppps_open_fixture_or_skip("/dev/gup_retry_ppps", O_RDWR);
	uffd = open_userfaultfd();
	ksft_test_result(device_fd >= 0 && uffd >= 0,
			 "enable GUP test device and userfaultfd\n");
	if (device_fd < 0 || uffd < 0)
		ksft_exit_fail_msg("test APIs unavailable: %s\n",
				   strerror(errno));

	reserve = mmap(NULL, RESERVE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	source = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reserve == MAP_FAILED || source == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	target = (void *)align_up((uintptr_t)reserve, NATIVE_PAGE_SIZE);
	ksft_test_result(target + NATIVE_PAGE_SIZE + PROCESS_PAGE_SIZE <=
			 reserve + RESERVE_SIZE,
			 "reserve the correct and native-shift retry targets\n");

	memset(source, 0x22, PROCESS_PAGE_SIZE);
	ksft_test_result(register_missing(uffd, target + PROCESS_PAGE_SIZE),
			 "register the second 4K page as missing\n");
	memset(target, 0x11, PROCESS_PAGE_SIZE);
	memset(target + NATIVE_PAGE_SIZE, 0x33, PROCESS_PAGE_SIZE);

	args.fd = device_fd;
	args.request.address = (uintptr_t)target;
	started = !pthread_create(&thread, NULL, ioctl_thread, &args);
	ksft_test_result(started, "start unlockable GUP across both pages\n");
	if (!started)
		ksft_exit_fail_msg("pthread_create failed\n");

	faulted = wait_for_fault(uffd, target + PROCESS_PAGE_SIZE);
	ksft_test_result(faulted, "observe GUP retry on the second 4K page\n");
	if (!faulted)
		ksft_exit_fail_msg("did not observe the expected fault\n");

	resolved = resolve_fault(uffd, target + PROCESS_PAGE_SIZE, source);
	ksft_test_result(resolved, "resolve the second 4K page fault\n");
	if (!resolved)
		ksft_exit_fail_msg("UFFDIO_COPY failed: %s\n", strerror(errno));

	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 5;
	joined = !pthread_timedjoin_np(thread, NULL, &deadline);
	ksft_test_result(joined && !args.result &&
			 args.request.nr_pinned == 2,
			 "unlockable GUP pins both 4K pages\n");
	if (joined)
		ksft_print_msg("ioctl result=%d errno=%d pinned=%lld match=%u\n",
			       args.result, args.error,
			       (long long)args.request.nr_pinned,
			       args.request.second_matches_expected);

	ksft_test_result(joined && args.request.second_matches_expected,
			 "GUP retry resumes at the missing 4K page\n");

	munmap(source, PROCESS_PAGE_SIZE);
	munmap(reserve, RESERVE_SIZE);
	close(uffd);
	close(device_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
