// SPDX-License-Identifier: GPL-2.0
/*
 * The in-kernel page walker fixture write-protects and cleans only the
 * requested 4K file slice of a compat process, without touching the
 * adjacent anonymous pages or the other slices of the native page.
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"
#include "pagewalk_ppps.h"

#define RESERVE_SIZE	(64 * 1024UL)
#define DEVICE_PATH	"/dev/" PAGEWALK_PPPS_DEVICE_NAME

static int create_memfd(void)
{
	int fd = memfd_create("pagewalk-ppps", MFD_CLOEXEC);

	if (fd >= 0 && ftruncate(fd, RESERVE_SIZE)) {
		close(fd);
		return -1;
	}
	return fd;
}

static unsigned char *reserve_range(void)
{
	return mmap(NULL, RESERVE_SIZE, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static bool test_write_protect_overrun(int device_fd, unsigned long *count)
{
	struct pagewalk_ppps_args request = {
		.offset = 4 * PROCESS_PAGE_SIZE,
		.length = PROCESS_PAGE_SIZE,
	};
	const size_t file_len = 5 * PROCESS_PAGE_SIZE;
	const size_t tail_len = 3 * PROCESS_PAGE_SIZE;
	unsigned char *base;
	unsigned char *tail;
	size_t offset;
	int fd;

	fd = create_memfd();
	if (fd < 0)
		return false;
	base = reserve_range();
	if (base == MAP_FAILED)
		goto close_fd;
	if (mmap(base, file_len, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
		goto unmap;
	tail = mmap(base + file_len, tail_len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (tail == MAP_FAILED)
		goto unmap;

	for (offset = 0; offset < file_len; offset += PROCESS_PAGE_SIZE)
		base[offset] = 0x40 + offset / PROCESS_PAGE_SIZE;
	for (offset = 0; offset < tail_len; offset += PROCESS_PAGE_SIZE)
		tail[offset] = 0x60 + offset / PROCESS_PAGE_SIZE;

	request.fd = fd;
	if (ioctl(device_fd, PAGEWALK_PPPS_IOCTL_WRITE_PROTECT, &request))
		goto unmap;
	*count = request.count;
	munmap(base, RESERVE_SIZE);
	close(fd);
	return true;

unmap:
	munmap(base, RESERVE_SIZE);
close_fd:
	close(fd);
	return false;
}

static bool test_clean_slice(int device_fd, unsigned long *count,
			     unsigned long *bitmap_weight)
{
	struct pagewalk_ppps_args request = {
		.offset = 4 * PROCESS_PAGE_SIZE,
		.length = PROCESS_PAGE_SIZE,
	};
	const size_t file_offset = PROCESS_PAGE_SIZE;
	const size_t file_len = 4 * PROCESS_PAGE_SIZE;
	unsigned char *base;
	int fd;

	fd = create_memfd();
	if (fd < 0)
		return false;
	base = reserve_range();
	if (base == MAP_FAILED)
		goto close_fd;
	if (mmap(base, file_len, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_FIXED, fd, file_offset) == MAP_FAILED)
		goto unmap;

	base[3 * PROCESS_PAGE_SIZE] = 0x7a;
	request.fd = fd;
	if (ioctl(device_fd, PAGEWALK_PPPS_IOCTL_CLEAN, &request))
		goto unmap;
	*count = request.count;
	*bitmap_weight = request.bitmap_weight;
	munmap(base, RESERVE_SIZE);
	close(fd);
	return true;

unmap:
	munmap(base, RESERVE_SIZE);
close_fd:
	close(fd);
	return false;
}

static int run_test(void)
{
	unsigned long clean_bitmap_weight = 0;
	unsigned long write_protected = 0;
	unsigned long cleaned = 0;
	bool clean_ok;
	bool wp_ok;
	int device_fd;

	ksft_print_header();
	ksft_set_plan(6);

	device_fd = ppps_open_fixture_or_skip(DEVICE_PATH, O_RDWR);
	ksft_test_result(true, "open the page-walk test helper\n");

	wp_ok = test_write_protect_overrun(device_fd, &write_protected);
	ksft_test_result(wp_ok, "create a file VMA followed by anonymous pages\n");
	ksft_test_result(wp_ok && write_protected == 1,
			 "write-protect only the requested file page\n");
	ksft_print_msg("write-protected PTEs=%lu (expected 1)\n",
		       write_protected);

	clean_ok = test_clean_slice(device_fd, &cleaned,
				    &clean_bitmap_weight);
	ksft_test_result(clean_ok, "create a sliced file mapping\n");
	ksft_test_result(clean_ok && cleaned == 1,
			 "clean the dirty PTE in the requested native page\n");
	ksft_test_result(clean_ok && clean_bitmap_weight == 1,
			 "record the correct native page in the dirty bitmap\n");
	ksft_print_msg("cleaned PTEs=%lu bitmap weight=%lu (expected 1, 1)\n",
		       cleaned, clean_bitmap_weight);

	close(device_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
