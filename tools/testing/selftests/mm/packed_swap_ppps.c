// SPDX-License-Identifier: GPL-2.0
/*
 * Four 4K anonymous slices packed into one native folio keep their data and
 * identity across pageout/swapin in any order, partial MADV_DONTNEED, fork
 * COW, concurrent swapin, reclaim churn, compaction and near-capacity swap
 * pressure; private file COW pages stay outside tuple accounting.
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ppps_tuple_test.h"

#define CONCURRENT_TUPLES 64
#define CHURN_TUPLES 256
#define COMPACT_TUPLES 1024
#define PRESSURE_TUPLES 12288
#define PRESSURE_CHURN 2048

struct mapping {
	unsigned char *base;
	unsigned char *reservation;
	size_t reservation_size;
};

struct worker {
	unsigned char *base;
	unsigned int slice;
};

struct test_cgroup {
	char parent[PATH_MAX];
	char child[PATH_MAX];
	bool active;
};

static struct mapping map_tuples(size_t size)
{
	struct mapping result = {
		.reservation_size = size + NATIVE_PAGE_SIZE,
	};

	result.base = map_aligned(size, &result.reservation);
	return result;
}

static void unmap_mapping(struct mapping *mapping)
{
	if (mapping->base != MAP_FAILED)
		munmap(mapping->reservation, mapping->reservation_size);
	mapping->base = MAP_FAILED;
}

static bool reclaim_self_cgroup(unsigned long bytes)
{
	char cgroup[PATH_MAX];
	char path[PATH_MAX + 32];
	char request[64];
	char *line = NULL;
	size_t capacity = 0;
	bool found = false;
	FILE *cgroups;
	int path_length;
	ssize_t length;
	ssize_t written;
	int fd;

	cgroups = fopen("/proc/self/cgroup", "re");
	if (!cgroups)
		return false;
	while (getline(&line, &capacity, cgroups) >= 0) {
		if (sscanf(line, "0::%4095[^\n]", cgroup) == 1) {
			found = true;
			break;
		}
	}
	free(line);
	fclose(cgroups);
	if (!found)
		return false;
	path_length = snprintf(path, sizeof(path),
			       "/sys/fs/cgroup%s/memory.reclaim", cgroup);
	if (path_length < 0 || (size_t)path_length >= sizeof(path))
		return false;
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	length = snprintf(request, sizeof(request), "%lu swappiness=max", bytes);
	written = write(fd, request, length);
	found = written == length || (written < 0 && errno == EAGAIN);
	close(fd);
	return found;
}

static bool write_pid_to_cgroup(const char *directory)
{
	char path[PATH_MAX];
	char pid[32];
	int path_length;
	ssize_t length;
	int fd;

	path_length = snprintf(path, sizeof(path), "%s/cgroup.procs", directory);
	if (path_length < 0 || (size_t)path_length >= sizeof(path))
		return false;
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	length = snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	length = write(fd, pid, length);
	close(fd);
	return length > 0;
}

static bool enter_private_cgroup(struct test_cgroup *group)
{
	char cgroup[PATH_MAX];
	char *line = NULL;
	size_t capacity = 0;
	FILE *cgroups;
	bool found = false;
	int length;

	memset(group, 0, sizeof(*group));
	cgroups = fopen("/proc/self/cgroup", "re");
	if (!cgroups)
		return false;
	while (getline(&line, &capacity, cgroups) >= 0) {
		if (sscanf(line, "0::%4095[^\n]", cgroup) == 1) {
			found = true;
			break;
		}
	}
	free(line);
	fclose(cgroups);
	if (!found)
		return false;
	length = snprintf(group->parent, sizeof(group->parent),
			  "/sys/fs/cgroup%s", cgroup);
	if (length < 0 || (size_t)length >= sizeof(group->parent))
		return false;
	/* The Android per-process cgroup is a leaf without delegated memory. */
	length = snprintf(group->child, sizeof(group->child),
			  "/sys/fs/cgroup/ppps-%ld", (long)getpid());
	if (length < 0 || (size_t)length >= sizeof(group->child) ||
	    mkdir(group->child, 0700))
		return false;
	if (!write_pid_to_cgroup(group->child)) {
		rmdir(group->child);
		return false;
	}
	group->active = true;
	return true;
}

static void leave_private_cgroup(struct test_cgroup *group)
{
	if (!group->active)
		return;
	write_pid_to_cgroup(group->parent);
	rmdir(group->child);
	group->active = false;
}

static bool cgroup_reclaim_reaches_swap(void *address, unsigned long length,
					unsigned long minimum)
{
	unsigned long swap;
	unsigned int attempt;

	for (attempt = 0; attempt < PAGEOUT_POLLS; attempt++) {
		if (!ppps_smaps_bytes(address, length, "Swap", &swap))
			return false;
		if (swap >= minimum)
			return true;
		if (!reclaim_self_cgroup(length))
			return false;
		usleep(10000);
	}
	return false;
}

static bool swap_is_active(void)
{
	unsigned long total_kb = 0;
	char *line = NULL;
	size_t capacity = 0;
	FILE *meminfo;

	meminfo = fopen("/proc/meminfo", "re");
	if (!meminfo)
		return false;
	while (getline(&line, &capacity, meminfo) >= 0)
		if (sscanf(line, "SwapTotal: %lu kB", &total_kb) == 1)
			break;
	free(line);
	fclose(meminfo);
	return total_kb != 0;
}

static unsigned long swap_free_bytes(void)
{
	unsigned long free_kb = 0;
	char *line = NULL;
	size_t capacity = 0;
	FILE *meminfo;

	meminfo = fopen("/proc/meminfo", "re");
	if (!meminfo)
		return 0;
	while (getline(&line, &capacity, meminfo) >= 0)
		if (sscanf(line, "SwapFree: %lu kB", &free_kb) == 1)
			break;
	free(line);
	fclose(meminfo);
	return free_kb * 1024;
}

static bool tuple_has_one_folio(const unsigned char *base)
{
	uint64_t pfn[PPPS_SLICES];

	return read_pfns(base, pfn) && same_pfn(pfn);
}

static bool full_pageout_swapin(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	const unsigned int order[PPPS_SLICES] = { 2, 0, 3, 1 };
	unsigned long rss, swap;
	unsigned int i;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x21);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	for (i = 0; passed && i < PPPS_SLICES; i++)
		passed = mapping.base[order[i] * PROCESS_PAGE_SIZE] == 0x21 + order[i];
	passed = passed && check_tuple(mapping.base, 0x21) &&
		tuple_has_one_folio(mapping.base) &&
		ppps_smaps_bytes(mapping.base, NATIVE_PAGE_SIZE, "Rss", &rss) &&
		rss == NATIVE_PAGE_SIZE &&
		ppps_smaps_bytes(mapping.base, NATIVE_PAGE_SIZE, "Swap", &swap) &&
		!swap;
	unmap_mapping(&mapping);
	return passed;
}

static bool first_swapin_is_write(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x31);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	if (passed)
		memset(mapping.base + 2 * PROCESS_PAGE_SIZE, 0x73, PROCESS_PAGE_SIZE);
	passed = passed &&
		mapping.base[0] == 0x31 &&
		mapping.base[PROCESS_PAGE_SIZE] == 0x32 &&
		mapping.base[2 * PROCESS_PAGE_SIZE] == 0x73 &&
		mapping.base[3 * PROCESS_PAGE_SIZE] == 0x34 &&
		tuple_has_one_folio(mapping.base);
	unmap_mapping(&mapping);
	return passed;
}

static bool read_swapin_write_pageout(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	unsigned char *slice;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x41);
	slice = mapping.base + PROCESS_PAGE_SIZE;
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	/*
	 * A read fault swaps the tuple in clean and writable.  The store that
	 * follows must dirty the PTE again, otherwise the next pageout drops
	 * the folio and the swap slot still holds the old contents.
	 */
	passed = passed && slice[0] == 0x42;
	if (passed)
		memset(slice, 0x74, PROCESS_PAGE_SIZE);
	passed = passed &&
		pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) &&
		mapping.base[0] == 0x41 &&
		slice[0] == 0x74 && slice[PROCESS_PAGE_SIZE - 1] == 0x74 &&
		mapping.base[2 * PROCESS_PAGE_SIZE] == 0x43 &&
		mapping.base[3 * PROCESS_PAGE_SIZE] == 0x44 &&
		tuple_has_one_folio(mapping.base);
	unmap_mapping(&mapping);
	return passed;
}

static bool private_file_cow_swapin(void)
{
	unsigned char *expected = NULL;
	unsigned char *mapping = MAP_FAILED;
	unsigned char *file_data = NULL;
	unsigned int slice;
	bool passed = false;
	int fd = -1;

	fd = memfd_create("ppps-private-cow", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		goto out;
	expected = malloc(NATIVE_PAGE_SIZE);
	file_data = malloc(NATIVE_PAGE_SIZE);
	if (!expected || !file_data)
		goto out;
	for (slice = 0; slice < PPPS_SLICES; slice++)
		memset(file_data + slice * PROCESS_PAGE_SIZE, 0x40 + slice,
		       PROCESS_PAGE_SIZE);
	if (pwrite(fd, file_data, NATIVE_PAGE_SIZE, 0) != NATIVE_PAGE_SIZE)
		goto out;
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE, fd, 0);
	if (mapping == MAP_FAILED)
		goto out;
	for (slice = 0; slice < PPPS_SLICES; slice++) {
		memset(mapping + slice * PROCESS_PAGE_SIZE, 0x70 + slice,
		       PROCESS_PAGE_SIZE);
		memset(expected + slice * PROCESS_PAGE_SIZE, 0x70 + slice,
		       PROCESS_PAGE_SIZE);
	}
	/*
	 * P3S may COW one 4K slice separately while the remaining slices
	 * still map a partially shared native folio.  MADV_PAGEOUT can then
	 * reclaim the separate slice without reclaiming that native folio.
	 * Require a real private-COW swapin, not full-range eviction.
	 */
	passed = pageout_reaches_swap(mapping, NATIVE_PAGE_SIZE,
				   PROCESS_PAGE_SIZE) &&
		 !memcmp(mapping, expected, NATIVE_PAGE_SIZE) &&
		 pread(fd, file_data, NATIVE_PAGE_SIZE, 0) == NATIVE_PAGE_SIZE;
	for (slice = 0; passed && slice < PPPS_SLICES; slice++)
		passed = file_data[slice * PROCESS_PAGE_SIZE] == 0x40 + slice;
out:
	if (mapping != MAP_FAILED)
		munmap(mapping, NATIVE_PAGE_SIZE);
	free(file_data);
	free(expected);
	if (fd >= 0)
		close(fd);
	return passed;
}

static bool partial_tuple_pageout(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	bool passed;
	int stage = 0;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x41);
	passed = !madvise(mapping.base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			  MADV_DONTNEED);
	if (passed)
		stage = 1;
	passed = passed && pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE,
						3 * PROCESS_PAGE_SIZE);
	if (passed)
		stage = 2;
	passed = passed && mapping.base[3 * PROCESS_PAGE_SIZE] == 0x44 &&
		 mapping.base[0] == 0x41 &&
		 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x43;
	if (passed)
		stage = 3;
	passed = passed && mapping.base[PROCESS_PAGE_SIZE] == 0;
	if (passed)
		stage = 4;
	if (passed)
		memset(mapping.base + PROCESS_PAGE_SIZE, 0x42, PROCESS_PAGE_SIZE);
	passed = passed && check_tuple(mapping.base, 0x41);
	if (passed)
		stage = 5;
	if (!passed)
		ksft_print_msg("partial tuple failed after stage %d\n", stage);
	unmap_mapping(&mapping);
	return passed;
}

static bool fork_swapped_tuple(bool fault_before_fork)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	pid_t child;
	int status;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x51);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	if (passed && fault_before_fork)
		passed = mapping.base[0] == 0x51;
	if (!passed) {
		unmap_mapping(&mapping);
		return false;
	}
	child = fork();
	if (!child) {
		memset(mapping.base + 3 * PROCESS_PAGE_SIZE, 0x7f, PROCESS_PAGE_SIZE);
		passed = mapping.base[0] == 0x51 &&
		 mapping.base[PROCESS_PAGE_SIZE] == 0x52 &&
		 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x53 &&
		 mapping.base[3 * PROCESS_PAGE_SIZE] == 0x7f;
		_exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (child < 0) {
		unmap_mapping(&mapping);
		return false;
	}
	passed = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		 WEXITSTATUS(status) == EXIT_SUCCESS &&
		 check_tuple(mapping.base, 0x51);
	unmap_mapping(&mapping);
	return passed;
}

/*
 * A tuple folio may stay in the swapcache with tuple-level exclusivity while
 * some slices are still exclusive swap PTEs.  Zapping its only present slice
 * and forking must still turn the folio into a shared one: writes to swapped
 * slices in either process have to copy instead of landing in the folio the
 * other process reaches through its swap PTEs.
 */
static bool fork_after_swapin_dontneed(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	unsigned char *slice1 = mapping.base + PROCESS_PAGE_SIZE;
	unsigned char *slice3 = mapping.base + 3 * PROCESS_PAGE_SIZE;
	int pipefd[2] = { -1, -1 };
	unsigned char token;
	pid_t child;
	int status;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x61);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	/* An exclusive read swapin restores tuple-level exclusivity ... */
	passed = passed && slice3[0] == 0x64;
	/* ... then the folio stays in the swapcache without a present slice. */
	passed = passed && !madvise(slice3, PROCESS_PAGE_SIZE, MADV_DONTNEED) &&
		 !pipe(pipefd);
	if (!passed) {
		unmap_mapping(&mapping);
		return false;
	}
	child = fork();
	if (!child) {
		close(pipefd[1]);
		/* Wait for the parent's write, which must not reach us. */
		passed = read(pipefd[0], &token, 1) == 1 && slice1[0] == 0x62;
		/* A non-exclusive swapin followed by a write must copy. */
		passed = passed && mapping.base[0] == 0x61;
		memset(mapping.base, 0x7e, PROCESS_PAGE_SIZE);
		passed = passed && mapping.base[0] == 0x7e &&
			 mapping.base[PROCESS_PAGE_SIZE - 1] == 0x7e &&
			 slice1[0] == 0x62 &&
			 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x63 &&
			 slice3[0] == 0;
		_exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	close(pipefd[0]);
	if (child < 0) {
		close(pipefd[1]);
		unmap_mapping(&mapping);
		return false;
	}
	memset(slice1, 0x7d, PROCESS_PAGE_SIZE);
	passed = write(pipefd[1], &token, 1) == 1;
	close(pipefd[1]);
	passed = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		 WEXITSTATUS(status) == EXIT_SUCCESS && passed;
	/* The child's write must not have reached the parent either. */
	passed = passed && mapping.base[0] == 0x61 &&
		 mapping.base[PROCESS_PAGE_SIZE - 1] == 0x61 &&
		 slice1[0] == 0x7d &&
		 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x63 && slice3[0] == 0;
	unmap_mapping(&mapping);
	return passed;
}

/* Bit 63 of a /proc/self/pagemap entry: the PTE is present. */
static int slices_present(const unsigned char *base)
{
	int fd = open("/proc/self/pagemap", O_RDONLY);
	int present = 0;
	unsigned int slice;

	if (fd < 0)
		return -1;
	for (slice = 0; slice < PPPS_SLICES; slice++) {
		unsigned long addr = (unsigned long)base +
				     slice * PROCESS_PAGE_SIZE;
		uint64_t entry;

		if (pread(fd, &entry, sizeof(entry),
			  (addr / PROCESS_PAGE_SIZE) * sizeof(entry)) !=
		    sizeof(entry)) {
			present = -1;
			break;
		}
		if (entry >> 63)
			present++;
	}
	close(fd);
	return present;
}

/*
 * A 16K kernel swaps a native page in as a whole; so does a packed tuple.  The
 * fault on one slice restores every slice of the tuple that still points at
 * the same swap entry, and the restored tuple is exclusive and writable
 * without further faults or copies.
 */
static bool swapin_restores_whole_tuple(void)
{
	static const unsigned int remaining[] = { 0, 2, 3 };
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	unsigned int stage = 0;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x31);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) &&
		 slices_present(mapping.base) == 0;
	if (passed)
		stage = 1;
	/* One read fault on slice 2 ... */
	passed = passed && mapping.base[2 * PROCESS_PAGE_SIZE] == 0x33;
	if (passed)
		stage = 2;
	/* ... brings all four slices back at once. */
	passed = passed && slices_present(mapping.base) == PPPS_SLICES;
	if (passed)
		stage = 3;
	passed = passed && check_tuple(mapping.base, 0x31) &&
		 tuple_has_one_folio(mapping.base);
	if (passed)
		stage = 4;
	/* A tuple with one slice discarded still comes back as one folio. */
	passed = passed &&
		 !madvise(mapping.base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			  MADV_DONTNEED) &&
		 pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE,
				      (PPPS_SLICES - 1) * PROCESS_PAGE_SIZE);
	if (passed)
		stage = 5;
	passed = passed && mapping.base[3 * PROCESS_PAGE_SIZE] == 0x34 &&
		 slices_present(mapping.base) == PPPS_SLICES - 1;
	if (passed)
		stage = 6;
	passed = passed && mapping.base[0] == 0x31 &&
		 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x33 &&
		 same_present_pfns(mapping.base, remaining, 3) &&
		 mapping.base[PROCESS_PAGE_SIZE] == 0;
	if (!passed)
		ksft_print_msg("whole-tuple swapin failed after stage %u\n",
			       stage);
	unmap_mapping(&mapping);
	return passed;
}

static bool discard_refill_verify_range(unsigned char *base,
					unsigned int slice_bias)
{
	unsigned int tuple;

	for (tuple = 0; tuple < CHURN_TUPLES; tuple++) {
		unsigned char seed = 0x20 + tuple % 32;
		unsigned int slice = (tuple + slice_bias) % PPPS_SLICES;
		unsigned char *tuple_base = base + tuple * NATIVE_PAGE_SIZE;

		if (madvise(tuple_base + slice * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			    MADV_DONTNEED))
			return false;
		memset(tuple_base + slice * PROCESS_PAGE_SIZE, seed + slice,
		       PROCESS_PAGE_SIZE);
		if (!check_tuple(tuple_base, seed))
			return false;
	}
	return true;
}

static bool rewrite_verify_range(unsigned char *base, unsigned int slice_bias)
{
	unsigned int tuple;

	for (tuple = 0; tuple < CHURN_TUPLES; tuple++) {
		unsigned char seed = 0x20 + tuple % 32;
		unsigned int slice = (tuple + slice_bias) % PPPS_SLICES;
		unsigned char *tuple_base = base + tuple * NATIVE_PAGE_SIZE;

		memset(tuple_base + slice * PROCESS_PAGE_SIZE, seed + slice,
		       PROCESS_PAGE_SIZE);
		if (!check_tuple(tuple_base, seed))
			return false;
	}
	return true;
}

static bool fork_before_pageout(void)
{
	const size_t length = CHURN_TUPLES * NATIVE_PAGE_SIZE;
	struct mapping mapping = map_tuples(length);
	struct test_cgroup cgroup = {};
	int ready[2] = { -1, -1 };
	int start[2] = { -1, -1 };
	unsigned int tuple;
	unsigned char byte = 1;
	pid_t child;
	int status;
	int parent_stage = 0;
	pid_t waited;
	bool passed = true;

	if (mapping.base == MAP_FAILED || pipe(ready) || pipe(start)) {
		passed = false;
		goto out;
	}
	if (!enter_private_cgroup(&cgroup)) {
		passed = false;
		goto out;
	}
	for (tuple = 0; tuple < CHURN_TUPLES; tuple++)
		fill_tuple(mapping.base + tuple * NATIVE_PAGE_SIZE,
			   0x20 + tuple % 32);

	child = fork();
	if (!child) {
		bool child_passed;

		close(ready[0]);
		close(start[1]);
		if (write(ready[1], &byte, 1) != 1 ||
		    read(start[0], &byte, 1) != 1)
			_exit(10);
		child_passed = rewrite_verify_range(mapping.base, 0);
		if (!child_passed)
			_exit(11);
		child_passed = discard_refill_verify_range(mapping.base, 2);
		_exit(child_passed ? EXIT_SUCCESS : 12);
	}
	if (child < 0) {
		passed = false;
		goto out;
	}
	close(ready[1]);
	ready[1] = -1;
	close(start[0]);
	start[0] = -1;
	passed = read(ready[0], &byte, 1) == 1;
	if (passed)
		parent_stage = 1;
	if (passed)
		passed = cgroup_reclaim_reaches_swap(mapping.base, length,
						     length);
	if (passed)
		parent_stage = 2;
	if (write(start[1], &byte, 1) != 1)
		passed = false;
	else if (passed)
		parent_stage = 3;
	if (passed)
		passed = rewrite_verify_range(mapping.base, 1);
	if (passed)
		parent_stage = 4;
	if (passed)
		passed = discard_refill_verify_range(mapping.base, 3);
	if (passed)
		parent_stage = 5;
	waited = waitpid(child, &status, 0);
	passed = waited == child && WIFEXITED(status) &&
		 WEXITSTATUS(status) == EXIT_SUCCESS && passed;
	if (!passed)
		ksft_print_msg("pre-fork reclaim failed: parent_stage=%d "
			       "waited=%ld child_status=%#x\n", parent_stage,
			       (long)waited, status);
out:
	if (ready[0] >= 0)
		close(ready[0]);
	if (ready[1] >= 0)
		close(ready[1]);
	if (start[0] >= 0)
		close(start[0]);
	if (start[1] >= 0)
		close(start[1]);
	unmap_mapping(&mapping);
	leave_private_cgroup(&cgroup);
	return passed;
}

static bool fork_after_hole_fill(void)
{
	struct mapping mapping = map_tuples(NATIVE_PAGE_SIZE);
	pid_t child;
	int status;
	bool passed;

	if (mapping.base == MAP_FAILED)
		return false;
	fill_tuple(mapping.base, 0x58);
	passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) &&
		!madvise(mapping.base, PROCESS_PAGE_SIZE, MADV_DONTNEED);
	if (passed)
		memset(mapping.base, 0x58, PROCESS_PAGE_SIZE);
	if (!passed) {
		unmap_mapping(&mapping);
		return false;
	}
	child = fork();
	if (!child) {
		memset(mapping.base + 3 * PROCESS_PAGE_SIZE, 0x7f, PROCESS_PAGE_SIZE);
		passed = pageout_reaches_swap(mapping.base, NATIVE_PAGE_SIZE,
					      2 * PROCESS_PAGE_SIZE) &&
			 mapping.base[0] == 0x58 &&
			 mapping.base[PROCESS_PAGE_SIZE] == 0x59 &&
			 mapping.base[2 * PROCESS_PAGE_SIZE] == 0x5a &&
			 mapping.base[3 * PROCESS_PAGE_SIZE] == 0x7f;
		_exit(passed ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (child < 0) {
		unmap_mapping(&mapping);
		return false;
	}
	passed = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		 WEXITSTATUS(status) == EXIT_SUCCESS &&
		 check_tuple(mapping.base, 0x58);
	unmap_mapping(&mapping);
	return passed;
}

static void *swapin_worker(void *argument)
{
	struct worker *worker = argument;
	unsigned int tuple;

	for (tuple = 0; tuple < CONCURRENT_TUPLES; tuple++)
		memset(worker->base + tuple * NATIVE_PAGE_SIZE +
		       worker->slice * PROCESS_PAGE_SIZE,
		       0x70 + worker->slice, PROCESS_PAGE_SIZE);
	return NULL;
}

static bool concurrent_swapin(void)
{
	const size_t length = CONCURRENT_TUPLES * NATIVE_PAGE_SIZE;
	struct mapping mapping = map_tuples(length);
	struct worker worker[PPPS_SLICES];
	pthread_t thread[PPPS_SLICES];
	unsigned int created = 0;
	unsigned int slice, tuple;
	bool passed = true;

	if (mapping.base == MAP_FAILED)
		return false;
	for (tuple = 0; tuple < CONCURRENT_TUPLES; tuple++)
		fill_tuple(mapping.base + tuple * NATIVE_PAGE_SIZE, 0x61);
	passed = pageout_reaches_swap(mapping.base, length, length);
	for (slice = 0; passed && slice < PPPS_SLICES; slice++) {
		worker[slice].base = mapping.base;
		worker[slice].slice = slice;
		if (pthread_create(&thread[slice], NULL, swapin_worker,
				   &worker[slice]))
			passed = false;
		else
			created++;
	}
	for (slice = 0; slice < created; slice++)
		if (pthread_join(thread[slice], NULL))
			passed = false;
	for (tuple = 0; passed && tuple < CONCURRENT_TUPLES; tuple++) {
		unsigned char *base = mapping.base + tuple * NATIVE_PAGE_SIZE;

		for (slice = 0; slice < PPPS_SLICES; slice++)
			if (base[slice * PROCESS_PAGE_SIZE] != 0x70 + slice)
				passed = false;
		if (passed && !tuple_has_one_folio(base))
			passed = false;
	}
	unmap_mapping(&mapping);
	return passed;
}

static bool pageout_dontneed_churn(void)
{
	const size_t length = CHURN_TUPLES * NATIVE_PAGE_SIZE;
	struct mapping mapping = map_tuples(length);
	unsigned int round, tuple;
	unsigned int max_split = 0;
	bool passed = true;

	if (mapping.base == MAP_FAILED)
		return false;
	for (tuple = 0; tuple < CHURN_TUPLES; tuple++)
		fill_tuple(mapping.base + tuple * NATIVE_PAGE_SIZE,
			   0x20 + tuple % 32);
	for (round = 0; passed && round < 8; round++) {
		unsigned int split = 0;

		passed = pageout_reaches_swap(mapping.base, length, length / 2);
		for (tuple = 0; passed && tuple < CHURN_TUPLES; tuple++) {
			unsigned char seed = 0x20 + tuple % 32;
			unsigned int slice = (tuple + round) % PPPS_SLICES;
			unsigned char *base = mapping.base + tuple * NATIVE_PAGE_SIZE;

			if ((tuple + round) % 3 == 0) {
				passed = !madvise(base + slice * PROCESS_PAGE_SIZE,
						  PROCESS_PAGE_SIZE, MADV_DONTNEED);
				if (passed)
					memset(base + slice * PROCESS_PAGE_SIZE,
					       seed + slice, PROCESS_PAGE_SIZE);
			}
			if (passed && !check_tuple(base, seed)) {
				ksft_print_msg("churn data mismatch at round %u tuple %u "
					       "bytes=%02x/%02x/%02x/%02x\n",
					       round, tuple, base[0],
					       base[PROCESS_PAGE_SIZE],
					       base[2 * PROCESS_PAGE_SIZE],
					       base[3 * PROCESS_PAGE_SIZE]);
				passed = false;
			}
			if (passed && !tuple_has_one_folio(base))
				split++;
		}
		if (split > max_split)
			max_split = split;
	}
	if (!passed)
		ksft_print_msg("churn failed at round %u tuple %u\n",
			       round, tuple);
	else if (max_split)
		ksft_print_msg("churn peak split tuples: %u/%u\n",
			       max_split, CHURN_TUPLES);
	unmap_mapping(&mapping);
	return passed;
}

static bool compaction_preserves_tuples(void)
{
	const size_t length = COMPACT_TUPLES * NATIVE_PAGE_SIZE;
	struct mapping mapping = map_tuples(length);
	unsigned int tuple, attempt;
	bool passed = true;
	int fd;

	if (mapping.base == MAP_FAILED)
		return false;
	for (tuple = 0; tuple < COMPACT_TUPLES; tuple++)
		fill_tuple(mapping.base + tuple * NATIVE_PAGE_SIZE,
			   0x30 + tuple % 32);
	fd = open("/proc/sys/vm/compact_memory", O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		unmap_mapping(&mapping);
		return false;
	}
	for (attempt = 0; attempt < 4; attempt++)
		if (write(fd, "1", 1) != 1)
			passed = false;
	close(fd);
	for (tuple = 0; passed && tuple < COMPACT_TUPLES; tuple++) {
		unsigned char *base = mapping.base + tuple * NATIVE_PAGE_SIZE;

		passed = check_tuple(base, 0x30 + tuple % 32) &&
			 tuple_has_one_folio(base);
	}
	unmap_mapping(&mapping);
	return passed;
}

static bool near_capacity_swap_pressure(void)
{
	unsigned long free_swap = swap_free_bytes();
	unsigned long available_tuples = free_swap * 3 / 4 / NATIVE_PAGE_SIZE;
	unsigned int pressure_tuples = available_tuples < PRESSURE_TUPLES ?
		available_tuples : PRESSURE_TUPLES;
	size_t length = pressure_tuples * NATIVE_PAGE_SIZE;
	struct mapping mapping = map_tuples(length);
	unsigned long swapped = 0;
	unsigned int tuple;
	bool passed = true;
	int fd = -1;

	if (pressure_tuples < 1024 || mapping.base == MAP_FAILED)
		return false;
	for (tuple = 0; tuple < pressure_tuples; tuple++)
		fill_tuple(mapping.base + tuple * NATIVE_PAGE_SIZE,
			   0x20 + tuple % 32);
	passed = pageout_reaches_swap(mapping.base, length, length / 2) &&
		 ppps_smaps_bytes(mapping.base, length, "Swap", &swapped);
	fd = open("/proc/sys/vm/compact_memory", O_WRONLY | O_CLOEXEC);
	for (tuple = 0; passed && tuple < PRESSURE_CHURN; tuple++) {
		unsigned int index = (tuple * 7919U) % pressure_tuples;
		unsigned int slice = tuple % PPPS_SLICES;
		unsigned char seed = 0x20 + index % 32;
		unsigned char *base = mapping.base + index * NATIVE_PAGE_SIZE;

		passed = check_tuple(base, seed);
		if (!passed) {
			ksft_print_msg("near-capacity churn mismatch at iteration %u "
				       "index %u bytes=%02x/%02x/%02x/%02x\n",
				       tuple, index, base[0], base[PROCESS_PAGE_SIZE],
				       base[2 * PROCESS_PAGE_SIZE],
				       base[3 * PROCESS_PAGE_SIZE]);
			break;
		}
		passed = !madvise(base + slice * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				  MADV_DONTNEED);
		if (!passed)
			break;
		memset(base + slice * PROCESS_PAGE_SIZE, seed + slice,
		       PROCESS_PAGE_SIZE);
		if (!(tuple % 32) && madvise(base, NATIVE_PAGE_SIZE, MADV_PAGEOUT))
			passed = false;
		if (fd >= 0 && !(tuple % 256) && write(fd, "1", 1) != 1)
			passed = false;
	}
	if (fd >= 0)
		close(fd);
	for (tuple = 0; passed && tuple < pressure_tuples; tuple++) {
		unsigned char *base = mapping.base + tuple * NATIVE_PAGE_SIZE;

		passed = check_tuple(base, 0x20 + tuple % 32);
		if (!passed)
			ksft_print_msg("near-capacity final mismatch at index %u "
				       "bytes=%02x/%02x/%02x/%02x\n", tuple,
				       base[0], base[PROCESS_PAGE_SIZE],
				       base[2 * PROCESS_PAGE_SIZE],
				       base[3 * PROCESS_PAGE_SIZE]);
	}
	if (!passed)
		ksft_print_msg("near-capacity pressure failed at tuple %u "
			       "after swapping %lu bytes\n", tuple, swapped);
	unmap_mapping(&mapping);
	return passed;
}

/* The data check must follow a move of swap PTEs, not merely resident PTEs. */
static bool swapped_misaligned_mremap(void)
{
	struct mapping source = map_tuples(2 * NATIVE_PAGE_SIZE);
	struct mapping target = map_tuples(3 * NATIVE_PAGE_SIZE);
	unsigned char *moved;
	unsigned int slice;
	size_t byte;
	bool passed = false;
	int fd = -1;

	if (source.base == MAP_FAILED || target.base == MAP_FAILED)
		goto out;
	for (slice = 0; slice < 2 * PPPS_SLICES; slice++)
		memset(source.base + slice * PROCESS_PAGE_SIZE, 0x40 + slice,
		       PROCESS_PAGE_SIZE);
	if (!pageout_reaches_swap(source.base, 2 * NATIVE_PAGE_SIZE,
				 2 * NATIVE_PAGE_SIZE))
		goto out;
	fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		goto out;
	for (slice = 0; slice < 2 * PPPS_SLICES; slice++) {
		uint64_t entry;
		uintptr_t addr = (uintptr_t)source.base + slice * PROCESS_PAGE_SIZE;

		if (pread(fd, &entry, sizeof(entry),
			  (addr / PROCESS_PAGE_SIZE) * sizeof(entry)) != sizeof(entry) ||
		    (entry & (1ULL << 63)) || !(entry & (1ULL << 62)))
			goto out;
	}
	moved = mremap(source.base, 2 * NATIVE_PAGE_SIZE, 2 * NATIVE_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED,
		       target.base + PROCESS_PAGE_SIZE);
	if (moved == MAP_FAILED)
		goto out;
	passed = true;
	for (byte = 0; byte < 2 * NATIVE_PAGE_SIZE; byte++)
		if (moved[byte] != 0x40 + byte / PROCESS_PAGE_SIZE) {
			passed = false;
			break;
		}
out:
	if (fd >= 0)
		close(fd);
	unmap_mapping(&target);
	unmap_mapping(&source);
	return passed;
}

static int run_tests(void)
{
	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(17);
	ksft_test_result(swap_is_active(), "swap is active\n");
	ksft_test_result(full_pageout_swapin(),
			 "full tuple pageout and random-order swapin\n");
	ksft_test_result(first_swapin_is_write(),
			 "first swapin can be a write fault\n");
	ksft_test_result(partial_tuple_pageout(),
			 "partial tuple pageout, DONTNEED and hole fill\n");
	ksft_test_result(fork_swapped_tuple(false),
			 "forked swap PTEs COW back into one tuple folio\n");
	ksft_test_result(fork_swapped_tuple(true),
			 "fork after partial swapin preserves tuple identity\n");
	ksft_test_result(fork_before_pageout(),
			 "pageout of a pre-fork shared tuple preserves both processes\n");
	ksft_test_result(fork_after_hole_fill(),
			 "fork after hole-first swapin preserves tuple data\n");
	ksft_test_result(concurrent_swapin(),
			 "concurrent four-slice swapin preserves tuple identity\n");
	ksft_test_result(pageout_dontneed_churn(),
			 "pageout churn preserves data across tuple splits\n");
	ksft_test_result(compaction_preserves_tuples(),
			 "memory compaction preserves tuple data and identity\n");
	ksft_test_result(near_capacity_swap_pressure(),
			 "near-capacity swap churn preserves tuple data\n");
	ksft_test_result(private_file_cow_swapin(),
			 "private file COW swapin stays outside tuple accounting\n");
	ksft_test_result(read_swapin_write_pageout(),
			 "store after a read-fault swapin survives the next pageout\n");
	ksft_test_result(fork_after_swapin_dontneed(),
			 "fork after swapin and DONTNEED shares the swapcache folio\n");
	ksft_test_result(swapin_restores_whole_tuple(),
			 "swapin restores the whole tuple at once\n");
	ksft_test_result(swapped_misaligned_mremap(),
			 "swapped tuples preserve every byte across a 4K-offset mremap\n");
	ksft_finished();
}

static int run_selected_test(int selected)
{
	bool passed;
	const char *name;

	switch (selected) {
	case 2:
		passed = full_pageout_swapin();
		name = "full tuple pageout and random-order swapin";
		break;
	case 3:
		passed = first_swapin_is_write();
		name = "first swapin can be a write fault";
		break;
	case 4:
		passed = partial_tuple_pageout();
		name = "partial tuple pageout, DONTNEED and hole fill";
		break;
	case 5:
		passed = fork_swapped_tuple(false);
		name = "forked swap PTEs COW back into one tuple folio";
		break;
	case 6:
		passed = fork_swapped_tuple(true);
		name = "fork after partial swapin preserves tuple identity";
		break;
	case 7:
		passed = fork_before_pageout();
		name = "pageout of a pre-fork shared tuple preserves both processes";
		break;
	case 8:
		passed = fork_after_hole_fill();
		name = "fork after hole-first swapin preserves tuple data";
		break;
	case 9:
		passed = concurrent_swapin();
		name = "concurrent four-slice swapin preserves tuple identity";
		break;
	case 10:
		passed = pageout_dontneed_churn();
		name = "pageout churn preserves tuple data";
		break;
	case 11:
		passed = compaction_preserves_tuples();
		name = "memory compaction preserves tuple data and identity";
		break;
	case 12:
		passed = near_capacity_swap_pressure();
		name = "near-capacity swap churn preserves tuple data";
		break;
	case 13:
		passed = private_file_cow_swapin();
		name = "private file COW swapin stays outside tuple accounting";
		break;
	case 14:
		passed = read_swapin_write_pageout();
		name = "store after a read-fault swapin survives the next pageout";
		break;
	case 15:
		passed = fork_after_swapin_dontneed();
		name = "fork after swapin and DONTNEED shares the swapcache folio";
		break;
	case 16:
		passed = swapin_restores_whole_tuple();
		name = "swapin restores the whole tuple at once";
		break;
	case 17:
		passed = swapped_misaligned_mremap();
		name = "swapped tuples survive a 4K-offset mremap";
		break;
	default:
		ksft_exit_fail_msg("unsupported selected test: %d\n", selected);
	}
	ksft_print_header();
	ksft_set_plan(1);
	ksft_test_result(passed, "%s\n", name);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode && argc <= 2)
		exec_compat(argv[0], PPPS_RUN_FLAG, argc == 2 ? argv[1] : NULL,
			    NULL);
	if (mode && argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_tests();
	if (mode && argc == 3 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_selected_test(atoi(argv[2]));
	return EXIT_FAILURE;
}
