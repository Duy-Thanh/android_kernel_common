// SPDX-License-Identifier: GPL-2.0
/*
 * The in-kernel folio_within_range() fixture, driven from a 4K compat
 * process, recognizes a fully covered native folio, rejects one only
 * partially covered at the VMA start and stops a rmap walk at the folio end.
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>

#include "kselftest_ppps.h"
#include "folio_within_range_ppps.h"

static int run_test(void)
{
	int fd;

	ksft_print_header();
	ksft_set_plan(4);

	fd = ppps_open_fixture_or_skip("/dev/folio_within_range_ppps",
				       O_RDONLY);
	ksft_test_result(fd >= 0, "open the folio range fixture\n");

	ksft_test_result(ioctl(fd,
			       FOLIO_WITHIN_RANGE_PPPS_CHECK_ALIGNED) == 0,
			 "recognize a completely covered native folio\n");
	ksft_test_result(ioctl(fd,
			       FOLIO_WITHIN_RANGE_PPPS_CHECK_SUBPAGE) == 0,
			 "reject a folio only partially covered at VMA start\n");
	ksft_test_result(ioctl(fd,
			       FOLIO_WITHIN_RANGE_PPPS_CHECK_ADDRESS_END) == 0,
			 "stop a reverse-map walk at the end of the native folio\n");

	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
