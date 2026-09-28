// SPDX-License-Identifier: GPL-2.0
/*
 * Faulting 64 MiB of anonymous memory in a 4K compat process raises its
 * VmRSS and memcg "anon" charge by that amount, and its oom_score scales
 * with the anonymous physical memory charged rather than a page count.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAP_SIZE (64UL * 1024 * 1024)

static long read_named_kb(const char *path, const char *name)
{
	char line[256];
	FILE *file;
	long value = -1;

	file = fopen(path, "re");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (!strncmp(line, name, strlen(name)) &&
		    sscanf(line, "%*[^:]: %ld kB", &value) == 1)
			break;
		value = -1;
	}
	fclose(file);
	return value;
}

static long read_oom_score(void)
{
	FILE *file;
	long score = -1;

	file = fopen("/proc/self/oom_score", "re");
	if (!file)
		return -1;
	if (fscanf(file, "%ld", &score) != 1)
		score = -1;
	fclose(file);
	return score;
}

static bool current_cgroup_file(char *path, size_t size, const char *file)
{
	char line[PATH_MAX];
	char *newline;
	FILE *cgroup;
	int length;

	cgroup = fopen("/proc/self/cgroup", "re");
	if (!cgroup)
		return false;
	while (fgets(line, sizeof(line), cgroup)) {
		if (strncmp(line, "0::", 3))
			continue;
		newline = strchr(line + 3, '\n');
		if (newline)
			*newline = '\0';
		length = snprintf(path, size, "/sys/fs/cgroup%s/%s", line + 3,
				  file);
		if (length < 0 || (size_t)length >= size) {
			fclose(cgroup);
			return false;
		}
		fclose(cgroup);
		return true;
	}
	fclose(cgroup);
	return false;
}

static long read_memcg_anon_kb(void)
{
	char path[PATH_MAX];
	char name[64];
	unsigned long bytes;
	FILE *stat;

	if (!current_cgroup_file(path, sizeof(path), "memory.stat"))
		return -1;
	stat = fopen(path, "re");
	if (!stat)
		return -1;
	while (fscanf(stat, "%63s %lu", name, &bytes) == 2) {
		if (!strcmp(name, "anon")) {
			fclose(stat);
			return bytes / 1024;
		}
	}
	fclose(stat);
	return -1;
}

static int run_test(void)
{
	long anon_before, anon_after, anon_delta;
	long rss_before, rss_after, rss_delta;
	long score_before, score_after, score_delta;
	long memtotal, swaptotal, expected_delta, tolerance;
	unsigned char *mapping;
	bool score_matches;
	size_t offset;

	ksft_print_header();
	ksft_set_plan(4);

	/* Warm stdio and cgroup rstat paths before taking the baseline. */
	read_memcg_anon_kb();
	read_oom_score();
	read_named_kb("/proc/self/status", "VmRSS");
	anon_before = read_memcg_anon_kb();
	rss_before = read_named_kb("/proc/self/status", "VmRSS");
	score_before = read_oom_score();
	memtotal = read_named_kb("/proc/meminfo", "MemTotal");
	swaptotal = read_named_kb("/proc/meminfo", "SwapTotal");
	if (anon_before < 0 || rss_before < 0 || score_before < 0 ||
	    memtotal <= 0 || swaptotal < 0)
		ksft_exit_skip("memory cgroup v2 accounting is unavailable\n");

	mapping = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping != MAP_FAILED)
		madvise(mapping, MAP_SIZE, MADV_NOHUGEPAGE);
	ksft_test_result(mapping != MAP_FAILED,
			 "map anonymous process pages\n");
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	for (offset = 0; offset < MAP_SIZE; offset += PROCESS_PAGE_SIZE)
		mapping[offset] = (unsigned char)offset;

	anon_after = read_memcg_anon_kb();
	rss_after = read_named_kb("/proc/self/status", "VmRSS");
	score_after = read_oom_score();
	anon_delta = anon_after - anon_before;
	rss_delta = rss_after - rss_before;
	score_delta = score_after - score_before;
	ksft_test_result(rss_delta >= 60 * 1024 &&
			labs(rss_delta - anon_delta) <=
			(anon_delta / 8 > 8 * 1024 ? anon_delta / 8 : 8 * 1024),
			 "report physical anonymous RSS charged to the cgroup (%ld kB)\n", rss_delta);
	ksft_test_result(anon_delta >= 60 * 1024,
			 "charge anonymous physical memory (%ld kB)\n",
			 anon_delta);

	expected_delta = anon_delta * 2000 / (3 * (memtotal + swaptotal));
	tolerance = expected_delta / 4;
	if (tolerance < 8)
		tolerance = 8;
	score_matches = score_delta > 0 &&
		labs(score_delta - expected_delta) <= tolerance;
	ksft_print_msg("mem=%ldkB swap=%ldkB rss=%ldkB anon=%ldkB score=%ld exp=%ld tol=%ld\n",
		       memtotal, swaptotal, rss_delta, anon_delta, score_delta,
		       expected_delta, tolerance);
	ksft_test_result(score_matches,
			 "scale oom_score by anonymous physical memory\n");

	munmap(mapping, MAP_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
