// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/mman.h>
#include "kselftest_ppps.h"

static int run_test(void)
{
	const unsigned char values[] = { 0x41, 0, 0, 0x62 };
	size_t size = getpagesize();
	unsigned int i;
	int fd;

	ksft_print_header();
	ksft_set_plan(4);
	fd = ppps_open_fixture_or_skip("/dev/vmalloc_align_ppps", O_RDWR);
	for (i = 0; i < 4; i++) {
		unsigned char *map;
		bool ok;
		size_t j;

		errno = 0;
		map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd,
			   (off_t)i * NATIVE_PAGE_SIZE);
		if (i == 1 || i == 2) {
			ok = map == MAP_FAILED && errno == EINVAL;
		} else {
			ok = map != MAP_FAILED;
			if (ok)
				for (j = 0; j < size; j++)
					ok &= map[j] == values[i];
		}
		ksft_test_result(ok, "%s backing case %u (errno=%d)\n",
				 i == 1 || i == 2 ? "reject unaligned" : "map aligned",
				 i, map == MAP_FAILED ? errno : 0);
		if (map != MAP_FAILED)
			munmap(map, size);
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
