// SPDX-License-Identifier: GPL-2.0-only
/* A private COW/unmap cycle must not accumulate references to its file page. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "kselftest_ppps.h"
#include "iov_iter_ppps.h"
#include "page_range_ppps.h"

static int file_refs(int fixture, int file)
{
	struct file_refs_ppps_args request = { .fd = file };

	if (ioctl(fixture, FILE_REFS_PPPS_IOCTL, &request))
		ksft_exit_fail_msg("file reference observation failed: %s\n", strerror(errno));
	return request.refs;
}

static int run_test(void)
{
	unsigned char initial[NATIVE_PAGE_SIZE];
	size_t page = sysconf(_SC_PAGESIZE);
	int fixture, file, before, after, round;
	bool contents = true;
	size_t i;

	ksft_print_header();
	ksft_set_plan(2);
	fixture = ppps_open_fixture_or_skip("/dev/" IOV_ITER_PPPS_DEVICE_NAME, O_RDWR);
	file = memfd_create("file-cow-references", MFD_CLOEXEC);
	memset(initial, 0x37, sizeof(initial));
	if (file < 0 || write(file, initial, sizeof(initial)) != sizeof(initial))
		ksft_exit_fail_msg("initialized backing setup failed\n");
	before = file_refs(fixture, file);
	for (round = 0; round < 8; round++) {
		volatile unsigned char *map = mmap(NULL, sizeof(initial),
			PROT_READ | PROT_WRITE, MAP_PRIVATE, file, 0);

		if ((void *)map == MAP_FAILED)
			ksft_exit_fail_msg("private file mapping failed\n");
		/* Populate file PTEs before any writes, then COW each slice. */
		for (i = 0; i < sizeof(initial); i += page)
			contents &= map[i] == 0x37;
		for (i = 0; i < sizeof(initial); i += page)
			map[i] = 0x80 + round;
		for (i = 0; i < sizeof(initial); i += page)
			contents &= map[i] == 0x80 + round;
		if (munmap((void *)map, sizeof(initial)))
			ksft_exit_fail_msg("unmap failed\n");
	}
	after = file_refs(fixture, file);
	ksft_test_result(contents, "repeated file COW preserves private bytes\n");
	/*
	 * One transient LRU-vector reference may differ between observations.
	 * An owned folio with no remaining mappings must not grow a reference
	 * per completed COW operation.
	 */
	ksft_test_result(after <= before + 1,
		"file references do not accumulate (before=%d after=%d)\n",
		before, after);
	close(file);
	close(fixture);
	ksft_finished();
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--native")) {
		if (sysconf(_SC_PAGESIZE) != NATIVE_PAGE_SIZE)
			exec_native(argv[0], "--native", NULL);
		return run_test();
	}
	return ppps_compat_main(argc, argv, run_test);
}
