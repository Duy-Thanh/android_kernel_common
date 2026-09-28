// SPDX-License-Identifier: GPL-2.0
/*
 * The kernel's fault_in_writeable/safe_writeable/readable helpers fault in
 * every 4K process page of a compat process's nonresident range, not just
 * one page per native page.
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "fault_in_ppps.h"
#include "kselftest_ppps.h"

#define RANGE_SIZE (4 * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE (8 * RANGE_SIZE)

static const char *const operation_names[] = {
	[FAULT_IN_PPPS_WRITEABLE] = "fault_in_writeable",
	[FAULT_IN_PPPS_SAFE_WRITEABLE] = "fault_in_safe_writeable",
	[FAULT_IN_PPPS_READABLE] = "fault_in_readable",
};

static unsigned char *aligned_mapping(void)
{
	unsigned char *reservation;
	unsigned long aligned;
	unsigned char *mapping;

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return MAP_FAILED;
	aligned = ((unsigned long)reservation + RANGE_SIZE - 1) &
		  ~(RANGE_SIZE - 1);
	mapping = mmap((void *)aligned, RANGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapping == MAP_FAILED) {
		munmap(reservation, RESERVE_SIZE);
		return MAP_FAILED;
	}
	if (aligned > (unsigned long)reservation)
		munmap(reservation, aligned - (unsigned long)reservation);
	if (aligned + RANGE_SIZE < (unsigned long)reservation + RESERVE_SIZE)
		munmap((void *)(aligned + RANGE_SIZE),
		       (unsigned long)reservation + RESERVE_SIZE -
		       aligned - RANGE_SIZE);
	return mapping;
}

static bool all_nonresident(unsigned char *mapping)
{
	unsigned char vec[RANGE_SIZE / PROCESS_PAGE_SIZE];
	size_t i;

	if (mincore(mapping, RANGE_SIZE, vec))
		return false;
	for (i = 0; i < sizeof(vec); i++) {
		if (vec[i] & 1)
			return false;
	}
	return true;
}

static bool all_resident(unsigned char *mapping, unsigned char *vec)
{
	size_t i;

	if (mincore(mapping, RANGE_SIZE, vec))
		return false;
	for (i = 0; i < RANGE_SIZE / PROCESS_PAGE_SIZE; i++) {
		vec[i] &= 1;
		if (!vec[i])
			return false;
	}
	return true;
}

static bool run_operation(int fd, unsigned int operation)
{
	unsigned char vec[RANGE_SIZE / PROCESS_PAGE_SIZE] = {};
	struct fault_in_ppps_args request = {};
	unsigned char *mapping;
	bool resident;

	mapping = aligned_mapping();
	if (mapping == MAP_FAILED) {
		ksft_print_msg("mmap failed: %s\n", strerror(errno));
		return false;
	}
	if (madvise(mapping, RANGE_SIZE, MADV_DONTNEED) ||
	    !all_nonresident(mapping)) {
		ksft_print_msg("could not establish nonresident range\n");
		munmap(mapping, RANGE_SIZE);
		return false;
	}

	request.address = (unsigned long)mapping;
	request.length = RANGE_SIZE;
	request.operation = operation;
	if (ioctl(fd, FAULT_IN_PPPS_IOCTL, &request)) {
		ksft_print_msg("%s ioctl failed: %s\n",
			       operation_names[operation], strerror(errno));
		munmap(mapping, RANGE_SIZE);
		return false;
	}
	resident = all_resident(mapping, vec);
	ksft_print_msg("%s: not_faulted=%llu residency=%u,%u,%u,%u\n",
		       operation_names[operation],
		       (unsigned long long)request.not_faulted,
		       vec[0], vec[1], vec[2], vec[3]);
	munmap(mapping, RANGE_SIZE);
	return request.not_faulted == 0 && resident;
}

static int run_test(void)
{
	unsigned int operation;
	int fd;

	ksft_print_header();
	ksft_set_plan(3);
	fd = ppps_open_fixture_or_skip("/dev/" FAULT_IN_PPPS_DEVICE_NAME,
				       O_RDWR);
	for (operation = 0; operation < 3; operation++) {
		bool passed = run_operation(fd, operation);

		ksft_test_result(passed, "%s faults every process page\n",
				 operation_names[operation]);
	}
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
