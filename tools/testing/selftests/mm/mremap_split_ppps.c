// SPDX-License-Identifier: GPL-2.0-only
/* Shrinking and no-op mremap retain the 6.12 cross-VMA semantics. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include "kselftest_ppps.h"

static void check_split(bool shrink)
{
	size_t page = sysconf(_SC_PAGESIZE);
	size_t old_len = 4 * page, new_len = shrink ? page : old_len;
	unsigned char *base, *result;
	unsigned char vec;
	bool ok;

	base = mmap(NULL, old_len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		ksft_exit_fail_msg("mapping failed\n");
	memset(base, 0x5a, old_len);
	if (mprotect(base + page, page, PROT_READ))
		ksft_exit_fail_msg("splitting VMA failed\n");
	result = mremap(base, old_len, new_len, 0);
	ok = result == base;
	ksft_test_result(ok, "%s across mprotect-created VMA boundaries\n",
			 shrink ? "shrink" : "no-op");
	if (ok && shrink)
		ok = mincore(base + page, page, &vec) == -1 && errno == ENOMEM;
	else if (ok)
		ok = base[3 * page] == 0x5a;
	ok &= base[0] == 0x5a && base[page - 1] == 0x5a;
	ksft_test_result(ok, "%s preserves retained bytes and expected tail\n",
			 shrink ? "shrink" : "no-op");
	munmap(base, result == MAP_FAILED ? old_len : new_len);
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(4);
	check_split(false);
	check_split(true);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--current"))
		return run_test();
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (sysconf(_SC_PAGESIZE) != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
