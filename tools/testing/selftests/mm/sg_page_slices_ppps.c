// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include "kselftest_ppps.h"
#include "sg_page_slices_ppps.h"

static const char * const descriptions[] = {
	"one full native page",
	"partial page with byte-granular endpoints",
	"consecutive slices keep separate SG entries",
	"reordered slices preserve logical byte order",
	"alternating backing pages preserve offsets",
	"overlapping aliases are not deduplicated",
	"chained SG table preserves every byte",
	"empty page array is rejected",
	"empty byte size is rejected",
	"NULL page after a valid prefix releases SG storage",
	"zero length after a valid prefix is rejected",
	"offset at native-page end is rejected",
	"slice extending past native-page end is rejected",
	"size smaller than span sum is rejected",
	"size larger than span sum is rejected",
	"overflow-sized length is rejected",
};

static int run_test(void)
{
	unsigned int i;
	int fd;

	ksft_print_header();
	ksft_set_plan(SG_SLICES_CASES);
	fd = ppps_open_fixture_or_skip("/dev/sg_page_slices_ppps", O_RDWR);
	for (i = 0; i < SG_SLICES_CASES; i++) {
		int ret = ioctl(fd, SG_PAGE_SLICES_PPPS_CHECK, (unsigned long)i);

		ksft_test_result(!ret, "%s (errno=%d)\n", descriptions[i],
				 ret ? errno : 0);
	}
	close(fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (getpagesize() != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
