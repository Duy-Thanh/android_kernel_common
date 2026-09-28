// SPDX-License-Identifier: GPL-2.0
/*
 * Trigger MGLRU aging with page-table walks enabled for every memcg/node
 * target listed in /sys/kernel/debug/lru_gen and check that each aging
 * request is accepted.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kselftest.h"

#define LRU_GEN_ENABLED_PATH "/sys/kernel/mm/lru_gen/enabled"
#define LRU_GEN_PATH "/sys/kernel/debug/lru_gen"
#define MAX_TARGETS 4096

struct aging_target {
	unsigned long memcg_id;
	int node_id;
	int max_gen;
};

static bool mglru_walks_enabled(void)
{
	unsigned int enabled;
	FILE *file;

	file = fopen(LRU_GEN_ENABLED_PATH, "r");
	if (!file)
		return false;
	if (fscanf(file, "%x", &enabled) != 1)
		enabled = 0;
	fclose(file);
	return enabled & 0x2;
}

static bool append_target(struct aging_target *targets, size_t *count,
			  unsigned long memcg_id, int node_id, int max_gen)
{
	if (!memcg_id || node_id < 0 || max_gen < 0)
		return true;
	if (*count == MAX_TARGETS)
		return false;
	targets[*count].memcg_id = memcg_id;
	targets[*count].node_id = node_id;
	targets[*count].max_gen = max_gen;
	(*count)++;
	return true;
}

static bool read_targets(struct aging_target *targets, size_t *count)
{
	unsigned long memcg_id = 0;
	int node_id = -1;
	int max_gen = -1;
	char line[512];
	FILE *file;

	*count = 0;
	file = fopen(LRU_GEN_PATH, "r");
	if (!file)
		return false;

	while (fgets(line, sizeof(line), file)) {
		unsigned long next_memcg;
		long age, anon, file_pages;
		int next_node, gen;

		if (sscanf(line, " memcg %lu", &next_memcg) == 1) {
			if (!append_target(targets, count, memcg_id, node_id,
					   max_gen))
				goto too_many;
			memcg_id = next_memcg;
			node_id = -1;
			max_gen = -1;
			continue;
		}
		if (sscanf(line, " node %d", &next_node) == 1) {
			if (!append_target(targets, count, memcg_id, node_id,
					   max_gen))
				goto too_many;
			node_id = next_node;
			max_gen = -1;
			continue;
		}
		if (sscanf(line, " %d %ld %ld %ld", &gen, &age, &anon,
			   &file_pages) == 4 && gen > max_gen)
			max_gen = gen;
	}
	if (!append_target(targets, count, memcg_id, node_id, max_gen))
		goto too_many;
	fclose(file);
	return true;

too_many:
	fclose(file);
	errno = E2BIG;
	return false;
}

static bool age_target(const struct aging_target *target)
{
	char command[128];
	ssize_t written;
	int length;
	int fd;

	length = snprintf(command, sizeof(command), "+ %lu %d %d 1 1\n",
			  target->memcg_id, target->node_id,
			  target->max_gen);
	fd = open(LRU_GEN_PATH, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	written = write(fd, command, length);
	close(fd);
	if (written >= 0 && written != length)
		errno = EIO;
	return written == length;
}

int main(void)
{
	struct aging_target targets[MAX_TARGETS];
	size_t aged = 0;
	size_t count;
	size_t i;
	bool enabled;
	bool parsed;

	ksft_print_header();
	ksft_set_plan(3);

	enabled = mglru_walks_enabled();
	if (!enabled)
		ksft_exit_skip("MGLRU page-table walks are disabled\n");
	ksft_test_result_pass("MGLRU page-table walks are enabled\n");

	parsed = read_targets(targets, &count);
	ksft_test_result(parsed && count, "find MGLRU memcg aging targets\n");
	if (!parsed || !count)
		ksft_exit_fail_msg("cannot parse %s: %s\n", LRU_GEN_PATH,
				   strerror(errno));

	for (i = 0; i < count; i++) {
		if (age_target(&targets[i]))
			aged++;
		else if (errno != ENODEV && errno != ENOENT)
			ksft_print_msg("cannot age memcg %lu node %d: %s\n",
				       targets[i].memcg_id,
				       targets[i].node_id, strerror(errno));
	}
	ksft_test_result(aged, "age %zu/%zu MGLRU memcg/node targets\n",
			 aged, count);
	ksft_finished();
}
