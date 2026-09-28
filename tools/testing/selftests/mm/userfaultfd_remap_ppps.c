// SPDX-License-Identifier: GPL-2.0
/*
 * mremap() of a userfaultfd-registered 4K page to a 4K-only-aligned address
 * in a compat process delivers an exact UFFD_EVENT_REMAP and preserves the
 * moved contents.
 */
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define SOURCE_ADDRESS		0x20000000UL
#define DESTINATION_ADDRESS	(0x30000000UL + PROCESS_PAGE_SIZE)
#define TEST_VALUE		0x5a

struct monitor_result {
	int uffd;
	int poll_result;
	int read_result;
	int error;
	struct uffd_msg message;
};

static void *monitor_remap(void *arg)
{
	struct monitor_result *result = arg;
	struct pollfd pollfd = {
		.fd = result->uffd,
		.events = POLLIN,
	};

	result->poll_result = poll(&pollfd, 1, 2000);
	if (result->poll_result <= 0) {
		result->error = errno;
		return NULL;
	}
	result->read_result = read(result->uffd, &result->message,
				   sizeof(result->message));
	if (result->read_result < 0)
		result->error = errno;
	return NULL;
}

static int run_test(void)
{
	struct uffdio_register registration = {
		.range = {
			.start = SOURCE_ADDRESS,
			.len = PROCESS_PAGE_SIZE,
		},
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_EVENT_REMAP,
	};
	struct monitor_result monitor = {};
	unsigned char *destination;
	unsigned char *source;
	pthread_t thread;
	void *moved;
	int uffd;

	ksft_print_header();
	ksft_set_plan(5);
	ksft_test_result(!(SOURCE_ADDRESS & (NATIVE_PAGE_SIZE - 1)) &&
			 !(DESTINATION_ADDRESS & (PROCESS_PAGE_SIZE - 1)) &&
			 (DESTINATION_ADDRESS & (NATIVE_PAGE_SIZE - 1)),
			 "destination is 4K-only aligned\n");

	source = mmap((void *)SOURCE_ADDRESS, PROCESS_PAGE_SIZE,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	destination = mmap((void *)DESTINATION_ADDRESS, PROCESS_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
			   -1, 0);
	if (source == MAP_FAILED || destination == MAP_FAILED)
		ksft_exit_fail_msg("fixed mmap failed: %s\n", strerror(errno));
	memset(source, TEST_VALUE, PROCESS_PAGE_SIZE);

	uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK |
		       UFFD_USER_MODE_ONLY);
	if (uffd < 0) {
		if (errno == EPERM || errno == EOPNOTSUPP)
			ksft_exit_skip("userfaultfd unavailable: %s\n",
				       strerror(errno));
		ksft_exit_fail_msg("userfaultfd failed: %s\n", strerror(errno));
	}
	if (ioctl(uffd, UFFDIO_API, &api))
		ksft_exit_fail_msg("UFFDIO_API failed: %s\n", strerror(errno));
	ksft_test_result(api.features & UFFD_FEATURE_EVENT_REMAP,
			 "kernel supports userfaultfd remap events\n");
	if (ioctl(uffd, UFFDIO_REGISTER, &registration))
		ksft_exit_fail_msg("UFFDIO_REGISTER failed: %s\n",
				   strerror(errno));

	monitor.uffd = uffd;
	if (pthread_create(&thread, NULL, monitor_remap, &monitor))
		ksft_exit_fail_msg("pthread_create failed\n");
	moved = mremap(source, PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED, destination);
	ksft_test_result(moved == destination,
			 "move registered VMA to 4K-only address\n");
	pthread_join(thread, NULL);
	ksft_test_result(monitor.poll_result == 1 &&
			 monitor.read_result == sizeof(monitor.message) &&
			 monitor.message.event == UFFD_EVENT_REMAP &&
			 monitor.message.arg.remap.from == SOURCE_ADDRESS &&
			 monitor.message.arg.remap.to == DESTINATION_ADDRESS &&
			 monitor.message.arg.remap.len == PROCESS_PAGE_SIZE,
			 "receive exact remap event (poll=%d read=%d event=%u error=%s)\n",
			 monitor.poll_result, monitor.read_result,
			 monitor.message.event,
			 monitor.error ? strerror(monitor.error) : "none");
	ksft_test_result(moved == destination &&
			 destination[0] == TEST_VALUE &&
			 destination[PROCESS_PAGE_SIZE - 1] == TEST_VALUE,
			 "moved mapping preserves contents\n");

	close(uffd);
	munmap(destination, PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
