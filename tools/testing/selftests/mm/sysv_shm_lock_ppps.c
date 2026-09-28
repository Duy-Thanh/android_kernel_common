// SPDX-License-Identifier: GPL-2.0
/*
 * SHM_LOCK of a one-process-page SysV segment fits a 4K RLIMIT_MEMLOCK for
 * an unprivileged 4K compat process.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/shm.h>

#include "kselftest_ppps.h"

#define UNPRIVILEGED_ID 65534

static int run_test(void)
{
	struct rlimit limit = {
		.rlim_cur = PROCESS_PAGE_SIZE,
		.rlim_max = PROCESS_PAGE_SIZE,
	};
	unsigned char *mapping = (void *)-1;
	bool lock_ok = false;
	int shmid = -1;
	int error;

	ksft_print_header();
	if (access("/proc/sysvipc/shm", F_OK))
		ksft_exit_skip("SysV shared memory is unavailable\n");
	ksft_set_plan(6);

	error = setrlimit(RLIMIT_MEMLOCK, &limit);
	ksft_test_result(!error, "set a one-process-page memlock limit\n");
	if (error)
		goto out_skip;

	error = setgid(UNPRIVILEGED_ID) || setuid(UNPRIVILEGED_ID);
	ksft_test_result(!error && getuid() == UNPRIVILEGED_ID,
			 "drop CAP_IPC_LOCK\n");
	if (error)
		goto out_skip;

	shmid = shmget(IPC_PRIVATE, PROCESS_PAGE_SIZE, IPC_CREAT | 0600);
	ksft_test_result(shmid >= 0, "create a one-process-page SysV segment\n");
	if (shmid < 0)
		goto out_skip;

	mapping = shmat(shmid, NULL, 0);
	if (mapping != (void *)-1) {
		mapping[0] = 0xa5;
		mapping[PROCESS_PAGE_SIZE - 1] = 0x5a;
	}
	ksft_test_result(mapping != (void *)-1 && mapping[0] == 0xa5 &&
			 mapping[PROCESS_PAGE_SIZE - 1] == 0x5a,
			 "map the complete 4K segment\n");

	errno = 0;
	lock_ok = shmctl(shmid, SHM_LOCK, NULL) == 0;
	if (!lock_ok)
		ksft_print_msg("SHM_LOCK failed: %s\n", strerror(errno));
	ksft_test_result(lock_ok,
			 "lock a 4K segment against a 4K byte limit\n");

	error = lock_ok ? shmctl(shmid, SHM_UNLOCK, NULL) : -1;
	ksft_test_result(!error, "unlock the 4K segment\n");
	goto out;

out_skip:
	while (ksft_test_num() < 6)
		ksft_test_result_skip("prerequisite failed\n");
out:
	if (mapping != (void *)-1)
		shmdt(mapping);
	if (shmid >= 0)
		shmctl(shmid, IPC_RMID, NULL);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
