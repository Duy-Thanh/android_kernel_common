// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process can mmap the multi-page remap_pfn_range() mapping of
 * /sys/kernel/btf/vmlinux and read every 4K page of it without SIGBUS.
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define BTF_PATH "/sys/kernel/btf/vmlinux"

static sigjmp_buf fault_env;

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

static bool mapping_is_readable(const unsigned char *mapping, size_t size,
				size_t *fault_offset, unsigned int *checksum)
{
	unsigned char value;
	size_t offset;

	for (offset = 0; offset < size; offset += PROCESS_PAGE_SIZE) {
		if (!read_byte(&mapping[offset], &value)) {
			*fault_offset = offset;
			return false;
		}
		*checksum = (*checksum * 33) ^ value;
	}
	offset = size - 1;
	if (!read_byte(&mapping[offset], &value)) {
		*fault_offset = offset;
		return false;
	}
	*checksum = (*checksum * 33) ^ value;
	return true;
}

static int run_test(void)
{
	struct sigaction action = {
		.sa_handler = sigbus_handler,
	};
	const unsigned char *mapping;
	unsigned int checksum = 0;
	struct stat status;
	size_t fault_offset = 0;
	bool readable;
	int fd;

	ksft_print_header();
	ksft_set_plan(3);

	fd = ppps_open_fixture_or_skip(BTF_PATH, O_RDONLY);
	if (fstat(fd, &status) || status.st_size <= 0)
		ksft_exit_fail_msg("invalid %s size: %s\n", BTF_PATH,
				   strerror(errno));
	ksft_test_result(status.st_size > (off_t)PROCESS_PAGE_SIZE,
			 "BTF exposes a multi-page PFN mapping\n");

	mapping = mmap(NULL, status.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapping == MAP_FAILED && errno == ENODEV)
		ksft_exit_skip("%s is not mappable on this kernel\n", BTF_PATH);
	ksft_test_result(mapping != MAP_FAILED, "BTF PFN mapping succeeds\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap %s failed: %s\n", BTF_PATH,
				   strerror(errno));
	close(fd);

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	readable = mapping_is_readable(mapping, status.st_size, &fault_offset,
				       &checksum);
	if (!readable)
		ksft_print_msg("SIGBUS at file offset %#zx of %#llx\n",
			       fault_offset, (unsigned long long)status.st_size);
	else
		ksft_print_msg("BTF size=%#llx checksum=%#x\n",
			       (unsigned long long)status.st_size, checksum);
	ksft_test_result(readable,
			 "every valid byte range in the PFN mapping is readable\n");

	munmap((void *)mapping, status.st_size);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
