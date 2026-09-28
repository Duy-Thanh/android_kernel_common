// SPDX-License-Identifier: GPL-2.0
/*
 * A shared 9p file mapping of a 4K compat process that crosses a native folio
 * boundary: dirtying the trailing 4K page and closing the VMA writes back the
 * trailing native folio (kpageflags no longer reports it dirty).
 */
#define _GNU_SOURCE

#include <limits.h>
#include <linux/kernel-page-flags.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAPPING_OFFSET ((1024 - 1) * PROCESS_PAGE_SIZE)
#define MAPPING_SIZE (2 * PROCESS_PAGE_SIZE)
#define WRITE_OFFSET PROCESS_PAGE_SIZE
#define FILE_SIZE (2048 * PROCESS_PAGE_SIZE)
#define TEST_VALUE 0x7b
#define BIT_ULL(nr) (1ULL << (nr))

static bool pfn_is_dirty(uint64_t pfn, bool *dirty)
{
	uint64_t flags;
	off_t offset;
	int fd;
	bool valid;

	fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	offset = pfn * sizeof(flags);
	valid = pread(fd, &flags, sizeof(flags), offset) ==
		(ssize_t)sizeof(flags);
	close(fd);
	if (valid)
		*dirty = flags & BIT_ULL(KPF_DIRTY);
	return valid;
}

static int run_test(const char *cached_dir)
{
	char cached_path[PATH_MAX];
	unsigned char *mapping;
	unsigned char zero = 0;
	uint64_t pfn;
	bool dirty;
	bool resolved;
	int fd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(3);
	if (snprintf(cached_path, sizeof(cached_path), "%s/mmap-close.bin",
		     cached_dir) >= (int)sizeof(cached_path))
		ksft_exit_fail_msg("9p test path is too long\n");

	fd = open(cached_path, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0 || ftruncate(fd, FILE_SIZE) ||
	    pwrite(fd, &zero, 1, MAPPING_OFFSET + WRITE_OFFSET) != 1 ||
	    fsync(fd))
		ksft_exit_fail_msg("initialize 9p file failed: %s\n",
				   strerror(errno));
	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fd, MAPPING_OFFSET);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a range crossing a native folio boundary\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("9p mmap failed: %s\n", strerror(errno));

	mapping[WRITE_OFFSET] = TEST_VALUE;
	resolved = ppps_pfn(mapping + WRITE_OFFSET, &pfn);
	ksft_test_result(resolved,
			 "resolve the trailing folio through pagemap\n");
	if (!resolved)
		ksft_exit_fail_msg("pagemap PFN is unavailable\n");
	munmap(mapping, MAPPING_SIZE);
	if (!pfn_is_dirty(pfn, &dirty))
		ksft_exit_fail_msg("kpageflags is unavailable: %s\n",
				   strerror(errno));
	ksft_test_result(!dirty,
			 "VMA close writes back the trailing native folio\n");
	close(fd);
	unlink(cached_path);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode && argc == 2)
		exec_compat(argv[0], "--run", argv[1], NULL);
	if (mode && argc == 3 && !strcmp(mode, "--run"))
		return run_test(argv[2]);
	return EXIT_FAILURE;
}
