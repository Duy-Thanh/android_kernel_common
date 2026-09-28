// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_COLD over a 2MB shared file mapping placed across a 4K compat
 * process's PMD boundary keeps every 4K file slice's contents and leaves
 * the kernel free of a bad-page taint.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define MAP_SIZE (2 * 1024 * 1024UL)
#define MAP_ADDR ((void *)0x40001000UL)
#define TAINT_BAD_PAGE_MASK 32UL

static int failures;
static int test_no;

static void result(int pass, const char *name)
{
	printf("%s %d - %s\n", pass ? "ok" : "not ok", ++test_no, name);
	if (!pass)
		failures++;
}

static void skip(const char *name, const char *reason)
{
	printf("ok %d - %s # SKIP %s\n", ++test_no, name, reason);
}

static int read_kernel_taint(unsigned long *taint)
{
	FILE *file;
	int rc;

	file = fopen("/proc/sys/kernel/tainted", "re");
	if (!file)
		return -1;
	rc = fscanf(file, "%lu", taint) == 1 ? 0 : -1;
	fclose(file);
	return rc;
}

static unsigned char slice_pattern(size_t offset)
{
	return ((offset / PROCESS_PAGE_SIZE) % 251) + 1;
}

static int prepare_file(int fd)
{
	unsigned char buffer[PROCESS_PAGE_SIZE];
	size_t offset;
	int rc;

	for (offset = 0; offset < MAP_SIZE; offset += PROCESS_PAGE_SIZE) {
		memset(buffer, slice_pattern(offset), sizeof(buffer));
		if (pwrite(fd, buffer, sizeof(buffer), offset) !=
		    (ssize_t)sizeof(buffer))
			return -1;
	}
	if (fsync(fd))
		return -1;
	rc = posix_fadvise(fd, 0, MAP_SIZE, POSIX_FADV_DONTNEED);
	if (rc) {
		errno = rc;
		return -1;
	}
	return 0;
}

static size_t mapping_mismatches(const unsigned char *map,
				 size_t *checksum)
{
	size_t mismatches = 0;
	size_t offset;

	*checksum = 0;
	for (offset = 0; offset < MAP_SIZE; offset += PROCESS_PAGE_SIZE) {
		*checksum += map[offset];
		if (map[offset] != slice_pattern(offset))
			mismatches++;
	}
	return mismatches;
}

static int run_test(const char *file)
{
	unsigned char *map;
	unsigned long taint_after;
	unsigned long taint_before;
	size_t checksum;
	size_t mismatches;
	int fd;
	int rc;
	int taint_available;

	printf("TAP version 13\n1..4\n");
	taint_available = read_kernel_taint(&taint_before) == 0;

	fd = open(file, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	rc = fd < 0 ? -1 : prepare_file(fd);
	if (rc)
		perror("prepare patterned file");
	result(rc == 0, "create a patterned 2MB file");
	if (rc)
		goto out_close;

	map = mmap(MAP_ADDR, MAP_SIZE, PROT_READ,
		   MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		result(0, "fault a file mapping across a process PMD");
		goto out_close;
	}
	mismatches = mapping_mismatches(map, &checksum);
	printf("# checksum before MADV_COLD=%zu mismatches=%zu\n",
	       checksum, mismatches);
	result(map == MAP_ADDR && !mismatches,
	       "map distinct file slices across a process PMD");

	rc = madvise(map, MAP_SIZE, MADV_COLD);
	if (rc)
		perror("madvise(MADV_COLD)");
	mismatches = mapping_mismatches(map, &checksum);
	printf("# checksum after MADV_COLD=%zu mismatches=%zu\n",
	       checksum, mismatches);
	result(rc == 0 && !mismatches,
	       "MADV_COLD preserves distinct file slices");

	munmap(map, MAP_SIZE);
out_close:
	if (fd >= 0)
		close(fd);
	unlink(file);
	if (!taint_available || read_kernel_taint(&taint_after))
		skip("MADV_COLD keeps page-cache accounting valid",
		     "kernel taint state is unavailable");
	else if (taint_before & TAINT_BAD_PAGE_MASK)
		skip("MADV_COLD keeps page-cache accounting valid",
		     "kernel was already tainted by a bad page");
	else
		result(!(taint_after & TAINT_BAD_PAGE_MASK),
		       "MADV_COLD keeps page-cache accounting valid");
	printf("# Totals: pass:%d fail:%d\n", test_no - failures, failures);
	return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode) {
		if (argc != 2) {
			printf("Bail out! usage: %s FILE\n", argv[0]);
			return 1;
		}
		if (!ppps_is_compat_process())
			exec_compat(argv[0], PPPS_RUN_FLAG, argv[1], NULL);
		return run_test(argv[1]);
	}
	if (argc == 3 && !strcmp(mode, PPPS_RUN_FLAG)) {
		ppps_require_compat();
		return run_test(argv[2]);
	}
	return EXIT_FAILURE;
}
