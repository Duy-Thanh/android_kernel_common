// SPDX-License-Identifier: GPL-2.0
/*
 * smaps of a 4K compat process's anonymous VMA reports MMUPageSize equal to
 * the 4K process page size and a KernelPageSize of at least that.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

static bool read_page_sizes(uintptr_t address, unsigned long *kernel_size,
			    unsigned long *mmu_size)
{
	char *line = NULL;
	size_t line_size = 0;
	bool in_mapping = false;
	FILE *file;

	*kernel_size = 0;
	*mmu_size = 0;
	file = fopen("/proc/self/smaps", "re");
	if (!file)
		return false;
	while (getline(&line, &line_size, file) >= 0) {
		unsigned long start, end, size_kb;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_mapping = address >= start && address < end;
			continue;
		}
		if (!in_mapping)
			continue;
		if (sscanf(line, "KernelPageSize: %lu kB", &size_kb) == 1)
			*kernel_size = size_kb * 1024;
		else if (sscanf(line, "MMUPageSize: %lu kB", &size_kb) == 1)
			*mmu_size = size_kb * 1024;
		if (*kernel_size && *mmu_size)
			break;
	}
	free(line);
	fclose(file);
	return *kernel_size && *mmu_size;
}

static int run_test(void)
{
	unsigned long kernel_size, mmu_size;
	unsigned char *mapping;
	bool parsed;

	ksft_print_header();
	ksft_set_plan(4);

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(mapping != MAP_FAILED, "create an anonymous mapping\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	*mapping = 0x5a;
	ksft_test_result(*mapping == 0x5a, "fault the mapping\n");

	parsed = read_page_sizes((uintptr_t)mapping, &kernel_size, &mmu_size);
	ksft_print_msg("KernelPageSize=%lu MMUPageSize=%lu\n",
		       kernel_size, mmu_size);
	ksft_test_result(parsed && kernel_size >= PROCESS_PAGE_SIZE,
			 "read the VMA page sizes from smaps\n");
	ksft_test_result(parsed && mmu_size == PROCESS_PAGE_SIZE,
			 "MMUPageSize matches the process page size\n");

	munmap(mapping, PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
