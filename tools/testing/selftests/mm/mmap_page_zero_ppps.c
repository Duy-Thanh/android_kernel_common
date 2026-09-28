// SPDX-License-Identifier: GPL-2.0
/*
 * With MMAP_PAGE_ZERO in its personality, a 4K compat process gets a
 * page-zero mapping that spans exactly one process page.
 */
#define _GNU_SOURCE

#include "kselftest_ppps.h"

static int read_zero_mapping(unsigned long *endp, char perms[5])
{
	char line[256];
	FILE *maps;

	maps = fopen("/proc/self/maps", "re");
	if (!maps)
		return -1;
	while (fgets(line, sizeof(line), maps)) {
		unsigned long start, end;

		if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3 &&
		    start == 0) {
			*endp = end;
			fclose(maps);
			return 0;
		}
	}
	fclose(maps);
	return -1;
}

static int run_test(void)
{
	unsigned long page_size = sysconf(_SC_PAGESIZE);
	unsigned long end = 0;
	char perms[5] = {};
	bool found;

	ppps_require_compat();
	found = read_zero_mapping(&end, perms) == 0;

	ksft_print_header();
	ksft_set_plan(1);
	ksft_test_result(found && end == page_size,
			 "MMAP_PAGE_ZERO maps one process page\n");
	ksft_print_msg("process page size %lu, page-zero mapping 0-%lx %s\n",
		       page_size, end, found ? perms : "missing");
	ksft_finished();
}

/*
 * Hand-written main: the compat re-exec also needs MMAP_PAGE_ZERO in the
 * personality, which the standard main does not set.
 */
int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	int persona;

	if (mode && argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	if (argc != 1)
		ksft_exit_fail_msg("unexpected arguments\n");

	persona = personality(0xffffffffUL);
	if (persona < 0)
		ksft_exit_fail_msg("failed to read personality\n");
	if (personality((unsigned int)(persona | MMAP_PAGE_ZERO)) < 0)
		ksft_exit_fail_msg("failed to set personality\n");
	exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
}
