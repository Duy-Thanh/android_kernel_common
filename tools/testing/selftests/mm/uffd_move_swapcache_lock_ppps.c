// SPDX-License-Identifier: GPL-2.0
/*
 * UFFDIO_MOVE of a whole swapped-out compat window takes the swap cache
 * folio's lock.  It must not sleep for that lock while it still has the
 * page tables mapped: pte_offset_map() holds an RCU read-side critical
 * section, and with PREEMPT_RCU a voluntary context switch inside one is a
 * bug that the scheduler reports ("Voluntary context switch within RCU
 * read-side critical section!") and that stalls RCU grace periods.
 *
 * The test makes the lock contended on purpose: the windows are swapped to
 * a private disk-backed swap file, their swap cache folios are reclaimed,
 * and MADV_WILLNEED starts asynchronous swap-in reads, which keep each new
 * swap cache folio locked until its read completes.  UFFDIO_MOVE is issued
 * right away.  Control windows wait for the reads to complete first, so the
 * slower race moves show that the lock was actually contended.
 *
 * Swap slots come from a per-CPU cluster cache before swap priorities are
 * consulted, so a CPU that was swapping to the system's zram keeps doing so
 * until its cached cluster is used up.  The child therefore pins itself to
 * one CPU and pages out scratch windows until one lands on the test swap
 * file before it swaps out the windows under test.
 *
 * Needs root (swap file, memory cgroup).  Fails on a new kernel warning or
 * a data mismatch; skips when the warning already fired in this boot, when
 * the setup cannot be built or when no move found its folio still locked.
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/wait.h>
#include <time.h>

#include "ppps_tuple_test.h"

#define NR_CONTROL		32
#define NR_RACE			224
#define NR_WINDOWS		(NR_CONTROL + NR_RACE)
#define SWAPFILE_SIZE		(64UL << 20)
#define SWAP_PRIORITY		32000
#define RECLAIM_ROUNDS		50
#define STEER_WINDOWS		32
#define STEER_ROUNDS		32

#define SWAP_VERSION_OFFSET	1024
#define SWAP_LAST_PAGE_OFFSET	1028
#define SWAP_MAGIC		"SWAPSPACE2"
#define SWAP_MAGIC_SIZE		10

#define SYSLOG_ACTION_READ_ALL		3
#define SYSLOG_ACTION_SIZE_BUFFER	10

#define RCU_WARNING	"Voluntary context switch within RCU read-side critical section"

#define CHILD_PASS	0
#define CHILD_FAIL	1
#define CHILD_SKIP	4

static bool write_file(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t len = strlen(value), ret;

	if (fd < 0)
		return false;
	ret = write(fd, value, len);
	close(fd);
	return ret == len;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* ---------------------------------------------------------------- child */

static unsigned char slice_byte(unsigned int window, unsigned int slice)
{
	return (unsigned char)(0x10 + (window * PPPS_SLICES + slice) % 0xe0);
}

static unsigned int resident_slices(unsigned char *base, size_t len)
{
	unsigned char vec[NR_WINDOWS * PPPS_SLICES];
	unsigned int i, n = 0;
	size_t pages = len / PROCESS_PAGE_SIZE;

	if (pages > sizeof(vec) || mincore(base, len, vec))
		return UINT_MAX;
	for (i = 0; i < pages; i++)
		n += vec[i] & 1;
	return n;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return (x > y) - (x < y);
}

static long uffd_move(int uffd, void *dst, void *src, unsigned long len,
		      int *error)
{
	struct uffdio_move move = {
		.dst = (uintptr_t)dst,
		.src = (uintptr_t)src,
		.len = len,
	};

	*error = 0;
	if (ioctl(uffd, UFFDIO_MOVE, &move))
		*error = errno;
	return move.move;
}

/*
 * Pin this task to its current CPU (the old mask is saved in @saved) and page
 * out scratch windows from @scratch, STEER_WINDOWS at a time, until one is
 * swapped to @swap_type: once its cached cluster on another swap area is used
 * up, the CPU picks a new one from the highest priority area, the test swap
 * file.  The scratch windows stay mapped so that their slots are not reused.
 */
static bool steer_swap_to(unsigned int swap_type, unsigned char *scratch,
			  cpu_set_t *saved)
{
	const size_t len = STEER_WINDOWS * NATIVE_PAGE_SIZE;
	int cpu = sched_getcpu();
	unsigned int round;
	cpu_set_t one;
	uint64_t e;

	if (cpu < 0 || sched_getaffinity(0, sizeof(*saved), saved))
		return false;
	CPU_ZERO(&one);
	CPU_SET(cpu, &one);
	if (sched_setaffinity(0, sizeof(one), &one))
		return false;
	for (round = 0; round < STEER_ROUNDS; round++, scratch += len) {
		memset(scratch, 0x5a, len);
		if (!pageout_reaches_swap(scratch, len, len))
			continue;
		if (ppps_pagemap_entry(scratch + len - PROCESS_PAGE_SIZE, &e) &&
		    !(e & PAGEMAP_PRESENT) && (e & PAGEMAP_SWAPPED) &&
		    (e & 0x1f) == swap_type) {
			ksft_print_msg("child: CPU %d swaps to type %u after %u scratch round(s)\n",
				       cpu, swap_type, round + 1);
			return true;
		}
	}
	return false;
}

static int child_main(const char *cgroup, unsigned int swap_type)
{
	const size_t bytes = NR_WINDOWS * NATIVE_PAGE_SIZE;
	unsigned char *src_res, *dst_res, *src, *dst, *scratch_res, *scratch;
	uint64_t control[NR_CONTROL], race[NR_RACE], threshold;
	unsigned int w, s, resident, slow = 0, bad = 0, moved = 0, round;
	unsigned int errors = 0, wrong_type = 0, not_swapped = 0;
	char reclaim[PATH_MAX];
	cpu_set_t saved_cpus;
	int uffd, error;

	ppps_require_compat();
	src = map_aligned(bytes, &src_res);
	dst = map_aligned(bytes, &dst_res);
	scratch = map_aligned(STEER_ROUNDS * STEER_WINDOWS * NATIVE_PAGE_SIZE,
			      &scratch_res);
	if (src == MAP_FAILED || dst == MAP_FAILED || scratch == MAP_FAILED) {
		ksft_print_msg("child: mmap: %s\n", strerror(errno));
		return CHILD_FAIL;
	}
	uffd = uffd_open_register(dst, bytes);
	if (uffd < 0) {
		ksft_print_msg("child: userfaultfd: %s\n", strerror(errno));
		return CHILD_SKIP;
	}
	for (w = 0; w < NR_WINDOWS; w++)
		for (s = 0; s < PPPS_SLICES; s++)
			memset(src + w * NATIVE_PAGE_SIZE + s * PROCESS_PAGE_SIZE,
			       slice_byte(w, s), PROCESS_PAGE_SIZE);
	if (!steer_swap_to(swap_type, scratch, &saved_cpus)) {
		ksft_print_msg("child: scratch windows never reached swap type %u\n",
			       swap_type);
		return CHILD_SKIP;
	}
	if (!pageout_reaches_swap(src, bytes, bytes)) {
		ksft_print_msg("child: MADV_PAGEOUT did not swap out the windows\n");
		return CHILD_SKIP;
	}
	sched_setaffinity(0, sizeof(saved_cpus), &saved_cpus);
	munmap(scratch_res, STEER_ROUNDS * STEER_WINDOWS * NATIVE_PAGE_SIZE +
			    NATIVE_PAGE_SIZE);
	for (w = 0; w < NR_WINDOWS * PPPS_SLICES; w++) {
		uint64_t e;

		if (!ppps_pagemap_entry(src + w * PROCESS_PAGE_SIZE, &e) ||
		    (e & PAGEMAP_PRESENT) || !(e & PAGEMAP_SWAPPED)) {
			not_swapped++;
			continue;
		}
		if ((e & 0x1f) != swap_type) {
			if (!wrong_type)
				ksft_print_msg("child: slice %u is on swap type %u\n",
					       w, (unsigned int)(e & 0x1f));
			wrong_type++;
		}
	}
	if (not_swapped || wrong_type) {
		ksft_print_msg("child: %u slices not swapped, %u not on swap type %u\n",
			       not_swapped, wrong_type, swap_type);
		return CHILD_SKIP;
	}

	/*
	 * Drop the swap cache folios so that swap-in has to read the file.
	 * A single empty mincore() reading can miss a swap cache folio for a
	 * moment, so only trust a second one.
	 */
	snprintf(reclaim, sizeof(reclaim), "%s/memory.reclaim", cgroup);
	for (round = 0; round < RECLAIM_ROUNDS; round++) {
		resident = resident_slices(src, bytes);
		if (!resident) {
			usleep(20000);
			resident = resident_slices(src, bytes);
			if (!resident)
				break;
		}
		write_file(reclaim, "64M");
		usleep(20000);
	}
	resident = resident_slices(src, bytes);
	ksft_print_msg("child: %u slices still in the swap cache after %u reclaim rounds\n",
		       resident, round);
	if (resident) {
		ksft_print_msg("child: cannot empty the swap cache\n");
		return CHILD_SKIP;
	}

	for (w = 0; w < NR_WINDOWS; w++) {
		unsigned char *from = src + w * NATIVE_PAGE_SIZE;
		unsigned char *to = dst + w * NATIVE_PAGE_SIZE;
		uint64_t start, deadline;
		long done;

		if (madvise(from, NATIVE_PAGE_SIZE, MADV_WILLNEED)) {
			ksft_print_msg("child: MADV_WILLNEED: %s\n", strerror(errno));
			return CHILD_FAIL;
		}
		if (w < NR_CONTROL) {
			/* Control: let the read complete first. */
			deadline = now_ns() + 1000000000ULL;
			while (resident_slices(from, NATIVE_PAGE_SIZE) != PPPS_SLICES &&
			       now_ns() < deadline)
				usleep(100);
		}
		start = now_ns();
		done = uffd_move(uffd, to, from, NATIVE_PAGE_SIZE, &error);
		if (w < NR_CONTROL)
			control[w] = now_ns() - start;
		else
			race[w - NR_CONTROL] = now_ns() - start;
		if (done == NATIVE_PAGE_SIZE)
			moved++;
		else if (error)
			errors++;
		for (s = 0; s < PPPS_SLICES; s++) {
			unsigned char *p = (done == NATIVE_PAGE_SIZE ? to : from) +
					   s * PROCESS_PAGE_SIZE;

			if (!all_bytes_are(p, PROCESS_PAGE_SIZE, slice_byte(w, s))) {
				if (!bad)
					ksft_print_msg("child: window %u slice %u reads 0x%02x, expected 0x%02x (moved %ld, errno %d)\n",
						       w, s, p[0], slice_byte(w, s),
						       done, error);
				bad++;
			}
		}
	}
	qsort(control, NR_CONTROL, sizeof(control[0]), cmp_u64);
	threshold = 4 * control[NR_CONTROL / 2];
	if (threshold < 20000)
		threshold = 20000;
	for (w = 0; w < NR_RACE; w++)
		slow += race[w] > threshold;
	qsort(race, NR_RACE, sizeof(race[0]), cmp_u64);
	ksft_print_msg("child: %u of %u windows moved, %u failed; UFFDIO_MOVE median %llu ns (control) vs %llu ns (racing reads), max %llu ns\n",
		       moved, NR_WINDOWS, errors,
		       (unsigned long long)control[NR_CONTROL / 2],
		       (unsigned long long)race[NR_RACE / 2],
		       (unsigned long long)race[NR_RACE - 1]);
	ksft_print_msg("child: %u racing moves took longer than %llu ns\n", slow,
		       (unsigned long long)threshold);
	if (bad) {
		ksft_print_msg("child: %u slices hold wrong data\n", bad);
		return CHILD_FAIL;
	}
	if (!moved) {
		ksft_print_msg("child: no window was moved\n");
		return CHILD_FAIL;
	}
	return slow ? CHILD_PASS : CHILD_SKIP;
}

/* --------------------------------------------------------------- parent */

static char swap_path[PATH_MAX];
static char cgroup_path[PATH_MAX];
static bool swap_active;

static void cleanup(void)
{
	if (swap_active && !swapoff(swap_path))
		swap_active = false;
	if (swap_active)
		ksft_print_msg("cannot swapoff %s: %s\n", swap_path, strerror(errno));
	if (swap_path[0] && !swap_active) {
		unlink(swap_path);
		swap_path[0] = '\0';
	}
	if (cgroup_path[0] && !rmdir(cgroup_path))
		cgroup_path[0] = '\0';
}

static int kernel_warnings(int *rcu)
{
	int size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0), len, count = 0;
	char *log, *line, *save = NULL;

	*rcu = 0;
	if (size <= 0)
		return -1;
	log = malloc(size + 1);
	if (!log)
		return -1;
	len = klogctl(SYSLOG_ACTION_READ_ALL, log, size);
	if (len < 0) {
		free(log);
		return -1;
	}
	log[len] = '\0';
	for (line = strtok_r(log, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		if (strstr(line, RCU_WARNING))
			(*rcu)++;
		if (strstr(line, "WARNING:") || strstr(line, "BUG:") ||
		    strstr(line, "Internal error") || strstr(line, RCU_WARNING))
			count++;
	}
	free(log);
	return count;
}

static void setup_swapfile(const char *dir)
{
	unsigned char *header = calloc(1, NATIVE_PAGE_SIZE);
	int fd;

	if (!header)
		ksft_exit_fail_msg("calloc\n");
	snprintf(swap_path, sizeof(swap_path), "%s/ppps_uffd_lock_%d.swap", dir,
		 (int)getpid());
	fd = open(swap_path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0) {
		swap_path[0] = '\0';
		ksft_exit_skip("cannot create a swap file in %s: %s\n", dir,
			       strerror(errno));
	}
	if (fallocate(fd, 0, 0, SWAPFILE_SIZE))
		ksft_exit_skip("fallocate: %s\n", strerror(errno));
	*(uint32_t *)(header + SWAP_VERSION_OFFSET) = 1;
	*(uint32_t *)(header + SWAP_LAST_PAGE_OFFSET) =
		SWAPFILE_SIZE / NATIVE_PAGE_SIZE - 1;
	memcpy(header + NATIVE_PAGE_SIZE - SWAP_MAGIC_SIZE, SWAP_MAGIC,
	       SWAP_MAGIC_SIZE);
	if (pwrite(fd, header, NATIVE_PAGE_SIZE, 0) != NATIVE_PAGE_SIZE ||
	    fsync(fd))
		ksft_exit_fail_msg("writing %s: %s\n", swap_path, strerror(errno));
	close(fd);
	free(header);
	if (swapon(swap_path, SWAP_FLAG_PREFER |
			      (SWAP_PRIORITY << SWAP_FLAG_PRIO_SHIFT)))
		ksft_exit_skip("swapon %s: %s\n", swap_path, strerror(errno));
	swap_active = true;
}

/* 0-based position of @path among the swap areas in /proc/swaps, or -1. */
static int swap_index(const char *path)
{
	char line[512], name[PATH_MAX];
	FILE *f = fopen("/proc/swaps", "re");
	int index = -1, i = 0;

	if (!f)
		return -1;
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%4095s", name) == 1 && !strcmp(name, path))
			index = i;
		i++;
	}
	fclose(f);
	return index;
}

int main(int argc, char **argv)
{
	const char *dir = getenv("PPPS_SWAP_DIR");
	int rcu_before, rcu_after, warn_before, warn_after, status, index;
	char path[PATH_MAX + 32], pid_text[32], type_text[16];
	char swap_dir[PATH_MAX];
	pid_t pid;

	ppps_argv0 = argv[0];
	if (argc == 4 && !strcmp(argv[1], "--child"))
		return child_main(argv[2], atoi(argv[3]));

	ksft_print_header();
	if (argc != 1)
		ksft_exit_fail_msg("usage: %s\n", argv[0]);
	if (getpagesize() != NATIVE_PAGE_SIZE)
		ksft_exit_skip("the parent must be a native 16K process\n");
	if (geteuid())
		ksft_exit_skip("root is required for swapon(2) and memory.reclaim\n");
	warn_before = kernel_warnings(&rcu_before);
	if (warn_before < 0)
		ksft_exit_skip("cannot read the kernel log\n");
	if (rcu_before)
		ksft_exit_skip("the RCU context-switch warning already fired in this boot\n");
	ksft_set_plan(1);

	atexit(cleanup);
	snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/ppps_uffd_lock_%d",
		 (int)getpid());
	if (mkdir(cgroup_path, 0755)) {
		cgroup_path[0] = '\0';
		ksft_exit_skip("cannot create a memory cgroup: %s\n", strerror(errno));
	}
	snprintf(path, sizeof(path), "%s/memory.reclaim", cgroup_path);
	if (access(path, W_OK))
		ksft_exit_skip("memory.reclaim is unavailable\n");
	/* /proc/swaps lists a swap file by its canonical absolute path. */
	if (!realpath(dir ? dir : ".", swap_dir))
		ksft_exit_skip("cannot resolve swap directory %s: %s\n",
			       dir ? dir : ".", strerror(errno));
	setup_swapfile(swap_dir);
	index = swap_index(swap_path);
	ksft_print_msg("swap file %s is swap area %d, priority %d\n", swap_path,
		       index, SWAP_PRIORITY);
	if (index < 0)
		ksft_exit_fail_msg("the swap file is missing from /proc/swaps\n");

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_path);
		snprintf(pid_text, sizeof(pid_text), "%d", (int)getpid());
		if (!write_file(path, pid_text))
			_exit(CHILD_SKIP);
		snprintf(type_text, sizeof(type_text), "%d", index);
		ppps_execl(true, NULL, "--child", cgroup_path, type_text, NULL);
		_exit(CHILD_FAIL);
	}
	waitpid(pid, &status, 0);
	warn_after = kernel_warnings(&rcu_after);
	ksft_print_msg("child exit status %d; kernel warnings %d -> %d, RCU context-switch warnings %d -> %d\n",
		       WIFEXITED(status) ? WEXITSTATUS(status) : -1, warn_before,
		       warn_after, rcu_before, rcu_after);
	cleanup();

	if (rcu_after > rcu_before || warn_after > warn_before ||
	    !WIFEXITED(status) || WEXITSTATUS(status) == CHILD_FAIL) {
		ksft_test_result_fail("UFFDIO_MOVE of a swapped compat window does not sleep with page tables mapped\n");
		ksft_finished();
	}
	if (WEXITSTATUS(status) == CHILD_SKIP) {
		ksft_test_result_skip("no move had to wait for a locked swap cache folio\n");
		ksft_finished();
	}
	ksft_test_result_pass("UFFDIO_MOVE of a swapped compat window does not sleep with page tables mapped\n");
	ksft_finished();
}
