// SPDX-License-Identifier: GPL-2.0
/*
 * vm_map_pages() through the vm_map_pages_ppps fixture maps five 4K slices at
 * a 4K file offset for a 4K compat process, each with a present PTE holding
 * the expected logical page, and a forked child inherits all of them.
 *
 * The vm_insert_pages() edge cases need symbols that a kernel built with
 * CONFIG_TRIM_UNUSED_KSYMS does not export; the fixture then fails those
 * mmap() calls with EOPNOTSUPP and they are reported as skipped.
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"
#include "vm_map_pages_ppps.h"

#define FILE_OFFSET	PROCESS_PAGE_SIZE
#define MAPPING_PAGES	5
#define MAPPING_SIZE	(MAPPING_PAGES * PROCESS_PAGE_SIZE)
#define FIRST_MARKER	0x41

static sigjmp_buf fault_environment;

static void sigbus_handler(int signal_number)
{
	siglongjmp(fault_environment, signal_number);
}

static bool read_mapping(const unsigned char *mapping, unsigned char *value)
{
	if (sigsetjmp(fault_environment, 1))
		return false;
	*value = *mapping;
	return true;
}

enum edge_result {
	EDGE_FAIL,
	EDGE_PASS,
	EDGE_UNSUPPORTED,
};

static void report_edge_case(enum edge_result result, const char *name)
{
	if (result == EDGE_UNSUPPORTED)
		ksft_test_result_skip("%s: the fixture cannot use vm_insert_pages() on this kernel\n",
				      name);
	else
		ksft_test_result(result == EDGE_PASS, "%s\n", name);
}

static enum edge_result mmap_edge_case(int fd, unsigned long offset,
				       bool readable)
{
	unsigned char value;
	unsigned char *mapping;
	bool result;

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, offset * PROCESS_PAGE_SIZE);
	if (mapping == MAP_FAILED)
		return errno == EOPNOTSUPP ? EDGE_UNSUPPORTED : EDGE_FAIL;
	result = read_mapping(mapping, &value) == readable;
	munmap(mapping, PROCESS_PAGE_SIZE);
	return result ? EDGE_PASS : EDGE_FAIL;
}

static enum edge_result native_partial_insert_rolled_back(int fd,
							   enum edge_result *conflict)
{
	unsigned char value;
	unsigned char *mapping;
	bool prefix_faults;

	*conflict = EDGE_FAIL;
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd,
		       VM_MAP_PAGES_PPPS_NATIVE_PARTIAL * PROCESS_PAGE_SIZE);
	if (mapping == MAP_FAILED) {
		if (errno != EOPNOTSUPP)
			return EDGE_FAIL;
		*conflict = EDGE_UNSUPPORTED;
		return EDGE_UNSUPPORTED;
	}
	prefix_faults = !read_mapping(mapping, &value);
	if (read_mapping(mapping + PROCESS_PAGE_SIZE, &value) && value == 0x44)
		*conflict = EDGE_PASS;
	munmap(mapping, NATIVE_PAGE_SIZE);
	return prefix_faults ? EDGE_PASS : EDGE_FAIL;
}

static int run_test(void)
{
	struct sigaction action = {
		.sa_handler = sigbus_handler,
	};
	unsigned char value = 0;
	unsigned char *mapping;
	bool readable;
	bool marker_ok;
	enum edge_result rollback, conflict;
	unsigned int page;
	int fd, status;
	pid_t pid;

	ksft_print_header();
	ksft_set_plan(12);

	fd = ppps_open_fixture_or_skip("/dev/vm_map_pages_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open the vm_map_pages test device\n");

	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, FILE_OFFSET);
	ksft_test_result(mapping != MAP_FAILED,
			 "map five 4K slices at file offset 4K\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("vm_map_pages mmap failed: %s\n",
				   strerror(errno));

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL))
		ksft_exit_fail_msg("sigaction failed: %s\n", strerror(errno));
	readable = true;
	marker_ok = true;
	for (page = 0; page < MAPPING_PAGES; page++) {
		unsigned char expected = FIRST_MARKER + page;

		if (!read_mapping(mapping + page * PROCESS_PAGE_SIZE, &value)) {
			ksft_print_msg("SIGBUS at logical page %u\n", page + 1);
			readable = false;
			marker_ok = false;
			break;
		}
		if (value != expected) {
			ksft_print_msg("logical page %u value=%#x expected=%#x\n",
				       page + 1, value, expected);
			marker_ok = false;
		}
	}
	ksft_test_result(readable,
			 "every vm_map_pages slice has a present PTE\n");
	ksft_test_result(marker_ok,
			 "the mapped slices contain logical pages 1 through 5\n");

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!pid) {
		for (page = 0; page < MAPPING_PAGES; page++) {
			unsigned char expected = FIRST_MARKER + page;

			if (!read_mapping(mapping + page * PROCESS_PAGE_SIZE,
					  &value) || value != expected)
				_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}
	if (waitpid(pid, &status, 0) != pid)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	ksft_test_result(WIFEXITED(status) && !WEXITSTATUS(status),
			 "fork inherits every vm_map_pages slice\n");

	report_edge_case(mmap_edge_case(fd, VM_MAP_PAGES_PPPS_ZERO, false),
			 "zero-page vm_insert_pages request is a no-op");
	report_edge_case(mmap_edge_case(fd, VM_MAP_PAGES_PPPS_BEFORE, false),
			 "vm_insert_pages rejects an address before the VMA");
	report_edge_case(mmap_edge_case(fd, VM_MAP_PAGES_PPPS_AFTER, false),
			 "vm_insert_pages rejects an address at the VMA end");
	report_edge_case(mmap_edge_case(fd, VM_MAP_PAGES_PPPS_TOO_MANY, false),
			 "vm_insert_pages rejects too many native pages");
	report_edge_case(mmap_edge_case(fd, VM_MAP_PAGES_PPPS_BUSY, true),
			 "vm_insert_pages reports a duplicate PTE and remaining page");
	rollback = native_partial_insert_rolled_back(fd, &conflict);
	report_edge_case(rollback,
			 "vm_insert_page_native rolls back a prefix on failure");
	report_edge_case(conflict,
			 "rollback preserves the pre-existing conflicting PTE");

	munmap(mapping, MAPPING_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
