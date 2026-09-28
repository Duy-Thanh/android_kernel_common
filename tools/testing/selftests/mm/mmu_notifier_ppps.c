// SPDX-License-Identifier: GPL-2.0
/*
 * An MGLRU page-table aging pass over a 4K compat process's anonymous
 * mapping invokes the fixture MMU notifier with ranges in whole process
 * pages that never exceed one native page (a packed anonymous tuple).
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"
#include "mmu_notifier_ppps.h"

#define TEST_PAGES 512
#define CGROUP_PATH "/sys/fs/cgroup/ppps-mmu-notifier"
#define LRU_GEN_PATH "/sys/kernel/debug/lru_gen"

struct generation_stats {
	unsigned long memcg_id;
	int node_id;
	int max_gen;
};

static bool write_text(const char *path, const char *text)
{
	ssize_t length = strlen(text);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	bool ok;

	if (fd < 0)
		return false;
	ok = write(fd, text, length) == length;
	close(fd);
	return ok;
}

static bool join_test_cgroup(void)
{
	char pid[32];

	if (!write_text("/sys/fs/cgroup/cgroup.subtree_control", "+memory\n") &&
	    errno != EBUSY)
		return false;
	if (mkdir(CGROUP_PATH, 0755) && errno != EEXIST)
		return false;
	snprintf(pid, sizeof(pid), "%d\n", getpid());
	return write_text(CGROUP_PATH "/cgroup.procs", pid);
}

static bool read_generation_stats(struct generation_stats *stats)
{
	char line[512];
	bool in_memcg = false;
	bool in_node = false;
	FILE *file;

	memset(stats, 0, sizeof(*stats));
	stats->max_gen = -1;
	stats->node_id = -1;
	file = fopen(LRU_GEN_PATH, "r");
	if (!file)
		return false;

	while (fgets(line, sizeof(line), file)) {
		unsigned long memcg_id;
		long age, anon, file_pages;
		int node, gen;

		if (sscanf(line, " memcg %lu", &memcg_id) == 1) {
			in_memcg = strstr(line, "ppps-mmu-notifier");
			in_node = false;
			if (in_memcg)
				stats->memcg_id = memcg_id;
			continue;
		}
		if (!in_memcg)
			continue;
		if (sscanf(line, " node %d", &node) == 1) {
			in_node = node == 0;
			if (in_node)
				stats->node_id = node;
			continue;
		}
		if (!in_node)
			continue;
		if (sscanf(line, " %d %ld %ld %ld", &gen, &age, &anon,
			   &file_pages) != 4)
			continue;
		if (gen > stats->max_gen)
			stats->max_gen = gen;
	}
	fclose(file);
	return stats->memcg_id && stats->node_id == 0 && stats->max_gen >= 0;
}

static bool age_memcg(const struct generation_stats *stats)
{
	char command[128];

	snprintf(command, sizeof(command), "+ %lu %d %d 1 1\n",
		 stats->memcg_id, stats->node_id, stats->max_gen);
	return write_text(LRU_GEN_PATH, command);
}

static void reclaim_memcg(void)
{
	write_text(CGROUP_PATH "/memory.reclaim", "1M\n");
}

static int run_test(void)
{
	struct mmu_notifier_ppps_range range;
	struct mmu_notifier_ppps_stats stats;
	struct generation_stats generations;
	unsigned char *mapping;
	size_t mapping_size = TEST_PAGES * PROCESS_PAGE_SIZE;
	bool joined;
	bool found;
	bool aged;
	char child_result = 0;
	int pipefd[2];
	pid_t child;
	int fd;
	int i;
	int attempt;

	printf("TAP version 13\n1..6\n");

	joined = join_test_cgroup();
	printf("%s 1 - join a dedicated memory cgroup\n",
	       joined ? "ok" : "not ok");
	if (!joined)
		return 1;

	fd = ppps_open_fixture_or_skip("/dev/mmu_notifier_ppps", O_RDWR);
	printf("%s 2 - register an MMU notifier for this process\n",
	       fd >= 0 ? "ok" : "not ok");

	mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping != MAP_FAILED)
		madvise(mapping, mapping_size, MADV_NOHUGEPAGE);
	printf("%s 3 - allocate the notifier test mapping\n",
	       mapping != MAP_FAILED ? "ok" : "not ok");
	if (mapping == MAP_FAILED)
		return 1;

	range.start = (uintptr_t)mapping;
	range.end = range.start + mapping_size;
	range.page_size = PROCESS_PAGE_SIZE;
	if (ioctl(fd, MMU_NOTIFIER_PPPS_SET_RANGE, &range)) {
		perror("MMU_NOTIFIER_PPPS_SET_RANGE");
		return 1;
	}

	for (i = 0; i < TEST_PAGES; i++)
		mapping[i * PROCESS_PAGE_SIZE]++;

	found = read_generation_stats(&generations);
	printf("%s 4 - find the test cgroup in the MGLRU histogram\n",
	       found ? "ok" : "not ok");
	if (!found)
		return 1;

	aged = false;
	memset(&stats, 0, sizeof(stats));
	if (pipe(pipefd)) {
		perror("pipe");
		return 1;
	}
	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}
	if (!child) {
		close(pipefd[0]);
		for (attempt = 0; attempt < 8; attempt++) {
			if (!age_memcg(&generations))
				break;
			child_result = 1;
			reclaim_memcg();
			if (ioctl(fd, MMU_NOTIFIER_PPPS_GET_STATS, &stats))
				break;
			if (stats.callbacks)
				break;
			if (!read_generation_stats(&generations))
				break;
		}
		if (write(pipefd[1], &child_result, sizeof(child_result)) !=
		    sizeof(child_result))
			child_result = 0;
		close(pipefd[1]);
		_exit(child_result ? 0 : 1);
	}
	close(pipefd[1]);
	aged = read(pipefd[0], &child_result, sizeof(child_result)) ==
		sizeof(child_result) && child_result;
	close(pipefd[0]);
	waitpid(child, NULL, 0);
	printf("%s 5 - force an MGLRU page-table aging pass\n",
	       aged ? "ok" : "not ok");
	if (!aged)
		return 1;

	if (ioctl(fd, MMU_NOTIFIER_PPPS_GET_STATS, &stats)) {
		perror("MMU_NOTIFIER_PPPS_GET_STATS");
		return 1;
	}
	printf("# total_callbacks=%llu callbacks=%llu bad_ranges=%llu",
	       (unsigned long long)stats.total_callbacks,
	       (unsigned long long)stats.callbacks,
	       (unsigned long long)stats.bad_ranges);
	printf(" max_span=%llu last=[%#llx,%#llx)\n",
	       (unsigned long long)stats.max_span,
	       (unsigned long long)stats.last_start,
	       (unsigned long long)stats.last_end);
	printf("%s 6 - MMU notifier ranges are process pages within a native page\n",
	       stats.callbacks && !stats.bad_ranges ? "ok" : "not ok");

	munmap(mapping, mapping_size);
	close(fd);
	return stats.callbacks && !stats.bad_ranges ? 0 : 1;
}

PPPS_COMPAT_MAIN(run_test)
