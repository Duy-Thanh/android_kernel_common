// SPDX-License-Identifier: GPL-2.0
/*
 * Committed_AS charges and uncharges an anonymous mapping and its mremap
 * expansion in 4K process-page units for a compat process, and strict
 * overcommit enforces its byte limit against such mappings.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <unistd.h>

#include "kselftest_ppps.h"

#define STRICT_MAP_SIZE (4UL * 1024 * 1024)
/*
 * Strict overcommit is enforced against a per-CPU approximation of
 * Committed_AS, so the limit is set well above the admitted mapping and the
 * rejected one overshoots it by far more than that slack.
 */
#define STRICT_MARGIN (64UL * 1024 * 1024)
#define STRICT_OVERSIZE (4UL * STRICT_MARGIN)
#define CHARGE_ATTEMPTS 5

static void write_sysctl(const char *path, unsigned long value)
{
	char buffer[32];
	ssize_t length;
	int fd;

	length = snprintf(buffer, sizeof(buffer), "%lu\n", value);
	if (length <= 0 || (size_t)length >= sizeof(buffer))
		ksft_exit_fail_msg("format %s value failed\n", path);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		ksft_exit_fail_msg("open %s failed: %s\n", path,
				   strerror(errno));
	if (write(fd, buffer, length) != length)
		ksft_exit_fail_msg("write %s failed: %s\n", path,
				   strerror(errno));
	if (close(fd))
		ksft_exit_fail_msg("close %s failed: %s\n", path,
				   strerror(errno));
}

static unsigned long meminfo_kb(const char *field, bool required)
{
	unsigned long value = 0;
	size_t field_len = strlen(field);
	char *line = NULL;
	size_t capacity = 0;
	FILE *meminfo;

	meminfo = fopen("/proc/meminfo", "re");
	if (!meminfo)
		ksft_exit_fail_msg("open /proc/meminfo failed: %s\n",
				   strerror(errno));
	while (getline(&line, &capacity, meminfo) >= 0) {
		if (!strncmp(line, field, field_len) && line[field_len] == ':' &&
		    sscanf(line + field_len + 1, " %lu kB", &value) == 1)
			break;
	}
	free(line);
	fclose(meminfo);
	if (!value && required)
		ksft_exit_fail_msg("read %s failed\n", field);
	return value;
}

static unsigned long committed_kb(void)
{
	return meminfo_kb("Committed_AS", true);
}

/*
 * Committed_AS is system wide; a reading that is still changing when read
 * twice is being moved by another process, so wait for it to settle.
 */
static unsigned long stable_committed_kb(void)
{
	unsigned long value = committed_kb();
	int i;

	for (i = 0; i < 100; i++) {
		unsigned long again = committed_kb();

		if (again == value)
			return value;
		value = again;
		usleep(1000);
	}
	return value;
}

struct charges {
	unsigned long before;
	unsigned long after_map;
	unsigned long after_expand;
	unsigned long after_unmap;
	long initial;
	long expansion;
	long uncharge;
};

/*
 * Map, expand and unmap one process page, recording the Committed_AS
 * deltas.  Another process may commit memory in between, so the caller
 * retries when the deltas do not match; the syscalls themselves must never
 * fail.
 */
static void charge_sequence(struct charges *c)
{
	void *mapping;
	void *expanded;

	c->before = stable_committed_kb();
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	c->after_map = stable_committed_kb();
	expanded = mremap(mapping, PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
			  MREMAP_MAYMOVE);
	if (expanded == MAP_FAILED)
		ksft_exit_fail_msg("mremap failed: %s\n", strerror(errno));
	c->after_expand = stable_committed_kb();
	if (munmap(expanded, 2 * PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("munmap failed: %s\n", strerror(errno));
	c->after_unmap = stable_committed_kb();
	c->initial = c->after_map - c->before;
	c->expansion = c->after_expand - c->after_map;
	c->uncharge = c->after_expand - c->after_unmap;
}

static bool charges_match(const struct charges *c)
{
	return c->initial == PROCESS_PAGE_SIZE / 1024 &&
	       c->expansion == PROCESS_PAGE_SIZE / 1024 &&
	       c->uncharge == 2 * PROCESS_PAGE_SIZE / 1024;
}

static int run_test(void)
{
	struct charges c;
	unsigned long before;
	unsigned long swap_kb;
	unsigned long limit_kb;
	unsigned long oversized_len;
	void *mapping;
	void *expanded;
	int strict_errno;
	int attempt;

	ksft_print_header();
	ksft_set_plan(6);

	/* Warm up: the first /proc/meminfo read grows the heap. */
	committed_kb();
	for (attempt = 1; attempt <= CHARGE_ATTEMPTS; attempt++) {
		charge_sequence(&c);
		if (charges_match(&c))
			break;
		ksft_print_msg("attempt %d disturbed by other commits\n",
			       attempt);
	}
	ksft_print_msg("Committed_AS before=%lu mapped=%lu expanded=%lu "
		       "unmapped=%lu kB; initial=%ld expansion=%ld uncharge=%ld kB\n",
		       c.before, c.after_map, c.after_expand, c.after_unmap,
		       c.initial, c.expansion, c.uncharge);
	ksft_test_result(c.initial == PROCESS_PAGE_SIZE / 1024,
			 "charge exactly one 4K process page\n");
	ksft_test_result(c.expansion == PROCESS_PAGE_SIZE / 1024,
			 "charge exactly one 4K mremap expansion page\n");
	ksft_test_result(c.uncharge == 2 * PROCESS_PAGE_SIZE / 1024,
			 "uncharge both 4K process pages\n");

	write_sysctl("/proc/sys/vm/user_reserve_kbytes", 0);
	write_sysctl("/proc/sys/vm/admin_reserve_kbytes", 0);
	before = stable_committed_kb();
	/* The commit limit is overcommit_kbytes plus the total swap. */
	swap_kb = meminfo_kb("SwapTotal", false);
	limit_kb = before + (STRICT_MAP_SIZE + STRICT_MARGIN) / 1024;
	/* Zero disables overcommit_kbytes and re-enables the ratio policy. */
	limit_kb = limit_kb > swap_kb ? limit_kb - swap_kb : 1;
	write_sysctl("/proc/sys/vm/overcommit_kbytes", limit_kb);
	write_sysctl("/proc/sys/vm/overcommit_memory", 2);
	mapping = mmap(NULL, STRICT_MAP_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "strict overcommit admits one 4M mapping\n");
	errno = 0;
	/* Swap alone may exceed the desired limit. Exceed the actual limit,
	 * rather than assuming the fixed-size request consumes all headroom.
	 */
	limit_kb = meminfo_kb("CommitLimit", false);
	before = stable_committed_kb();
	oversized_len = STRICT_OVERSIZE;
	if (limit_kb > before)
		oversized_len += (limit_kb - before) * 1024;
	expanded = mmap(NULL, oversized_len, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	strict_errno = errno;
	ksft_test_result(expanded == MAP_FAILED && strict_errno == ENOMEM,
			 "strict overcommit rejects a mapping past the byte limit\n");
	ksft_test_result(mapping != MAP_FAILED && munmap(mapping, STRICT_MAP_SIZE) == 0,
			 "unmap the admitted mapping\n");
	if (expanded != MAP_FAILED)
		munmap(expanded, oversized_len);
	write_sysctl("/proc/sys/vm/overcommit_memory", 0);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
