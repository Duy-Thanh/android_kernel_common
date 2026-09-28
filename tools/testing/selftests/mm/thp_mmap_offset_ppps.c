// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process mapping a PMD-sized file range from a 4K file offset
 * gets a mapping whose address minus that offset is PMD-aligned (THP mmap
 * alignment honours the 4K file offset).
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define THP_SIZE_PATH "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size"

static unsigned long read_thp_size(void)
{
	char buffer[64];
	char *end;
	ssize_t bytes;
	unsigned long size;
	int fd;

	fd = open(THP_SIZE_PATH, O_RDONLY);
	if (fd < 0)
		return 0;
	bytes = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (bytes <= 0)
		return 0;
	buffer[bytes] = '\0';
	errno = 0;
	size = strtoul(buffer, &end, 10);
	if (errno || end == buffer || size < PROCESS_PAGE_SIZE ||
	    (size & (size - 1)))
		return 0;
	return size;
}

static int run_test(int fd)
{
	unsigned long thp_size = read_thp_size();
	const off_t offset = PROCESS_PAGE_SIZE;
	size_t length;
	void *mapping;
	bool aligned;

	if (!thp_size)
		ksft_exit_skip("PMD THP size is unavailable\n");
	length = 2 * thp_size;
	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(2);
	mapping = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, offset);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a file from a 4K offset\n");
	aligned = mapping != MAP_FAILED &&
		   (((uintptr_t)mapping - offset) & (thp_size - 1)) == 0;
	ksft_test_result(aligned,
			 "align the mapping to its file offset at PMD size\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, length);
	close(fd);
	ksft_finished();
}

/* Create the test file at @path and re-exec as a compat process with its fd. */
static void __noreturn exec_compat_run(const char *argv0, const char *path)
{
	char fd_string[32];
	unsigned long thp_size;
	int fd;

	thp_size = read_thp_size();
	if (!thp_size)
		ksft_exit_skip("PMD THP size is unavailable\n");
	fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0)
		ksft_exit_fail_msg("open %s failed: %s\n", path,
				   strerror(errno));
	unlink(path);
	if (ftruncate(fd, PROCESS_PAGE_SIZE + 2 * thp_size))
		ksft_exit_fail_msg("ftruncate failed: %s\n", strerror(errno));
	snprintf(fd_string, sizeof(fd_string), "%d", fd);
	exec_compat(argv0, PPPS_RUN_FLAG, fd_string, NULL);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	char path[] = "./thp-mmap-offset-ppps-XXXXXX";
	int fd;

	if (!mode && argc == 1) {
		fd = mkstemp(path);
		if (fd < 0)
			ksft_exit_fail_msg("mkstemp failed: %s\n",
					   strerror(errno));
		close(fd);
		unlink(path);
		exec_compat_run(argv[0], path);
	}
	if (!mode && argc == 2)
		exec_compat_run(argv[0], argv[1]);
	if (mode && argc == 3 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test(atoi(argv[2]));
	return EXIT_FAILURE;
}
