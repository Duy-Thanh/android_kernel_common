// SPDX-License-Identifier: GPL-2.0
/*
 * A shared file mapping in a 4K compat process observes EOF at 4K
 * granularity: the partial final 4K page is readable and the first complete
 * 4K page beyond EOF raises SIGBUS, at file offset zero and at a 4K offset.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define FILE_SIZE 5000UL

static int read_status(const unsigned char *address)
{
	int status;
	pid_t child = fork();

	if (child < 0)
		return -1;
	if (!child)
		_exit(!!*address);
	if (waitpid(child, &status, 0) != child)
		return -1;
	return status;
}

static bool read_succeeds(const unsigned char *address)
{
	int status = read_status(address);

	return status >= 0 && WIFEXITED(status);
}

static bool read_gets_sigbus(const unsigned char *address)
{
	int status = read_status(address);

	return status >= 0 && WIFSIGNALED(status) &&
	       WTERMSIG(status) == SIGBUS;
}

static int run_compat_test(const char *mount_dir)
{
	unsigned char marker = 0x5a;
	unsigned char *mapping;
	unsigned char *offset_mapping;
	char path[256];
	bool initialized;
	int fd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(8);

	if (snprintf(path, sizeof(path), "%s/filemap-eof.bin", mount_dir) >=
	    (int)sizeof(path))
		ksft_exit_fail_msg("test path is too long\n");
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0600);
	initialized = fd >= 0 && !ftruncate(fd, FILE_SIZE) &&
		pwrite(fd, &marker, 1, 0) == 1 &&
		pwrite(fd, &marker, 1, PROCESS_PAGE_SIZE) == 1 && !fsync(fd);
	ksft_test_result(initialized, "create a 5000-byte regular file\n");
	if (!initialized)
		ksft_exit_fail_msg("file initialization failed: %s\n",
				   strerror(errno));

	mapping = mmap(NULL, 3 * PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
		       0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map three 4K pages at file offset zero\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("first mmap failed: %s\n", strerror(errno));
	ksft_test_result(read_succeeds(mapping),
			 "first file page is readable\n");
	ksft_test_result(read_succeeds(mapping + PROCESS_PAGE_SIZE),
			 "partial final file page is readable\n");
	ksft_test_result(read_gets_sigbus(mapping + 2 * PROCESS_PAGE_SIZE),
			 "first complete 4K page beyond EOF raises SIGBUS\n");

	offset_mapping = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_READ,
			      MAP_SHARED, fd, PROCESS_PAGE_SIZE);
	ksft_test_result(offset_mapping != MAP_FAILED,
			 "map two 4K pages at a 4K file offset\n");
	if (offset_mapping == MAP_FAILED)
		ksft_exit_fail_msg("offset mmap failed: %s\n", strerror(errno));
	ksft_test_result(read_succeeds(offset_mapping),
			 "offset mapping starts in the file\n");
	ksft_test_result(read_gets_sigbus(offset_mapping + PROCESS_PAGE_SIZE),
			 "offset mapping preserves 4K EOF SIGBUS semantics\n");

	munmap(offset_mapping, 2 * PROCESS_PAGE_SIZE);
	munmap(mapping, 3 * PROCESS_PAGE_SIZE);
	close(fd);
	unlink(path);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (argc == 2 && !mode)
		exec_compat(argv[0], "--compat", argv[1], NULL);
	if (argc == 3 && mode && !strcmp(mode, "--compat"))
		return run_compat_test(argv[2]);
	return EXIT_FAILURE;
}
