// SPDX-License-Identifier: GPL-2.0
/*
 * remap_file_pages() in a 4K compat process accepts a 4K-aligned address and
 * size within a shared memfd mapping and remaps exactly that one 4K file
 * slice, leaving the neighbouring pages untouched.
 */
#define _GNU_SOURCE

#include <asm/unistd.h>
#include <linux/memfd.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define FILE_PAGES		8
#define MAPPING_PAGES		4
#define REMAP_FILE_PAGE		5

static unsigned char page_pattern(unsigned int page)
{
	return 0x41 + page;
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

static bool mapping_has_expected_pages(const unsigned char *mapping)
{
	unsigned int page;

	for (page = 0; page < MAPPING_PAGES; page++) {
		unsigned char expected = page_pattern(page);
		size_t i;

		if (page == 1)
			expected = page_pattern(REMAP_FILE_PAGE);
		for (i = 0; i < PROCESS_PAGE_SIZE; i++) {
			unsigned char value = mapping[page * PROCESS_PAGE_SIZE + i];

			if (value != expected) {
				ksft_print_msg("page %u byte %#zx: got %#x, expected %#x\n",
					       page, i, value, expected);
				return false;
			}
		}
	}
	return true;
}

static int run_test(void)
{
	const size_t mapping_size = MAPPING_PAGES * PROCESS_PAGE_SIZE;
	unsigned char *mapping = MAP_FAILED;
	bool remap_succeeded;
	int fd = -1;

	ksft_print_header();
	ksft_set_plan(2);

	fd = memfd_create("remap-file-pages-ppps", MFD_CLOEXEC);
	if (fd < 0)
		ksft_exit_fail_msg("memfd_create failed: %s\n", strerror(errno));
	if (!initialize_file(fd))
		ksft_exit_fail_msg("failed to initialize memfd\n");

	mapping = mmap(NULL, mapping_size, PROT_READ, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	errno = 0;
	remap_succeeded = !syscall(__NR_remap_file_pages,
				   mapping + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				   0, REMAP_FILE_PAGE, 0);
	if (!remap_succeeded)
		ksft_print_msg("remap_file_pages at a 4K-only-aligned address failed: %s\n",
			       strerror(errno));
	ksft_test_result(remap_succeeded,
			 "remap_file_pages accepts a 4K address and size\n");
	ksft_test_result(remap_succeeded && mapping_has_expected_pages(mapping),
			 "remap_file_pages remaps exactly one 4K file slice\n");

	munmap(mapping, mapping_size);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
