// SPDX-License-Identifier: GPL-2.0
/* Verify that 4K PPPS processes cannot create videobuf2 USERPTR queues. */
#define _GNU_SOURCE

#include <sys/ioctl.h>

#include "kselftest_ppps.h"

#define VB2_USERPTR_REJECT_PPPS_RUN _IO('v', 0x71)

static int run_test(bool compat)
{
	int expected_errno = compat ? EOPNOTSUPP : 0;
	int saved_errno;
	int ret;
	int fd;

	if (compat)
		ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(2);
	fd = ppps_open_fixture_or_skip("/dev/vb2_userptr_reject_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open the videobuf2 USERPTR fixture\n");

	errno = 0;
	ret = ioctl(fd, VB2_USERPTR_REJECT_PPPS_RUN);
	saved_errno = errno;
	ksft_test_result((!compat && ret == 0) ||
			 (compat && ret == -1 && saved_errno == expected_errno),
			 "%s process %s USERPTR queue creation (errno=%d)\n",
			 compat ? "4K PPPS" : "native",
			 compat ? "rejects" : "supports", saved_errno);
	close(fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], "--run", NULL);
	if (argc == 2 && !strcmp(mode, "--run"))
		return run_test(true);
	if (argc == 2 && !strcmp(mode, "--native"))
		exec_native(argv[0], "--native-run", NULL);
	if (argc == 2 && !strcmp(mode, "--native-run"))
		return run_test(false);
	return EXIT_FAILURE;
}
