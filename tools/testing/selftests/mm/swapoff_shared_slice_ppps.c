// SPDX-License-Identifier: GPL-2.0
/*
 * All four 4K slice PTEs of a compat native page share one swap entry, and
 * only the slice that try_to_unmap() visited first carries the exclusive
 * marker.  When swapoff(2) brings that slice back it must not mark the page
 * PageAnonExclusive while sibling slices still hold swap PTEs for the same
 * entry: a later fault on such a sibling finds an exclusive anonymous page
 * behind a swap PTE, which do_swap_page() treats as a BUG.
 *
 * swapoff walks a process in address order and a window's slices are
 * adjacent, so the natural race between swapoff and a sibling fault is a
 * few microseconds wide.  The test widens it deterministically: after the
 * windows are paged out, one swapped slice of every window is moved with
 * mremap() (same slice index, 16K-multiple distance) below a large swapped
 * filler, so swapoff brings it back long before it reaches the remaining
 * slices.  Windows rotate the moved slice through 0..3, so whichever slice
 * holds the exclusive marker is moved in a quarter of them.  A compat child
 * waits until a moved slice is present again and then touches its
 * siblings while swapoff is still working through the filler.
 *
 * Swap slots come from a per-CPU cluster cache before swap priorities are
 * consulted, so a CPU that last swapped to the system's swap keeps using it
 * until its cached cluster is full.  The child therefore stays on one CPU
 * and pages out the 32 MiB filler first, which uses up any such cluster and
 * leaves the CPU allocating from the private device, before the windows.
 *
 * Needs root and zram: the parent hot-adds a private zram swap device with
 * the highest priority, runs swapoff on it and removes it again; the
 * system's own swap is not touched.  Fails when the kernel log gains a BUG,
 * when swapoff fails or when any byte changes; skips when the race window
 * was not reached for every slice position.
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>

#include "ppps_tuple_test.h"

#define NR_WINDOWS		64
#define FILLER_SIZE		(32UL << 20)
#define ZRAM_DISKSIZE		(256UL << 20)
#define SWAP_PRIORITY		32000
#define RACE_TIMEOUT_NS		(120ULL * 1000000000ULL)

#define SWAP_VERSION_OFFSET	1024
#define SWAP_LAST_PAGE_OFFSET	1028
#define SWAP_MAGIC		"SWAPSPACE2"
#define SWAP_MAGIC_SIZE		10

#define SYSLOG_ACTION_READ_ALL		3
#define SYSLOG_ACTION_SIZE_BUFFER	10

#define CHILD_PASS	0
#define CHILD_FAIL	1
#define CHILD_SKIP	4

/* ---------------------------------------------------------------- child */

static int pagemap_fd = -1;

static uint64_t pagemap_of(const void *address)
{
	uint64_t entry = 0;
	off_t offset = (off_t)((uintptr_t)address / PROCESS_PAGE_SIZE) *
		       sizeof(entry);

	if (pread(pagemap_fd, &entry, sizeof(entry), offset) != sizeof(entry))
		return 0;
	return entry;
}

/* Never zero, and different for the four slices of a window. */
static unsigned char slice_byte(unsigned int window, unsigned int slice)
{
	return (unsigned char)(0x10 + (window * PPPS_SLICES + slice) % 0xe0);
}

static uint64_t filler_seed(uint64_t x)
{
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

static void fill_filler(uint64_t *filler, size_t words)
{
	uint64_t x = 0x9e3779b97f4a7c15ULL;
	size_t i;

	for (i = 0; i < words; i++) {
		x = filler_seed(x);
		filler[i] = x;
	}
}

static size_t check_filler(const uint64_t *filler, size_t words)
{
	uint64_t x = 0x9e3779b97f4a7c15ULL;
	size_t i, bad = 0;

	for (i = 0; i < words; i++) {
		x = filler_seed(x);
		bad += filler[i] != x;
	}
	return bad;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static bool send_message(int fd, char message)
{
	return write(fd, &message, 1) == 1;
}

static bool message_ready(int fd)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };

	return poll(&pfd, 1, 0) > 0;
}

static int child_main(int in_fd, int out_fd, unsigned int swap_type)
{
	const size_t window_bytes = NR_WINDOWS * NATIVE_PAGE_SIZE;
	unsigned char *reservation, *low, *high;
	uint64_t *filler;
	unsigned int raced[PPPS_SLICES] = { 0 }, touched[PPPS_SLICES] = { 0 };
	unsigned int w, s, not_swapped = 0, mixed = 0;
	size_t bad_filler, bad_bytes = 0;
	unsigned long swap;
	uint64_t deadline;
	cpu_set_t saved, one;
	char message;
	int ret = CHILD_PASS, cpu;

	ppps_require_compat();
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	if (pagemap_fd < 0) {
		ksft_print_msg("child: /proc/self/pagemap: %s\n", strerror(errno));
		return CHILD_FAIL;
	}

	/* [low: move targets][filler][high: windows], in address order. */
	low = map_aligned_prot(2 * window_bytes + FILLER_SIZE, PROT_NONE,
			       &reservation);
	if (low == MAP_FAILED) {
		ksft_print_msg("child: reservation: %s\n", strerror(errno));
		return CHILD_FAIL;
	}
	filler = mmap(low + window_bytes, FILLER_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	high = mmap(low + window_bytes + FILLER_SIZE, window_bytes,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (filler == MAP_FAILED || high == MAP_FAILED) {
		ksft_print_msg("child: mmap: %s\n", strerror(errno));
		return CHILD_FAIL;
	}
	fill_filler(filler, FILLER_SIZE / sizeof(*filler));
	for (w = 0; w < NR_WINDOWS; w++)
		for (s = 0; s < PPPS_SLICES; s++)
			memset(high + w * NATIVE_PAGE_SIZE + s * PROCESS_PAGE_SIZE,
			       slice_byte(w, s), PROCESS_PAGE_SIZE);

	cpu = sched_getcpu();
	if (cpu < 0 || sched_getaffinity(0, sizeof(saved), &saved)) {
		ksft_print_msg("child: cannot read the CPU affinity: %s\n",
			       strerror(errno));
		return CHILD_SKIP;
	}
	CPU_ZERO(&one);
	CPU_SET(cpu, &one);
	if (sched_setaffinity(0, sizeof(one), &one)) {
		ksft_print_msg("child: cannot pin to CPU %d: %s\n", cpu,
			       strerror(errno));
		return CHILD_SKIP;
	}
	if (madvise(filler, FILLER_SIZE, MADV_PAGEOUT) ||
	    !ppps_smaps_bytes(filler, FILLER_SIZE, "Swap", &swap)) {
		ksft_print_msg("child: filler pageout: %s\n", strerror(errno));
		return CHILD_FAIL;
	}
	ksft_print_msg("child: %lu KiB of the %lu KiB filler swapped out on CPU %d\n",
		       swap >> 10, FILLER_SIZE >> 10, cpu);
	/* The windows may share a VMA, and so a smaps entry, with the filler. */
	if (!ppps_smaps_bytes(high, window_bytes, "Swap", &swap) ||
	    !pageout_reaches_swap(high, window_bytes, swap + window_bytes)) {
		ksft_print_msg("child: MADV_PAGEOUT did not swap out the windows\n");
		return CHILD_SKIP;
	}
	sched_setaffinity(0, sizeof(saved), &saved);

	for (w = 0; w < NR_WINDOWS; w++) {
		for (s = 0; s < PPPS_SLICES; s++) {
			uint64_t e = pagemap_of(high + w * NATIVE_PAGE_SIZE +
						s * PROCESS_PAGE_SIZE);
			unsigned int type = e & 0x1f;

			if ((e & PAGEMAP_PRESENT) || !(e & PAGEMAP_SWAPPED)) {
				not_swapped++;
				continue;
			}
			if (type != swap_type)
				mixed++;
		}
	}
	if (not_swapped || mixed) {
		ksft_print_msg("child: %u slices not swapped, %u not on swap type %u\n",
			       not_swapped, mixed, swap_type);
		return CHILD_SKIP;
	}
	ksft_print_msg("child: all %u window slices swapped (swap type %u)\n",
		       NR_WINDOWS * (unsigned int)PPPS_SLICES, swap_type);

	/* Move slice w % 4 of window w below the filler, keeping its slice. */
	for (w = 0; w < NR_WINDOWS; w++) {
		unsigned char *src, *dst;

		s = w % PPPS_SLICES;
		src = high + w * NATIVE_PAGE_SIZE + s * PROCESS_PAGE_SIZE;
		dst = low + w * NATIVE_PAGE_SIZE + s * PROCESS_PAGE_SIZE;
		if (mremap(src, PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			   MREMAP_MAYMOVE | MREMAP_FIXED, dst) != dst) {
			ksft_print_msg("child: mremap of window %u slice %u: %s\n",
				       w, s, strerror(errno));
			return CHILD_FAIL;
		}
		if (!ppps_page_swapped(dst)) {
			ksft_print_msg("child: moved window %u slice %u is not a swap entry\n",
				       w, s);
			return CHILD_FAIL;
		}
	}

	if (!send_message(out_fd, 'R') || read(in_fd, &message, 1) != 1 ||
	    message != 'G')
		return CHILD_FAIL;

	/*
	 * swapoff is running.  As soon as a moved slice is present again,
	 * touch the siblings that are still swap entries.
	 */
	deadline = now_ns() + RACE_TIMEOUT_NS;
	for (w = 0; w < NR_WINDOWS; w++) {
		unsigned char *dst;
		unsigned int k, spins = 0;
		bool present = false;

		s = w % PPPS_SLICES;
		dst = low + w * NATIVE_PAGE_SIZE + s * PROCESS_PAGE_SIZE;
		for (;;) {
			if (pagemap_of(dst) & PAGEMAP_PRESENT) {
				present = true;
				break;
			}
			if (!(++spins & 255) &&
			    (message_ready(in_fd) || now_ns() > deadline))
				break;
		}
		if (!present)
			break;
		for (k = 0; k < PPPS_SLICES; k++) {
			unsigned char *sibling = high + w * NATIVE_PAGE_SIZE +
						 k * PROCESS_PAGE_SIZE;
			uint64_t e;

			if (k == s)
				continue;
			e = pagemap_of(sibling);
			if (!(e & PAGEMAP_PRESENT) && (e & PAGEMAP_SWAPPED))
				raced[s]++;
			touched[s]++;
			if (*(volatile unsigned char *)sibling != slice_byte(w, k))
				bad_bytes++;
		}
	}

	if (read(in_fd, &message, 1) != 1 || message != 'D')
		return CHILD_FAIL;

	for (w = 0; w < NR_WINDOWS; w++) {
		for (s = 0; s < PPPS_SLICES; s++) {
			unsigned char *p = (s == w % PPPS_SLICES ? low : high) +
					   w * NATIVE_PAGE_SIZE +
					   s * PROCESS_PAGE_SIZE;

			if (!all_bytes_are(p, PROCESS_PAGE_SIZE, slice_byte(w, s))) {
				if (!bad_bytes)
					ksft_print_msg("child: window %u slice %u reads 0x%02x, expected 0x%02x\n",
						       w, s, p[0], slice_byte(w, s));
				bad_bytes++;
			}
			if (!(pagemap_of(p) & PAGEMAP_PRESENT))
				not_swapped++;
		}
	}
	bad_filler = check_filler(filler, FILLER_SIZE / sizeof(*filler));
	for (s = 0; s < PPPS_SLICES; s++)
		ksft_print_msg("child: moved slice %u: %u sibling faults, %u while still a swap entry\n",
			       s, touched[s], raced[s]);
	if (bad_bytes || bad_filler) {
		ksft_print_msg("child: %zu bad window slices, %zu bad filler words\n",
			       bad_bytes, bad_filler);
		ret = CHILD_FAIL;
	}
	if (not_swapped) {
		ksft_print_msg("child: %u window slices not present after swapoff\n",
			       not_swapped);
		ret = CHILD_FAIL;
	}
	if (ret == CHILD_PASS) {
		for (s = 0; s < PPPS_SLICES; s++)
			if (!raced[s])
				ret = CHILD_SKIP;
		if (ret == CHILD_SKIP)
			ksft_print_msg("child: the race window was not reached for every moved slice\n");
	}
	munmap(reservation, 2 * window_bytes + FILLER_SIZE + NATIVE_PAGE_SIZE);
	return ret;
}

/* --------------------------------------------------------------- parent */

static int zram_id = -1;
static char zram_dev[PATH_MAX];
static bool swap_active;

static bool write_sysfs(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	bool ok;

	if (fd < 0)
		return false;
	ok = write(fd, value, strlen(value)) == (ssize_t)strlen(value);
	close(fd);
	return ok;
}

static void cleanup(void)
{
	char value[16];
	int i;

	for (i = 0; swap_active && i < 3; i++)
		if (!swapoff(zram_dev))
			swap_active = false;
	if (swap_active)
		ksft_print_msg("cannot swapoff %s: %s; device left in place\n",
			       zram_dev, strerror(errno));
	if (zram_id >= 0 && !swap_active) {
		snprintf(value, sizeof(value), "%d", zram_id);
		if (!write_sysfs("/sys/class/zram-control/hot_remove", value))
			ksft_print_msg("cannot hot_remove zram%d: %s\n", zram_id,
				       strerror(errno));
		zram_id = -1;
	}
}

static int kernel_bugs(void)
{
	int size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0), len, count = 0;
	char *log, *line, *save = NULL;

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
	     line = strtok_r(NULL, "\n", &save))
		if (strstr(line, "kernel BUG at") ||
		    strstr(line, "Internal error") ||
		    strstr(line, "BUG: Bad page") ||
		    strstr(line, "BUG: KASAN"))
			count++;
	free(log);
	return count;
}

/* Hot-add a zram disk, format it with a native swap header and swapon it. */
static void setup_swap(void)
{
	unsigned char *header;
	char path[PATH_MAX], value[32];
	struct stat st;
	int fd, i;

	fd = open("/sys/class/zram-control/hot_add", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		ksft_exit_skip("zram hot_add is unavailable: %s\n", strerror(errno));
	i = read(fd, value, sizeof(value) - 1);
	close(fd);
	if (i <= 0)
		ksft_exit_fail_msg("zram hot_add: %s\n", strerror(errno));
	value[i] = '\0';
	zram_id = atoi(value);
	snprintf(path, sizeof(path), "/sys/block/zram%d/disksize", zram_id);
	snprintf(value, sizeof(value), "%lu", ZRAM_DISKSIZE);
	if (!write_sysfs(path, value))
		ksft_exit_fail_msg("%s: %s\n", path, strerror(errno));

	snprintf(zram_dev, sizeof(zram_dev), "/dev/block/zram%d", zram_id);
	for (i = 0; i < 500 && stat(zram_dev, &st); i++)
		usleep(10000);
	if (stat(zram_dev, &st) || !S_ISBLK(st.st_mode))
		ksft_exit_fail_msg("%s did not appear\n", zram_dev);

	header = calloc(1, NATIVE_PAGE_SIZE);
	if (!header)
		ksft_exit_fail_msg("calloc\n");
	*(uint32_t *)(header + SWAP_VERSION_OFFSET) = 1;
	*(uint32_t *)(header + SWAP_LAST_PAGE_OFFSET) =
		ZRAM_DISKSIZE / NATIVE_PAGE_SIZE - 1;
	memcpy(header + NATIVE_PAGE_SIZE - SWAP_MAGIC_SIZE, SWAP_MAGIC,
	       SWAP_MAGIC_SIZE);
	fd = open(zram_dev, O_WRONLY | O_CLOEXEC);
	if (fd < 0 || pwrite(fd, header, NATIVE_PAGE_SIZE, 0) != NATIVE_PAGE_SIZE ||
	    fsync(fd))
		ksft_exit_fail_msg("formatting %s: %s\n", zram_dev, strerror(errno));
	close(fd);
	free(header);
	if (swapon(zram_dev, SWAP_FLAG_PREFER |
			     (SWAP_PRIORITY << SWAP_FLAG_PRIO_SHIFT)))
		ksft_exit_fail_msg("swapon %s: %s\n", zram_dev, strerror(errno));
	swap_active = true;
}

/* Used KiB of @dev in /proc/swaps, or -1. */
static long swap_used_kb(const char *dev)
{
	char line[512], name[PATH_MAX];
	long size, used;
	FILE *f = fopen("/proc/swaps", "re");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%4095s %*s %ld %ld", name, &size, &used) == 3 &&
		    !strcmp(name, dev)) {
			fclose(f);
			return used;
		}
	}
	fclose(f);
	return -1;
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
	int to_child[2], from_child[2], status, bugs_before, bugs_after, index;
	char in_arg[16], out_arg[16], type_arg[16], message;
	struct timespec start, end;
	long used;
	pid_t pid;
	int ret, error;
	bool ok;

	ppps_argv0 = argv[0];
	if (argc == 5 && !strcmp(argv[1], "--child"))
		return child_main(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));

	ksft_print_header();
	if (argc != 1)
		ksft_exit_fail_msg("usage: %s\n", argv[0]);
	if (getpagesize() != NATIVE_PAGE_SIZE)
		ksft_exit_skip("the parent must be a native 16K process\n");
	if (geteuid())
		ksft_exit_skip("root is required for swapon(2)\n");
	bugs_before = kernel_bugs();
	if (bugs_before < 0)
		ksft_exit_skip("cannot read the kernel log\n");
	ksft_set_plan(1);

	atexit(cleanup);
	setup_swap();
	index = swap_index(zram_dev);
	ksft_print_msg("swap on %s (swap type %d), priority %d\n", zram_dev,
		       index, SWAP_PRIORITY);
	if (index < 0)
		ksft_exit_fail_msg("%s is missing from /proc/swaps\n", zram_dev);

	if (pipe2(to_child, 0) || pipe2(from_child, 0))
		ksft_exit_fail_msg("pipe: %s\n", strerror(errno));
	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork: %s\n", strerror(errno));
	if (!pid) {
		close(to_child[1]);
		close(from_child[0]);
		snprintf(in_arg, sizeof(in_arg), "%d", to_child[0]);
		snprintf(out_arg, sizeof(out_arg), "%d", from_child[1]);
		snprintf(type_arg, sizeof(type_arg), "%d", index);
		ppps_execl(true, NULL, "--child", in_arg, out_arg, type_arg,
			   NULL);
		_exit(CHILD_FAIL);
	}
	close(to_child[0]);
	close(from_child[1]);

	if (read(from_child[0], &message, 1) != 1 || message != 'R') {
		waitpid(pid, &status, 0);
		ret = WIFEXITED(status) ? WEXITSTATUS(status) : CHILD_FAIL;
		if (ret == CHILD_SKIP)
			ksft_test_result_skip("compat child could not set up swapped windows\n");
		else
			ksft_test_result_fail("compat child failed during setup\n");
		ksft_finished();
	}
	used = swap_used_kb(zram_dev);
	ksft_print_msg("%s holds %ld KiB before swapoff\n", zram_dev, used);
	if (used < (long)((NR_WINDOWS * NATIVE_PAGE_SIZE) >> 10)) {
		send_message(to_child[1], 'G');
		send_message(to_child[1], 'D');
		waitpid(pid, &status, 0);
		ksft_test_result_skip("the windows did not reach %s\n", zram_dev);
		ksft_finished();
	}

	if (!send_message(to_child[1], 'G'))
		ksft_exit_fail_msg("pipe write: %s\n", strerror(errno));
	clock_gettime(CLOCK_MONOTONIC, &start);
	ret = swapoff(zram_dev);
	error = errno;
	clock_gettime(CLOCK_MONOTONIC, &end);
	if (!ret)
		swap_active = false;
	ksft_print_msg("swapoff = %d (%s) after %ld ms\n", ret,
		       ret ? strerror(error) : "ok",
		       (long)((end.tv_sec - start.tv_sec) * 1000 +
			      (end.tv_nsec - start.tv_nsec) / 1000000));
	if (!send_message(to_child[1], 'D'))
		ksft_print_msg("pipe write: %s\n", strerror(errno));
	waitpid(pid, &status, 0);
	bugs_after = kernel_bugs();
	ksft_print_msg("child exit status %d, kernel BUG reports %d -> %d\n",
		       WIFEXITED(status) ? WEXITSTATUS(status) : -1,
		       bugs_before, bugs_after);
	cleanup();

	ok = !ret && bugs_after >= 0 && bugs_after <= bugs_before &&
	     WIFEXITED(status);
	if (ok && WEXITSTATUS(status) == CHILD_SKIP) {
		ksft_test_result_skip("swapoff finished before the siblings could be touched\n");
		ksft_finished();
	}
	ksft_test_result(ok && WEXITSTATUS(status) == CHILD_PASS,
			 "swapoff of a shared compat swap entry with sibling slices still swapped out\n");
	ksft_finished();
}
