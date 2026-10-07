// SPDX-License-Identifier: GPL-2.0
/*
 * Changing the tail of a VMA so that it merges with the following VMA
 * shrinks the first VMA and moves the start of the second one backwards
 * (vma_merge_existing_range(), "merge right" with __adjust_next_start).  The
 * second VMA's vm_pgoff must then be decreased by the number of pages it
 * gained.  If it is computed from an address below vm_start with unsigned
 * arithmetic it wraps, and the moved VMA no longer maps the file offsets or
 * anon_vma indices it covers: file faults in it raise SIGBUS and it can no
 * longer merge with its linear neighbours.
 *
 * The bug is a regression for native 16K processes on a PPPS kernel, so the
 * native geometry is tested first; the 4K compat geometry must behave the
 * same way.
 */
#define _GNU_SOURCE

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define UNITS	3

struct vma_range {
	unsigned long start, end;
	char perms[5];
};

static sigjmp_buf fault_env;

static void fault_handler(int sig)
{
	siglongjmp(fault_env, sig);
}

/* Read one byte, returning the signal number on a fault and 0 otherwise. */
static int read_byte(const volatile unsigned char *addr, unsigned char *val)
{
	int sig = sigsetjmp(fault_env, 1);

	if (sig)
		return sig;
	*val = *addr;
	return 0;
}

/* VMAs of this process overlapping [start, end), in address order. */
static int collect_vmas(unsigned long start, unsigned long end,
			struct vma_range *out, int max)
{
	char line[512];
	FILE *maps = fopen("/proc/self/maps", "re");
	int nr = 0;

	if (!maps)
		return -1;
	while (fgets(line, sizeof(line), maps)) {
		struct vma_range range;

		if (sscanf(line, "%lx-%lx %4s", &range.start, &range.end,
			   range.perms) != 3)
			continue;
		if (range.end <= start || range.start >= end)
			continue;
		if (nr < max)
			out[nr] = range;
		nr++;
	}
	fclose(maps);
	return nr;
}

static void dump_vmas(unsigned long start, unsigned long end)
{
	struct vma_range vmas[8];
	int nr = collect_vmas(start, end, vmas, 8), i;

	for (i = 0; i < nr && i < 8; i++)
		ksft_print_msg("  vma %lx-%lx %s\n", vmas[i].start,
			       vmas[i].end, vmas[i].perms);
}

/* The range is covered by exactly the listed VMA boundaries. */
static bool vmas_are(unsigned char *base, size_t unit, int nr_expected,
		     const int *boundaries)
{
	unsigned long start = (unsigned long)base;
	unsigned long end = start + UNITS * unit;
	struct vma_range vmas[8];
	int nr = collect_vmas(start, end, vmas, 8), i;

	if (nr != nr_expected)
		return false;
	for (i = 0; i < nr; i++) {
		if (vmas[i].start != start + boundaries[i] * unit ||
		    vmas[i].end != start + boundaries[i + 1] * unit)
			return false;
	}
	return true;
}

/*
 * Map UNITS pages, make the last one read-only, then make the middle one
 * read-only so that it is cut from the first VMA and merged into the last.
 * Returns 0 on pass, 1 on fail.
 */
static int check_merge_next(const char *name, int fd, size_t unit)
{
	static const int split[] = { 0, 1, UNITS };
	static const int whole[] = { 0, UNITS };
	unsigned char *base, value = 0;
	int flags = fd >= 0 ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS;
	unsigned int i;
	int sig, ret = 0;

	base = mmap(NULL, UNITS * unit, PROT_READ | PROT_WRITE, flags, fd, 0);
	if (base == MAP_FAILED) {
		ksft_print_msg("%s: mmap: %s\n", name, strerror(errno));
		return 1;
	}
	for (i = 0; i < UNITS; i++)
		memset(base + i * unit, 0x41 + i, unit);

	if (mprotect(base + 2 * unit, unit, PROT_READ) ||
	    mprotect(base + unit, unit, PROT_READ)) {
		ksft_print_msg("%s: mprotect: %s\n", name, strerror(errno));
		ret = 1;
		goto out;
	}
	if (!vmas_are(base, unit, 2, split)) {
		ksft_print_msg("%s: the read-only middle page did not merge into the next VMA\n",
			       name);
		dump_vmas((unsigned long)base, (unsigned long)base + UNITS * unit);
		ret = 1;
		goto out;
	}

	if (fd >= 0) {
		/* Drop the PTEs so the moved range has to fault from the file. */
		if (madvise(base + unit, 2 * unit, MADV_DONTNEED)) {
			ksft_print_msg("%s: MADV_DONTNEED: %s\n", name,
				       strerror(errno));
			ret = 1;
			goto out;
		}
	}
	for (i = 0; i < UNITS; i++) {
		sig = read_byte(base + i * unit + unit / 2, &value);
		if (sig) {
			ksft_print_msg("%s: reading page %u raised signal %d\n",
				       name, i, sig);
			ret = 1;
		} else if (value != 0x41 + i) {
			ksft_print_msg("%s: page %u reads %#x, expected %#x\n",
				       name, i, value, 0x41 + i);
			ret = 1;
		}
	}

	/* Linear vm_pgoff on both sides lets the three pages merge again. */
	if (mprotect(base, unit, PROT_READ)) {
		ksft_print_msg("%s: mprotect head: %s\n", name, strerror(errno));
		ret = 1;
		goto out;
	}
	if (!vmas_are(base, unit, 1, whole)) {
		ksft_print_msg("%s: linear neighbours no longer merge\n", name);
		dump_vmas((unsigned long)base, (unsigned long)base + UNITS * unit);
		ret = 1;
	}
out:
	munmap(base, UNITS * unit);
	return ret;
}

static int run_cases(void)
{
	size_t unit = getpagesize();
	struct sigaction action = { .sa_handler = fault_handler };
	char name[64];
	int fd, ret = 0;

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGBUS, &action, NULL) || sigaction(SIGSEGV, &action, NULL))
		return 1;

	snprintf(name, sizeof(name), "%zuK anon", unit / 1024);
	ret |= check_merge_next(name, -1, unit);

	fd = syscall(__NR_memfd_create, "vma_merge_next_pgoff", 0);
	if (fd < 0 || ftruncate(fd, UNITS * unit)) {
		ksft_print_msg("memfd: %s\n", strerror(errno));
		return 1;
	}
	snprintf(name, sizeof(name), "%zuK shmem file", unit / 1024);
	ret |= check_merge_next(name, fd, unit);
	close(fd);
	return ret;
}

/* Run the cases in a child re-exec'd with the requested geometry. */
static int run_child(bool compat)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		ppps_execl(compat, NULL, compat ? "--compat" : "--native", NULL);
		_exit(KSFT_SKIP);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return KSFT_FAIL;
	return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	int result;

	if (mode && !strcmp(mode, "--native")) {
		if (getpagesize() != (int)NATIVE_PAGE_SIZE)
			return KSFT_SKIP;
		return run_cases() ? KSFT_FAIL : KSFT_PASS;
	}
	if (mode && !strcmp(mode, "--compat")) {
		if (!ppps_is_compat_process())
			return KSFT_SKIP;
		return run_cases() ? KSFT_FAIL : KSFT_PASS;
	}

	ksft_print_header();
	ksft_set_plan(2);
	fflush(stdout);
	result = run_child(false);
	if (result == KSFT_SKIP)
		ksft_test_result_skip("native 16K: merge-right keeps vm_pgoff linear\n");
	else
		ksft_test_result(result == KSFT_PASS,
				 "native 16K: merge-right keeps vm_pgoff linear\n");
	fflush(stdout);
	result = run_child(true);
	if (result == KSFT_SKIP)
		ksft_test_result_skip("4K compat: merge-right keeps vm_pgoff linear\n");
	else
		ksft_test_result(result == KSFT_PASS,
				 "4K compat: merge-right keeps vm_pgoff linear\n");
	ksft_finished();
}
