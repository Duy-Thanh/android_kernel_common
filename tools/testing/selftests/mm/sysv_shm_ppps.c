// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process can attach, round, mremap and detach a one-page SysV
 * shm segment at 4K-aligned addresses inside a native 16K page.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/shm.h>

#include "kselftest_ppps.h"

#define RESERVE_SIZE	(4 * NATIVE_PAGE_SIZE)
#define TEST_VALUE	0x5a17c0deU

struct target_area {
	void *base;
	void *target;
};

static bool reserve_subpage_target(struct target_area *area)
{
	uintptr_t start;
	uintptr_t target;

	area->base = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (area->base == MAP_FAILED)
		return false;
	start = (uintptr_t)area->base;
	target = (start + NATIVE_PAGE_SIZE - 1) & -NATIVE_PAGE_SIZE;
	target += PROCESS_PAGE_SIZE;
	if (target + PROCESS_PAGE_SIZE > start + RESERVE_SIZE) {
		munmap(area->base, RESERVE_SIZE);
		area->base = MAP_FAILED;
		return false;
	}
	area->target = (void *)target;
	return true;
}

static void release_target(struct target_area *area)
{
	if (area->base != MAP_FAILED)
		munmap(area->base, RESERVE_SIZE);
}

static int create_segment(void)
{
	return shmget(IPC_PRIVATE, PROCESS_PAGE_SIZE, IPC_CREAT | 0600);
}

static bool fixed_attach_roundtrip(bool *detach_ok)
{
	struct target_area area = { .base = MAP_FAILED };
	unsigned int *mapping = (void *)-1;
	bool success = false;
	int shmid;

	*detach_ok = false;
	shmid = create_segment();
	if (shmid < 0 || !reserve_subpage_target(&area))
		goto out;
	mapping = shmat(shmid, area.target, SHM_REMAP);
	if (mapping == (void *)-1)
		goto out;
	*mapping = TEST_VALUE;
	success = *mapping == TEST_VALUE;
	*detach_ok = shmdt(mapping) == 0;
	mapping = (void *)-1;
out:
	if (mapping != (void *)-1)
		shmdt(mapping);
	if (shmid >= 0)
		shmctl(shmid, IPC_RMID, NULL);
	release_target(&area);
	return success;
}

static bool rounded_attach_roundtrip(void)
{
	struct target_area area = { .base = MAP_FAILED };
	unsigned int *mapping = (void *)-1;
	bool success = false;
	int shmid;

	shmid = create_segment();
	if (shmid < 0 || !reserve_subpage_target(&area))
		goto out;
	mapping = shmat(shmid, area.target, SHM_RND | SHM_REMAP);
	if (mapping == (void *)-1)
		goto out;
	*mapping = TEST_VALUE;
	success = mapping == area.target && *mapping == TEST_VALUE;
out:
	if (mapping != (void *)-1 && shmdt(mapping))
		success = false;
	if (shmid >= 0)
		shmctl(shmid, IPC_RMID, NULL);
	release_target(&area);
	return success;
}

static bool moved_attach_roundtrip(bool *detach_ok)
{
	struct target_area area = { .base = MAP_FAILED };
	unsigned int *mapping = (void *)-1;
	void *moved = MAP_FAILED;
	bool success = false;
	int shmid;

	*detach_ok = false;
	shmid = create_segment();
	if (shmid < 0)
		goto out;
	mapping = shmat(shmid, NULL, 0);
	if (mapping == (void *)-1 || !reserve_subpage_target(&area))
		goto out;
	*mapping = TEST_VALUE;
	moved = mremap(mapping, PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED, area.target);
	if (moved == MAP_FAILED)
		goto out;
	mapping = (void *)-1;
	success = *(unsigned int *)moved == TEST_VALUE;
	*detach_ok = shmdt(moved) == 0;
	moved = MAP_FAILED;
out:
	if (moved != MAP_FAILED)
		munmap(moved, PROCESS_PAGE_SIZE);
	if (mapping != (void *)-1)
		shmdt(mapping);
	if (shmid >= 0)
		shmctl(shmid, IPC_RMID, NULL);
	release_target(&area);
	return success;
}

static int run_test(void)
{
	bool fixed_detach;
	bool moved_detach;
	bool fixed;
	bool moved;
	bool rounded;
	int probe;

	ksft_print_header();
	if (access("/proc/sysvipc/shm", F_OK))
		ksft_exit_skip("CONFIG_SYSVIPC is disabled\n");
	probe = create_segment();
	if (probe < 0)
		ksft_exit_fail_msg("SysV shm probe failed: %s\n",
				   strerror(errno));
	shmctl(probe, IPC_RMID, NULL);
	ksft_set_plan(5);
	fixed = fixed_attach_roundtrip(&fixed_detach);
	ksft_test_result(fixed,
			 "attach SysV shm at a 4K-aligned subpage address\n");
	ksft_test_result(fixed && fixed_detach,
			 "detach the fixed subpage SysV shm mapping\n");
	rounded = rounded_attach_roundtrip();
	ksft_test_result(rounded,
			 "round SysV shm addresses at process-page granularity\n");
	moved = moved_attach_roundtrip(&moved_detach);
	ksft_test_result(moved,
			 "mremap SysV shm to a 4K-aligned subpage address\n");
	ksft_test_result(moved && moved_detach,
			 "detach the moved subpage SysV shm mapping\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
