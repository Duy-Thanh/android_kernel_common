// SPDX-License-Identifier: GPL-2.0
/*
 * msync of a shmem mapping that a 4K compat process placed at a 4K file
 * offset hands vfs_fsync_range the byte range of that process page, as
 * observed through a kprobe.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define TRACEFS		"/sys/kernel/tracing/"

static bool write_control(const char *name, const char *value)
{
	char path[256];
	ssize_t length = strlen(value);
	int fd;

	snprintf(path, sizeof(path), TRACEFS "%s", name);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	if (write(fd, value, length) != length) {
		close(fd);
		return false;
	}
	close(fd);
	return true;
}

static bool install_probe(void)
{
	write_control("tracing_on", "0\n");
	write_control("kprobe_events", "-:ppps_msync\n");
	if (!write_control("kprobe_events",
			   "p:ppps_msync vfs_fsync_range start=%x1 end=%x2\n"))
		return false;
	if (!write_control("events/kprobes/ppps_msync/enable", "1\n"))
		return false;
	if (!write_control("trace", "\n"))
		return false;
	return write_control("tracing_on", "1\n");
}

static void remove_probe(void)
{
	write_control("tracing_on", "0\n");
	write_control("events/kprobes/ppps_msync/enable", "0\n");
	write_control("kprobe_events", "-:ppps_msync\n");
}

static bool read_fsync_range(unsigned long long *start,
			     unsigned long long *end)
{
	char line[1024];
	bool found = false;
	FILE *trace = fopen(TRACEFS "trace", "r");

	if (!trace)
		return false;
	while (fgets(line, sizeof(line), trace)) {
		char *range;

		if (!strstr(line, "ppps_msync:"))
			continue;
		range = strstr(line, "start=0x");
		if (range && sscanf(range, "start=0x%llx end=0x%llx",
				    start, end) == 2)
			found = true;
	}
	fclose(trace);
	return found;
}

static int run_test(void)
{
	unsigned long long fsync_start = 0;
	unsigned long long fsync_end = 0;
	unsigned char *mapping;
	bool range_ok;
	bool probe_ok;
	bool sync_ok;
	int memfd;

	ksft_print_header();
	ksft_set_plan(3);

	memfd = memfd_create("msync-offset-ppps", MFD_CLOEXEC);
	if (memfd < 0 || ftruncate(memfd, 4 * PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		       memfd, PROCESS_PAGE_SIZE);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	mapping[0] = 0x4d;

	probe_ok = install_probe();
	ksft_test_result(probe_ok, "trace the fsync range used by msync\n");
	if (!probe_ok)
		ksft_exit_skip("kprobe tracing is unavailable: %s\n",
			       strerror(errno));

	sync_ok = !msync(mapping, PROCESS_PAGE_SIZE, MS_SYNC);
	write_control("tracing_on", "0\n");
	ksft_test_result(sync_ok, "synchronize the offset shmem mapping\n");
	range_ok = read_fsync_range(&fsync_start, &fsync_end) &&
		   fsync_start == PROCESS_PAGE_SIZE &&
		   fsync_end == 2 * PROCESS_PAGE_SIZE - 1;
	ksft_test_result(range_ok, "msync uses the PPPS byte file offset\n");
	ksft_print_msg("vfs_fsync_range start=%llu end=%llu\n",
		       fsync_start, fsync_end);

	remove_probe();
	munmap(mapping, PROCESS_PAGE_SIZE);
	close(memfd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
