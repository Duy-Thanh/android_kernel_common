// SPDX-License-Identifier: GPL-2.0
/*
 * On a PPPS kernel /proc/<pid>/{smaps,smaps_rollup,pagemap} of a native 16K
 * process must behave as on a native kernel: a file page that only native
 * processes map is accounted from its mapcount, without locking the folio
 * and walking its reverse mappings.  Walking the rmap for every shared file
 * page makes procfs readers contend on i_mmap_rwsem and other processes'
 * page-table locks and sleep on locked folios.
 *
 * rmap_walk() calls made by the reading task are counted with a kprobe
 * event in a private tracefs instance.  A native reader reading a range
 * that maps a file shared only with another native process must not walk
 * the rmap; for smaps and smaps_rollup, which cover every VMA, the count is
 * compared with and without that file mapped.  A 4K compat process reading
 * its own pagemap of a file that is also mapped by a native process is the
 * control proving the probe sees the walks P3S does need.
 *
 * Root and kprobe events are required.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TRACEFS		"/sys/kernel/tracing"
#define FILE_PAGES	64
#define FILE_SIZE	(FILE_PAGES * NATIVE_PAGE_SIZE)
/* Unrelated rmap walks (reclaim, ...) the reader may run into. */
#define NOISE		4

static char event[64], instance[PATH_MAX];

static bool write_path(const char *path, const char *value, bool append)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC | (append ? O_APPEND : O_TRUNC));
	ssize_t len = strlen(value), ret;

	if (fd < 0)
		return false;
	ret = write(fd, value, len);
	close(fd);
	return ret == len;
}

static bool instance_write(const char *file, const char *value)
{
	char path[PATH_MAX + 128];

	snprintf(path, sizeof(path), "%s/%s", instance, file);
	return write_path(path, value, false);
}

static void cleanup(void)
{
	char cmd[96];

	if (instance[0]) {
		char path[PATH_MAX + 128];

		snprintf(path, sizeof(path), "events/kprobes/%s/enable", event);
		instance_write(path, "0");
		rmdir(instance);
		instance[0] = '\0';
	}
	if (event[0]) {
		snprintf(cmd, sizeof(cmd), "-:%s\n", event);
		write_path(TRACEFS "/kprobe_events", cmd, true);
		event[0] = '\0';
	}
}

static bool setup_probe(void)
{
	char cmd[128], path[PATH_MAX + 128];

	snprintf(event, sizeof(event), "ppps_rmap_%d", (int)getpid());
	snprintf(cmd, sizeof(cmd), "p:%s rmap_walk\n", event);
	if (!write_path(TRACEFS "/kprobe_events", cmd, true)) {
		event[0] = '\0';
		return false;
	}
	snprintf(instance, sizeof(instance), TRACEFS "/instances/%s", event);
	if (mkdir(instance, 0700)) {
		instance[0] = '\0';
		return false;
	}
	snprintf(path, sizeof(path), "events/kprobes/%s/enable", event);
	return instance_write(path, "1");
}

/* Start counting rmap_walk() calls made by @pid. */
static bool arm(pid_t pid)
{
	char path[PATH_MAX + 128], filter[64];

	snprintf(path, sizeof(path), "events/kprobes/%s/filter", event);
	snprintf(filter, sizeof(filter), "common_pid == %d", (int)pid);
	return instance_write(path, filter) && instance_write("trace", "");
}

static long count_hits(void)
{
	char path[PATH_MAX + 32], line[512], tag[80];
	long hits = 0;
	FILE *trace;

	snprintf(path, sizeof(path), "%s/trace", instance);
	snprintf(tag, sizeof(tag), " %s:", event);
	trace = fopen(path, "re");
	if (!trace)
		return -1;
	while (fgets(line, sizeof(line), trace))
		if (line[0] != '#' && strstr(line, tag))
			hits++;
	fclose(trace);
	return hits;
}

static void slurp(const char *path)
{
	char buffer[65536];
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return;
	while (read(fd, buffer, sizeof(buffer)) > 0)
		;
	close(fd);
}

static void read_pagemap(const unsigned char *start, size_t len)
{
	size_t entries = len / getpagesize();
	uint64_t *buffer = calloc(entries, sizeof(*buffer));
	int fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);

	if (fd >= 0 && buffer)
		pread(fd, buffer, entries * sizeof(*buffer),
		      (off_t)((uintptr_t)start / getpagesize()) * sizeof(*buffer));
	if (fd >= 0)
		close(fd);
	free(buffer);
}

static int make_file(const char *name)
{
	static unsigned char page[NATIVE_PAGE_SIZE];
	unsigned int i;
	int fd = syscall(__NR_memfd_create, name, 0);

	if (fd < 0)
		ksft_exit_fail_msg("memfd_create: %s\n", strerror(errno));
	for (i = 0; i < FILE_PAGES; i++) {
		memset(page, i + 1, sizeof(page));
		if (!pwrite_full(fd, page, sizeof(page), (off_t)i * sizeof(page)))
			ksft_exit_fail_msg("write: %s\n", strerror(errno));
	}
	return fd;
}

static unsigned char *map_file(int fd)
{
	unsigned char *p = mmap(NULL, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	unsigned long off, sum = 0;

	if (p == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	for (off = 0; off < FILE_SIZE; off += getpagesize())
		sum += p[off];
	if (!sum)
		ksft_exit_fail_msg("file is empty\n");
	return p;
}

/*
 * A helper that maps @fd and touches every page, then waits for EOF on
 * @release.  @compat selects its geometry; a compat helper also reads its
 * own pagemap of the file when told to over @go and reports back on @done.
 */
struct helper {
	pid_t pid;
	int release, go, done;
};

static int helper_main(int argc, char **argv)
{
	int fd, ready, release, go, done;
	unsigned char *p;
	char byte = 'r';

	if (argc != 7)
		return KSFT_FAIL;
	fd = atoi(argv[2]);
	ready = atoi(argv[3]);
	release = atoi(argv[4]);
	go = atoi(argv[5]);
	done = atoi(argv[6]);
	p = map_file(fd);
	if (write(ready, &byte, 1) != 1)
		return KSFT_FAIL;
	if (read(go, &byte, 1) == 1) {
		read_pagemap(p, FILE_SIZE);
		write(done, &byte, 1);
	}
	while (read(release, &byte, 1) > 0)
		;
	return KSFT_PASS;
}

static bool start_helper(struct helper *h, int fd, bool compat)
{
	int ready[2], release[2], go[2], done[2];
	char args[5][16], byte;

	if (pipe(ready) || pipe(release) || pipe(go) || pipe(done))
		return false;
	h->pid = fork();
	if (h->pid < 0)
		return false;
	if (!h->pid) {
		close(ready[0]);
		close(release[1]);
		close(go[1]);
		close(done[0]);
		snprintf(args[0], 16, "%d", fd);
		snprintf(args[1], 16, "%d", ready[1]);
		snprintf(args[2], 16, "%d", release[0]);
		snprintf(args[3], 16, "%d", go[0]);
		snprintf(args[4], 16, "%d", done[1]);
		ppps_execl(compat, NULL, "--helper", args[0], args[1], args[2],
			   args[3], args[4], NULL);
		_exit(KSFT_SKIP);
	}
	close(ready[1]);
	close(release[0]);
	close(go[0]);
	close(done[1]);
	h->release = release[1];
	h->go = go[1];
	h->done = done[0];
	return read(ready[0], &byte, 1) == 1 && !close(ready[0]);
}

static void stop_helper(struct helper *h)
{
	int status;

	close(h->go);
	close(h->release);
	close(h->done);
	waitpid(h->pid, &status, 0);
}

static long measure(void (*reader)(void *), void *arg)
{
	if (!arm(getpid()))
		return -1;
	reader(arg);
	return count_hits();
}

static void read_smaps(void *arg)
{
	slurp("/proc/self/smaps");
}

static void read_rollup(void *arg)
{
	slurp("/proc/self/smaps_rollup");
}

static void read_own_pagemap(void *arg)
{
	read_pagemap(arg, FILE_SIZE);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	struct helper native_helper, compat_helper;
	long with, without, hits;
	unsigned char *native_map;
	int native_fd, compat_fd;
	char byte;

	if (mode && !strcmp(mode, "--helper"))
		return helper_main(argc, argv);
	ksft_print_header();
	if (getpagesize() != (int)NATIVE_PAGE_SIZE) {
		if (ppps_is_compat_process())
			exec_native(NULL, NULL);
		ksft_exit_skip("requires a native 16K PPPS kernel\n");
	}
	if (geteuid())
		ksft_exit_skip("root is required for kprobe events\n");
	atexit(cleanup);
	if (!setup_probe())
		ksft_exit_skip("cannot set up a kprobe on rmap_walk: %s\n",
			       strerror(errno));
	ksft_set_plan(4);

	/* Control: a compat reader of a file a native process maps too. */
	compat_fd = make_file("ppps_rmap_compat");
	map_file(compat_fd);
	if (!start_helper(&compat_helper, compat_fd, true))
		ksft_exit_fail_msg("compat helper failed to start\n");
	if (!arm(compat_helper.pid) || write(compat_helper.go, "g", 1) != 1 ||
	    read(compat_helper.done, &byte, 1) != 1)
		ksft_exit_fail_msg("compat helper did not read its pagemap\n");
	hits = count_hits();
	stop_helper(&compat_helper);
	ksft_print_msg("compat reader, pagemap of a mixed-geometry file: %ld rmap walks\n",
		       hits);
	if (hits <= 0)
		ksft_exit_skip("the kprobe does not see P3S rmap walks (%ld)\n",
			       hits);
	ksft_test_result_pass("probe sees P3S rmap walks of a compat reader\n");

	/* A file mapped by this process and one other native process. */
	native_fd = make_file("ppps_rmap_native");
	native_map = map_file(native_fd);
	if (!start_helper(&native_helper, native_fd, false))
		ksft_exit_fail_msg("native helper failed to start\n");

	hits = measure(read_own_pagemap, native_map);
	ksft_print_msg("native pagemap of a native-only shared file: %ld rmap walks\n",
		       hits);
	ksft_test_result(hits >= 0 && hits <= NOISE,
			 "native pagemap does not walk the rmap of native-only file pages\n");

	with = measure(read_rollup, NULL);
	munmap(native_map, FILE_SIZE);
	without = measure(read_rollup, NULL);
	ksft_print_msg("native smaps_rollup: %ld rmap walks with the file, %ld without\n",
		       with, without);
	ksft_test_result(with >= 0 && without >= 0 && with - without <= NOISE,
			 "native smaps_rollup does not walk the rmap of native-only file pages\n");

	native_map = map_file(native_fd);
	with = measure(read_smaps, NULL);
	munmap(native_map, FILE_SIZE);
	without = measure(read_smaps, NULL);
	ksft_print_msg("native smaps: %ld rmap walks with the file, %ld without\n",
		       with, without);
	ksft_test_result(with >= 0 && without >= 0 && with - without <= NOISE,
			 "native smaps does not walk the rmap of native-only file pages\n");

	stop_helper(&native_helper);
	cleanup();
	ksft_finished();
}
