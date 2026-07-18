// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_REMOVE through a 4K compat process's shared memfd mapping punches
 * exactly the mapped 4K file range, honouring a 4K slice offset and a range
 * that crosses a native page.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define FILE_PAGES	8
#define FILE_SIZE	(FILE_PAGES * PROCESS_PAGE_SIZE)

static unsigned char page_pattern(unsigned int page)
{
	return 0x31 + page;
}

static bool initialize_file(int fd)
{
	unsigned char page[PROCESS_PAGE_SIZE];
	unsigned int i;

	for (i = 0; i < FILE_PAGES; i++) {
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

static bool file_matches_removed_range(int fd, size_t removed_offset,
				       size_t removed_length)
{
	unsigned char contents[FILE_SIZE];
	ssize_t bytes;
	size_t i;

	bytes = pread(fd, contents, sizeof(contents), 0);
	if (bytes != (ssize_t)sizeof(contents)) {
		ksft_print_msg("pread returned %zd: %s\n", bytes, strerror(errno));
		return false;
	}

	for (i = 0; i < sizeof(contents); i++) {
		unsigned char expected;

		if (i >= removed_offset && i < removed_offset + removed_length)
			expected = 0;
		else
			expected = page_pattern(i / PROCESS_PAGE_SIZE);
		if (contents[i] != expected) {
			ksft_print_msg("file mismatch at offset %#zx: got %#x, expected %#x\n",
				       i, contents[i], expected);
			return false;
		}
	}
	return true;
}

static bool test_remove_range(size_t mapping_offset, size_t length)
{
	void *mapping = MAP_FAILED;
	bool passed = false;
	int fd = -1;

	fd = memfd_create("madvise-remove-ppps", MFD_CLOEXEC);
	if (fd < 0) {
		ksft_print_msg("memfd_create failed: %s\n", strerror(errno));
		goto out;
	}
	if (!initialize_file(fd))
		goto out;

	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		       mapping_offset);
	if (mapping == MAP_FAILED) {
		ksft_print_msg("mmap(offset=%#zx, length=%#zx) failed: %s\n",
			       mapping_offset, length, strerror(errno));
		goto out;
	}
	if (madvise(mapping, length, MADV_REMOVE)) {
		ksft_print_msg("MADV_REMOVE(offset=%#zx, length=%#zx) failed: %s\n",
			       mapping_offset, length, strerror(errno));
		goto out;
	}

	passed = file_matches_removed_range(fd, mapping_offset, length);

out:
	if (mapping != MAP_FAILED)
		munmap(mapping, length);
	if (fd >= 0)
		close(fd);
	return passed;
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(2);

	ksft_test_result(test_remove_range(PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE),
			 "MADV_REMOVE honors a file mapping's 4K slice offset\n");
	ksft_test_result(test_remove_range(3 * PROCESS_PAGE_SIZE,
					   2 * PROCESS_PAGE_SIZE),
			 "MADV_REMOVE handles a range crossing a native page\n");

	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
