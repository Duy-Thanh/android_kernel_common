// SPDX-License-Identifier: GPL-2.0
/*
 * The in-kernel iov_iter page extraction helpers, driven by the
 * iov_iter_ppps fixture on a 4K compat process's buffers, return the exact
 * 4K slices for a file mapping at a mismatched native offset and for a
 * packed anonymous tuple, and bulk extraction stops at one process page.
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"
#include "iov_iter_ppps.h"

#define FILE_TEST_LENGTH (2 * PROCESS_PAGE_SIZE)
#define ANON_TEST_LENGTH NATIVE_PAGE_SIZE
#define PHYS_SLICE_TEST_LENGTH NATIVE_PAGE_SIZE

static const unsigned char expected[] = { 0x31, 0x72, 0x93, 0xb4 };

static void *map_test_file(int *fd_out, void **reservation_out)
{
	char path[] = "/tmp/iov-iter-XXXXXX";
	uintptr_t aligned;
	void *reservation;
	void *mapping;
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		return MAP_FAILED;
	unlink(path);
	if (ftruncate(fd, NATIVE_PAGE_SIZE))
		goto err;

	reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		goto err;
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	mapping = mmap((void *)(aligned + PROCESS_PAGE_SIZE), FILE_TEST_LENGTH,
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	if (mapping == MAP_FAILED) {
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
		goto err;
	}

	*fd_out = fd;
	*reservation_out = reservation;
	return mapping;

err:
	close(fd);
	return MAP_FAILED;
}

static unsigned char *map_test_anon(void **reservation_out)
{
	uintptr_t aligned;
	void *reservation;

	reservation = mmap(NULL, 2 * NATIVE_PAGE_SIZE,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return MAP_FAILED;
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	*reservation_out = reservation;
	return (unsigned char *)aligned;
}

static void populate(unsigned char *mapping, size_t length)
{
	unsigned int i;

	for (i = 0; i < length / PROCESS_PAGE_SIZE; i++)
		mapping[i * PROCESS_PAGE_SIZE] = expected[i];
}

static void run_iov_checks(int device_fd, unsigned char *mapping,
			   size_t length, unsigned int flags,
			   const char *description)
{
	struct iov_iter_ppps_args request = {
		.address = (uintptr_t)mapping,
		.length = length,
		.flags = flags,
	};

	ksft_test_result(ioctl(device_fd, IOV_ITER_PPPS_IOCTL, &request) == 0,
			 "run kernel %s iov_iter checks\n", description);
	ksft_test_result(request.get_pages_result == 0,
			 "iov_iter_get_pages2 returns correct %s slices (%d)\n",
			 description, request.get_pages_result);
	ksft_test_result(request.extract_pages_result == 0,
			 "iov_iter_extract_pages returns correct %s slices (%d)\n",
			 description, request.extract_pages_result);
	ksft_test_result(request.bulk_first_len ==
			 (int)(request.native_page_size == PROCESS_PAGE_SIZE ?
			       length : PROCESS_PAGE_SIZE),
			 "compat %s bulk extraction stops at one process page (%d)\n",
			 description, request.bulk_first_len);
	ksft_test_result(request.npages_result == 0,
			 "iov_iter_npages counts and caps %s process pages (%d)\n",
			 description, request.npages_result);
}

static int run_test(void)
{
	void *reservation = MAP_FAILED;
	void *anon_reservation = MAP_FAILED;
	unsigned char *phys_slice_mapping;
	unsigned char *anon_mapping;
	unsigned char *mapping;
	int backing_fd = -1;
	int device_fd;

	ksft_print_header();
	ksft_set_plan(19);

	device_fd = ppps_open_fixture_or_skip("/dev/" IOV_ITER_PPPS_DEVICE_NAME,
					     O_RDWR);
	ksft_test_result(device_fd >= 0, "open the iov_iter test device\n");

	mapping = map_test_file(&backing_fd, &reservation);
	ksft_test_result(mapping != MAP_FAILED,
			 "map two file slices at a mismatched native offset\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("test mapping failed: %s\n", strerror(errno));
	populate(mapping, FILE_TEST_LENGTH);
	run_iov_checks(device_fd, mapping, FILE_TEST_LENGTH, 0, "file");

	anon_mapping = map_test_anon(&anon_reservation);
	ksft_test_result(anon_mapping != MAP_FAILED,
			 "map one native-page-aligned anonymous tuple\n");
	if (anon_mapping == MAP_FAILED)
		ksft_exit_fail_msg("anonymous mapping failed: %s\n",
				   strerror(errno));
	populate(anon_mapping, ANON_TEST_LENGTH);
	run_iov_checks(device_fd, anon_mapping, ANON_TEST_LENGTH,
		       IOV_ITER_PPPS_F_EXPECT_PACKED, "packed anonymous");

	phys_slice_mapping = mmap(NULL, PHYS_SLICE_TEST_LENGTH,
				  PROT_READ | PROT_WRITE, MAP_SHARED,
				  device_fd, 0);
	ksft_test_result(phys_slice_mapping != MAP_FAILED,
			 "map deliberately non-linear physical slices\n");
	if (phys_slice_mapping == MAP_FAILED)
		ksft_exit_fail_msg("physical-slice mapping failed: %s\n",
				   strerror(errno));
	populate(phys_slice_mapping, PHYS_SLICE_TEST_LENGTH);
	run_iov_checks(device_fd, phys_slice_mapping, PHYS_SLICE_TEST_LENGTH,
		       0, "non-linear physical");

	munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	munmap(anon_reservation, 2 * NATIVE_PAGE_SIZE);
	munmap(phys_slice_mapping, PHYS_SLICE_TEST_LENGTH);
	close(backing_fd);
	close(device_fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
