// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process mapping a huge=always tmpfs file from a 4K file
 * offset gets an address that is PMD-aligned relative to that offset.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/mount.h>
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

static int run_test(int fd, const char *mount_path)
{
	unsigned long thp_size = read_thp_size();
	const off_t offset = PROCESS_PAGE_SIZE;
	size_t length;
	void *mapping;
	bool aligned;

	ppps_require_compat();
	if (!thp_size)
		ksft_exit_skip("PMD THP size is unavailable\n");
	length = 2 * thp_size;
	ksft_print_header();
	ksft_set_plan(2);
	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fd, offset);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a huge-enabled tmpfs file from a 4K offset\n");
	aligned = mapping != MAP_FAILED &&
		  (((uintptr_t)mapping - offset) & (thp_size - 1)) == 0;
	ksft_test_result(aligned,
			 "align the shmem mapping to its file offset at PMD size\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, length);
	close(fd);
	if (umount(mount_path))
		ksft_print_msg("umount %s failed: %s\n", mount_path,
			       strerror(errno));
	if (rmdir(mount_path))
		ksft_print_msg("rmdir %s failed: %s\n", mount_path,
			       strerror(errno));
	ksft_finished();
}

/*
 * The tmpfs mount and test file are prepared by the top-level invocation
 * and handed to the compat re-exec as "--run <fd> <mount_path>".
 */
int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	char mount_path[] = "./shmem-mmap-offset-ppps-XXXXXX";
	char file_path[sizeof(mount_path) + 16];
	char fd_string[32];
	unsigned long thp_size;
	int fd;

	if (mode && argc == 4 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test(atoi(argv[2]), argv[3]);
	if (argc != 1)
		return EXIT_FAILURE;

	thp_size = read_thp_size();
	if (!thp_size)
		ksft_exit_skip("PMD THP size is unavailable\n");
	if (!mkdtemp(mount_path))
		ksft_exit_fail_msg("mkdtemp failed: %s\n", strerror(errno));
	if (mount("tmpfs", mount_path, "tmpfs", 0,
		  "size=128m,huge=always")) {
		rmdir(mount_path);
		ksft_exit_skip("huge=always tmpfs mount failed: %s\n",
			       strerror(errno));
	}
	snprintf(file_path, sizeof(file_path), "%s/test", mount_path);
	fd = open(file_path, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0)
		ksft_exit_fail_msg("open %s failed: %s\n", file_path,
				   strerror(errno));
	unlink(file_path);
	if (ftruncate(fd, PROCESS_PAGE_SIZE + 2 * thp_size))
		ksft_exit_fail_msg("ftruncate failed: %s\n", strerror(errno));
	snprintf(fd_string, sizeof(fd_string), "%d", fd);
	exec_compat(argv[0], PPPS_RUN_FLAG, fd_string, mount_path, NULL);
}
