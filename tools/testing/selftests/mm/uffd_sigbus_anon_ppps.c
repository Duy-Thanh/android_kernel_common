// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <linux/userfaultfd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define SLICES 4
#define ROUNDS 1000

static unsigned char *destination;
static unsigned char *source;
static int uffd;
static volatile sig_atomic_t handler_failed;

static void sigbus_handler(int sig, siginfo_t *info, void *context)
{
	unsigned long fault = (unsigned long)info->si_addr & ~(PROCESS_PAGE_SIZE - 1);
	unsigned long index = (fault - (unsigned long)destination) / PROCESS_PAGE_SIZE;
	struct uffdio_copy copy = {
		.dst = fault,
		.src = (unsigned long)source + index * PROCESS_PAGE_SIZE,
		.len = PROCESS_PAGE_SIZE,
	};

	(void)sig;
	(void)context;
	if (fault < (unsigned long)destination || index >= SLICES ||
	    ioctl(uffd, UFFDIO_COPY, &copy) || copy.copy != PROCESS_PAGE_SIZE)
		handler_failed = 1;
}

static int run_test(void)
{
	static const unsigned int order[SLICES] = { 2, 0, 3, 1 };
	struct uffdio_register registration = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_SIGBUS,
	};
	struct sigaction action = {
		.sa_sigaction = sigbus_handler,
		.sa_flags = SA_SIGINFO | SA_NODEFER,
	};
	unsigned int round, slice;
	bool passed = true;

	ksft_print_header();
	ksft_set_plan(3);
	ksft_test_result(sysconf(_SC_PAGESIZE) == PROCESS_PAGE_SIZE,
			 "process uses 4K compatibility pages\n");

	destination = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	source = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uffd = syscall(SYS_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
	if (destination == MAP_FAILED || source == MAP_FAILED || uffd < 0 ||
	    sigaction(SIGBUS, &action, NULL) || ioctl(uffd, UFFDIO_API, &api))
		ksft_exit_fail_msg("setup failed: %s\n", strerror(errno));

	registration.range.start = (unsigned long)destination;
	registration.range.len = NATIVE_PAGE_SIZE;
	ksft_test_result(!ioctl(uffd, UFFDIO_REGISTER, &registration),
			 "register anonymous range with UFFD SIGBUS mode\n");

	for (round = 0; round < ROUNDS && passed; round++) {
		for (slice = 0; slice < SLICES; slice++) {
			memset(source + slice * PROCESS_PAGE_SIZE,
			       0x31 + slice + (round & 7), PROCESS_PAGE_SIZE);
			memset(destination + slice * PROCESS_PAGE_SIZE,
			       0xa1 + slice, PROCESS_PAGE_SIZE);
		}
		if (madvise(destination, NATIVE_PAGE_SIZE, MADV_DONTNEED))
			ksft_exit_fail_msg("MADV_DONTNEED failed: %s\n", strerror(errno));
		handler_failed = 0;
		for (slice = 0; slice < SLICES; slice++) {
			unsigned int victim = order[slice];
			unsigned char expected = 0x31 + victim + (round & 7);
			unsigned char *page = destination + victim * PROCESS_PAGE_SIZE;
			size_t i;

			if (page[0] != expected || handler_failed) {
				passed = false;
				break;
			}
			for (i = 1; i < PROCESS_PAGE_SIZE; i++) {
				if (page[i] != expected) {
					ksft_print_msg("round %u slice %u offset %zu: %#x != %#x\n",
						       round, victim, i, page[i], expected);
					passed = false;
					break;
				}
			}
			if (!passed)
				break;
		}
	}
	ksft_test_result(passed && !handler_failed,
			 "SIGBUS handler restores all four packed slices for %u rounds\n",
			 round);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		ppps_execl(true, NULL, "--run", NULL);
	if (argc == 2 && !strcmp(mode, "--run"))
		return run_test();
	return EXIT_FAILURE;
}
