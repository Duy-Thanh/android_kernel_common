// SPDX-License-Identifier: GPL-2.0
/*
 * Anonymous mlock lifecycle in native and compat processes.  A compat fault
 * may populate a whole native tuple, so use several native pages and inspect
 * VM_LOCKED/VM_LOCKONFAULT separately from RSS.  This is not a large file
 * folio test and does not require pressure, root, PFNs or fixture modules.
 */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif

#define LENGTH (4 * NATIVE_PAGE_SIZE)

struct snapshot {
	unsigned long size;
	unsigned long rss;
	bool locked;
	bool onfault;
};

static bool snapshot(void *address, struct snapshot *s)
{
	unsigned long start, end;
	bool found = false, have_size = false, have_rss = false;
	char line[1024];
	FILE *file;

	memset(s, 0, sizeof(*s));
	file = fopen("/proc/self/smaps", "r");
	if (!file)
		return false;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			if (found)
				break;
			found = start <= (uintptr_t)address && (uintptr_t)address < end;
			continue;
		}
		if (!found)
			continue;
		if (sscanf(line, "Size: %lu kB", &s->size) == 1)
			have_size = true;
		if (sscanf(line, "Rss: %lu kB", &s->rss) == 1)
			have_rss = true;
		if (!strncmp(line, "VmFlags:", 8)) {
			char *flag = strtok(line + 8, "\t\n ");

			while (flag) {
				s->locked |= !strcmp(flag, "lo");
				s->onfault |= !strcmp(flag, "lf");
				flag = strtok(NULL, "\t\n ");
			}
			fclose(file);
			return have_size && have_rss;
		}
	}
	fclose(file);
	return false;
}

static unsigned long locked_kb(void)
{
	unsigned long value;
	char line[256];
	FILE *file = fopen("/proc/self/status", "r");

	if (!file)
		ksft_exit_fail_msg("open status: %s\n", strerror(errno));
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "VmLck: %lu kB", &value) == 1) {
			fclose(file);
			return value;
		}
	}
	fclose(file);
	ksft_exit_fail_msg("VmLck missing from status\n");
}

static bool resident(void *map, size_t page_size, bool all)
{
	unsigned char vec[LENGTH / PROCESS_PAGE_SIZE];
	size_t nr = LENGTH / page_size;
	size_t i;

	if (mincore(map, LENGTH, vec))
		return false;
	for (i = 0; i < nr; i++) {
		if (!!(vec[i] & 1) != all)
			return false;
	}
	return true;
}

static bool fork_check(unsigned char *map)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		return false;
	if (!pid) {
		struct snapshot s;
		bool pass = snapshot(map, &s) && !s.locked && !s.onfault &&
			    !locked_kb() && map[0] == 0x39 && map[LENGTH - 1] == 0x71;

		map[0] = 0x44;
		_exit(pass ? 0 : 1);
	}
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return false;
	}
	return WIFEXITED(status) && !WEXITSTATUS(status) && map[0] == 0x39;
}

static int run_test(void)
{
	struct snapshot initial, s;
	struct rlimit limit;
	size_t page_size = getpagesize();
	size_t reservation_size = LENGTH + 2 * NATIVE_PAGE_SIZE;
	unsigned char *reservation, *map;
	unsigned long before, faulted_rss;
	unsigned char far;
	bool pass, initial_absent;
	int ret, saved_errno;

	ksft_print_header();
	if (getrlimit(RLIMIT_MEMLOCK, &limit))
		ksft_exit_fail_msg("getrlimit: %s\n", strerror(errno));
	before = locked_kb();
	if (limit.rlim_cur != RLIM_INFINITY &&
	    limit.rlim_cur < LENGTH + before * 1024)
		ksft_exit_skip("needs 64K of available RLIMIT_MEMLOCK\n");

	/* Guard VMAs keep smaps observations confined to this mapping. */
	reservation = mmap(NULL, reservation_size, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	map = (unsigned char *)(((uintptr_t)reservation + NATIVE_PAGE_SIZE) &
			       ~(NATIVE_PAGE_SIZE - 1));
	if (mprotect(map, LENGTH, PROT_READ | PROT_WRITE))
		ksft_exit_fail_msg("mprotect: %s\n", strerror(errno));
	if (madvise(map, LENGTH, MADV_NOHUGEPAGE) && errno != EINVAL)
		ksft_exit_fail_msg("MADV_NOHUGEPAGE: %s\n", strerror(errno));
	if (!snapshot(map, &initial))
		ksft_exit_fail_msg("missing smaps snapshot\n");
	initial_absent = resident(map, page_size, false);
	ret = syscall(SYS_mlock2, map, LENGTH, MLOCK_ONFAULT);
	saved_errno = errno;
	if (ret && (saved_errno == EPERM || saved_errno == ENOMEM)) {
		munmap(reservation, reservation_size);
		ksft_exit_skip("mlock2 unavailable or lock limit reached: %s\n",
			       strerror(saved_errno));
	}
	ksft_set_plan(20);
	ksft_print_msg("process page size: %zu bytes; mapping: %lu bytes\n",
		       page_size, LENGTH);
	ksft_test_result(initial.size == LENGTH / 1024 && !initial.rss &&
			 !initial.locked && !initial.onfault && initial_absent,
			 "fresh guarded mapping has no residency or lock flags\n");
	ksft_test_result(!ret, "mlock2(MLOCK_ONFAULT) succeeds\n");
	ksft_test_result(snapshot(map, &s) && s.locked && s.onfault && !s.rss &&
			 resident(map, page_size, false),
			 "ONFAULT sets lo/lf without populating pages\n");
	ksft_test_result(locked_kb() == before + LENGTH / 1024,
			 "ONFAULT charges virtual bytes, not resident bytes\n");
	map[0] = 0x39;
	pass = snapshot(map, &s) && s.rss >= page_size / 1024 &&
	       s.rss <= NATIVE_PAGE_SIZE / 1024 && s.rss < s.size;
	ksft_test_result(pass, "first fault populates at most one native tuple\n");
	ksft_test_result(!mincore(map + LENGTH - page_size, page_size, &far) &&
			 !(far & 1), "distant process page is still absent\n");
	map[LENGTH - 1] = 0x71;
	pass = snapshot(map, &s) && s.rss > 0 && s.rss <= 2 * NATIVE_PAGE_SIZE / 1024 &&
	       s.rss < s.size && map[0] == 0x39;
	faulted_rss = s.rss;
	ksft_test_result(pass, "second distant fault remains demand-populated\n");
	ksft_test_result(fork_check(map),
			 "fork preserves data/COW but does not inherit memory locks\n");
	ksft_test_result(snapshot(map, &s) && s.locked && s.onfault &&
			 map[LENGTH - 1] == 0x71,
			 "child exit leaves parent's lock flags and data intact\n");
	ksft_test_result(!munlock(map, LENGTH), "munlock of ONFAULT mapping succeeds\n");
	ksft_test_result(snapshot(map, &s) && !s.locked && !s.onfault &&
			 s.rss == faulted_rss,
			 "munlock clears both flags without discarding residency\n");
	ksft_test_result(locked_kb() == before, "munlock restores VmLck baseline\n");
	ksft_test_result(!mlock(map, LENGTH), "ordinary mlock succeeds\n");
	ksft_test_result(snapshot(map, &s) && s.locked && !s.onfault &&
			 s.rss == LENGTH / 1024 && resident(map, page_size, true),
			 "ordinary mlock populates all pages and sets only lo\n");
	ksft_test_result(locked_kb() == before + LENGTH / 1024,
			 "ordinary mlock charges the same virtual byte count\n");
	ksft_test_result(map[0] == 0x39 && map[LENGTH - 1] == 0x71 &&
			 map[LENGTH / 2] == 0, "eager lock preserves initialized and zero data\n");
	ksft_test_result(!munlock(map, LENGTH), "ordinary munlock succeeds\n");
	ksft_test_result(snapshot(map, &s) && !s.locked && !s.onfault &&
			 s.rss == LENGTH / 1024,
			 "ordinary munlock clears flags and keeps pages resident\n");
	ksft_test_result(locked_kb() == before, "final VmLck equals baseline\n");
	ksft_test_result(!munmap(reservation, reservation_size),
			 "unmap including guards succeeds\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	return ppps_geometry_main(argc, argv, run_test);
}
