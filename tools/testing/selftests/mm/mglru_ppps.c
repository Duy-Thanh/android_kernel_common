// SPDX-License-Identifier: GPL-2.0
/*
 * A forced MGLRU page-table aging pass over a 4K compat process's anonymous
 * mapping, driven through debugfs lru_gen in a dedicated memory cgroup,
 * produces a new generation.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define TEST_PAGES 512
#define CGROUP_PATH "/sys/fs/cgroup/ppps-mglru"
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

	if (!write_text("/sys/fs/cgroup/cgroup.subtree_control", "+memory\n"))
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
			in_memcg = strstr(line, "ppps-mglru");
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

static int run_test(void)
{
	struct generation_stats before;
	struct generation_stats after;
	unsigned char *mapping;
	bool joined;
	bool have_before;
	bool aged;
	bool have_after;
	int i;

	ksft_print_header();
	ksft_set_plan(5);

	joined = join_test_cgroup();
	ksft_test_result(joined, "join a dedicated memory cgroup\n");
	if (!joined)
		ksft_exit_fail_msg("cgroup setup failed: %s\n", strerror(errno));

	mapping = mmap(NULL, TEST_PAGES * PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		       -1, 0);
	if (mapping != MAP_FAILED)
		madvise((void *)mapping, TEST_PAGES * PROCESS_PAGE_SIZE,
			MADV_NOHUGEPAGE);
	ksft_test_result(mapping != MAP_FAILED,
			 "allocate the MGLRU test mapping\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	for (i = 0; i < TEST_PAGES; i++)
		mapping[i * PROCESS_PAGE_SIZE] = (unsigned char)i;

	have_before = read_generation_stats(&before);
	ksft_test_result(have_before,
			 "find the test cgroup in the MGLRU histogram\n");
	if (!have_before)
		ksft_exit_fail_msg("cannot parse %s\n", LRU_GEN_PATH);

	aged = age_memcg(&before);
	ksft_test_result(aged, "force an MGLRU page-table aging pass\n");
	have_after = read_generation_stats(&after);
	ksft_test_result(have_after && after.max_gen > before.max_gen,
			 "MGLRU creates a new generation (%d -> %d)\n",
			 before.max_gen, have_after ? after.max_gen : -1);

	munmap((void *)mapping, TEST_PAGES * PROCESS_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
