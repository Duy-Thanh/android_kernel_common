// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process maps the KCOV coverage buffer at a 4K offset from a
 * native-page boundary, collects coverage spanning several 4K slices and
 * reads every entry back without a mapping fault.
 */
#define _GNU_SOURCE

#include <linux/kcov.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define COVER_ENTRIES (512UL << 10)
#define COVER_BYTES (COVER_ENTRIES * sizeof(unsigned long))

static sigjmp_buf fault_environment;

static void fault_handler(int signal_number)
{
	siglongjmp(fault_environment, signal_number);
}

static bool read_coverage(const unsigned long *cover, unsigned long count)
{
	unsigned long checksum = 0;
	unsigned long i;

	if (sigsetjmp(fault_environment, 1))
		return false;
	for (i = 1; i <= count; i++)
		checksum ^= cover[i];
	return checksum != 0;
}

static int run_test(void)
{
	struct sigaction action = {
		.sa_handler = fault_handler,
	};
	unsigned long *cover;
	unsigned long count;
	unsigned long aligned;
	void *reservation;
	unsigned int i;
	int fd;

	ksft_print_header();
	ksft_set_plan(4);

	fd = ppps_open_fixture_or_skip("/sys/kernel/debug/kcov", O_RDWR);
	ksft_test_result(fd >= 0, "open KCOV debugfs file\n");
	if (ioctl(fd, KCOV_INIT_TRACE, COVER_ENTRIES))
		ksft_exit_fail_msg("KCOV_INIT_TRACE failed: %s\n",
				   strerror(errno));

	reservation = mmap(NULL, COVER_BYTES + 2 * NATIVE_PAGE_SIZE,
			   PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("guard reservation failed: %s\n",
				   strerror(errno));
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	cover = mmap((void *)(aligned + PROCESS_PAGE_SIZE), COVER_BYTES,
		     PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	ksft_test_result(cover == (void *)(aligned + PROCESS_PAGE_SIZE),
			 "map KCOV after a 4K guard slice\n");
	if (cover == MAP_FAILED)
		ksft_exit_fail_msg("KCOV mmap failed: %s\n", strerror(errno));

	if (ioctl(fd, KCOV_ENABLE, KCOV_TRACE_PC))
		ksft_exit_fail_msg("KCOV_ENABLE failed: %s\n", strerror(errno));
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);
	for (i = 0; i < 64; i++)
		syscall(__NR_getpid);
	if (ioctl(fd, KCOV_DISABLE, 0))
		ksft_exit_fail_msg("KCOV_DISABLE failed: %s\n", strerror(errno));

	count = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	ksft_test_result(count > PROCESS_PAGE_SIZE / sizeof(*cover) &&
			 count < COVER_ENTRIES,
			 "collect coverage across multiple 4K slices\n");

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL) ||
	    sigaction(SIGSEGV, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	ksft_test_result(count < COVER_ENTRIES && read_coverage(cover, count),
			 "read every KCOV entry without a mapping fault\n");

	munmap(reservation, COVER_BYTES + 2 * NATIVE_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
