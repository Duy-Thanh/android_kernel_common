// SPDX-License-Identifier: GPL-2.0
/*
 * Truncating a memfd at a native-page boundary under a 4K compat process's
 * two-slice mapping keeps the slice below the new EOF readable and makes the
 * slice at the new EOF fault with SIGBUS.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAPPING_OFFSET	(3 * PROCESS_PAGE_SIZE)
#define MAPPING_SIZE	(2 * PROCESS_PAGE_SIZE)
#define FILE_SIZE	(8 * PROCESS_PAGE_SIZE)
#define TRUNCATED_SIZE	(4 * PROCESS_PAGE_SIZE)

static sigjmp_buf fault_env;

static unsigned char page_pattern(unsigned int page)
{
	return 0x41 + page;
}

static bool initialize_file(int fd)
{
	unsigned char page[PROCESS_PAGE_SIZE];
	unsigned int i;

	for (i = 0; i < FILE_SIZE / PROCESS_PAGE_SIZE; i++) {
		ssize_t written;

		memset(page, page_pattern(i), sizeof(page));
		written = pwrite(fd, page, sizeof(page), i * PROCESS_PAGE_SIZE);
		if (written != (ssize_t)sizeof(page)) {
			ksft_print_msg("pwrite page %u returned %zd: %s\n", i,
				       written, strerror(errno));
			return false;
		}
	}
	return true;
}

static void sigbus_handler(int signal_number)
{
	siglongjmp(fault_env, signal_number);
}

static bool read_byte(const unsigned char *address, unsigned char *value)
{
	if (sigsetjmp(fault_env, 1))
		return false;
	*value = *address;
	return true;
}

static int run_test(void)
{
	unsigned char *mapping;
	struct sigaction action = {
		.sa_handler = sigbus_handler,
	};
	unsigned char truncated_value = 0;
	bool initial_contents;
	bool truncated_ok;
	bool truncated_readable;
	int fd;

	ksft_print_header();
	ksft_set_plan(4);

	fd = memfd_create("truncate-ppps", MFD_CLOEXEC);
	if (fd < 0)
		ksft_exit_fail_msg("memfd_create failed: %s\n", strerror(errno));
	if (!initialize_file(fd))
		ksft_exit_fail_msg("failed to initialize memfd\n");
	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ, MAP_SHARED, fd,
		       MAPPING_OFFSET);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	initial_contents = mapping[0] == page_pattern(3) &&
			   mapping[PROCESS_PAGE_SIZE] == page_pattern(4);
	ksft_test_result(initial_contents,
			 "sliced mapping initially exposes both file pages\n");

	truncated_ok = !ftruncate(fd, TRUNCATED_SIZE);
	ksft_test_result(truncated_ok, "truncate at the native-page boundary succeeds\n");
	if (!truncated_ok)
		ksft_print_msg("ftruncate failed: %s\n", strerror(errno));

	ksft_test_result(mapping[0] == page_pattern(3),
			 "file data below the new EOF remains accessible\n");
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	truncated_readable = read_byte(&mapping[PROCESS_PAGE_SIZE],
				       &truncated_value);
	ksft_test_result(!truncated_readable,
			 "file data at the new EOF is unmapped and faults with SIGBUS\n");
	if (truncated_readable)
		ksft_print_msg("post-EOF access returned %#x\n", truncated_value);

	munmap(mapping, MAPPING_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
