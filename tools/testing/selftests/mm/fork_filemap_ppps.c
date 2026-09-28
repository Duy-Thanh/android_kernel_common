// SPDX-License-Identifier: GPL-2.0
/*
 * A forked child of a 4K compat process faults the same non-native-aligned
 * private file page as its parent, even after the VMA slab has been seeded
 * with objects carrying a different slice offset.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "kselftest_ppps.h"

#define TARGET_OFFSET		PROCESS_PAGE_SIZE
#define POISON_OFFSET		(3 * PROCESS_PAGE_SIZE)
#define POISON_MAPPINGS		256
#define POISON_STRIDE		(2 * NATIVE_PAGE_SIZE)
#define FILE_PAGES		4

/* A private file whose pages each carry a distinct byte pattern. */
static int create_backing_file(void)
{
	char path[] = "./fork_filemap_ppps.XXXXXX";
	unsigned char page[PROCESS_PAGE_SIZE];
	int fd = mkstemp(path);
	int i;

	if (fd < 0)
		return -1;
	unlink(path);
	for (i = 0; i < FILE_PAGES; i++) {
		memset(page, 0x40 + i, sizeof(page));
		if (pwrite(fd, page, sizeof(page), i * PROCESS_PAGE_SIZE) !=
		    (ssize_t)sizeof(page)) {
			close(fd);
			return -1;
		}
	}
	return fd;
}

static int run_test(void)
{
	unsigned char expected[PROCESS_PAGE_SIZE];
	unsigned char *poison_region;
	unsigned char *poison_base;
	size_t poison_region_size;
	unsigned char *mapping;
	struct stat statbuf;
	pid_t child;
	int fd;
	int status;
	int i;

	ksft_print_header();
	ksft_set_plan(5);

	fd = create_backing_file();
	ksft_test_result(fd >= 0, "create a four-page backing file\n");
	if (fd < 0)
		ksft_exit_fail_msg("backing file failed: %s\n", strerror(errno));
	if (fstat(fd, &statbuf) ||
	    statbuf.st_size < (off_t)(FILE_PAGES * PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("backing file is too small\n");
	if (pread(fd, expected, sizeof(expected), TARGET_OFFSET) !=
	    sizeof(expected))
		ksft_exit_fail_msg("pread failed: %s\n", strerror(errno));

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd,
		       TARGET_OFFSET);
	ksft_test_result(mapping != MAP_FAILED,
			 "map a non-native-aligned file page\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("target mmap failed: %s\n", strerror(errno));
	ksft_test_result(!memcmp(mapping, expected, sizeof(expected)),
			 "parent reads the expected file page\n");

	/*
	 * Fill the VMA slab freelist with objects carrying a different PPPS
	 * slice offset.  A fork must initialize every field of the duplicated
	 * VMA rather than depending on stale slab contents.
	 */
	poison_region_size = (POISON_MAPPINGS + 1) * POISON_STRIDE;
	poison_region = mmap(NULL, poison_region_size, PROT_NONE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (poison_region == MAP_FAILED)
		ksft_exit_fail_msg("poison reservation failed: %s\n",
				   strerror(errno));
	poison_base = (unsigned char *)(((uintptr_t)poison_region +
					NATIVE_PAGE_SIZE - 1) &
				       ~(NATIVE_PAGE_SIZE - 1));
	for (i = 0; i < POISON_MAPPINGS; i++) {
		unsigned char *address = poison_base + i * POISON_STRIDE +
					 POISON_OFFSET;
		void *poison;

		poison = mmap(address, PROCESS_PAGE_SIZE, PROT_READ,
			      MAP_PRIVATE | MAP_FIXED, fd, POISON_OFFSET);
		if (poison != address)
			ksft_exit_fail_msg("poison mmap failed: %s\n",
					   strerror(errno));
	}
	for (i = 0; i < POISON_MAPPINGS; i++) {
		unsigned char *address = poison_base + i * POISON_STRIDE;

		if (munmap(address, NATIVE_PAGE_SIZE))
			ksft_exit_fail_msg("poison munmap failed: %s\n",
					   strerror(errno));
	}

	child = fork();
	ksft_test_result(child >= 0, "fork the process\n");
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child)
		_exit(memcmp(mapping, expected, sizeof(expected)) ? 1 : 0);

	if (waitpid(child, &status, 0) != child)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	ksft_test_result(WIFEXITED(status) && WEXITSTATUS(status) == 0,
			 "child faults the same file page after fork\n");

	munmap(mapping, PROCESS_PAGE_SIZE);
	munmap(poison_region, poison_region_size);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
