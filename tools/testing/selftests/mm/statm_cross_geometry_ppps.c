// SPDX-License-Identifier: GPL-2.0
/*
 * /proc/<pid>/statm and field 24 (rss) of /proc/<pid>/stat carry page
 * counts without a unit; readers multiply them by their own page size
 * (lmkd, ps, top, Process.getRss()).  A native 16K reader looking at a 4K
 * compat process, and a compat reader looking at a native process, must
 * therefore see counts in the reader's page size.  /proc/<pid>/status
 * reports the same quantities in kB and is the reference.
 *
 * Run as a native process: one case reads a compat target from native, the
 * other reads this native process from a compat reader.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TARGET_BYTES	(32UL << 20)

struct mem_view {
	unsigned long statm_size, statm_resident, stat_rss;
	long vmsize_kb, vmrss_kb;
};

static bool read_statm(pid_t pid, unsigned long *size, unsigned long *resident)
{
	char path[64];
	FILE *file;
	bool ok;

	snprintf(path, sizeof(path), "/proc/%d/statm", (int)pid);
	file = fopen(path, "re");
	if (!file)
		return false;
	ok = fscanf(file, "%lu %lu", size, resident) == 2;
	fclose(file);
	return ok;
}

/* Field 24 of /proc/<pid>/stat. */
static bool read_stat_rss(pid_t pid, unsigned long *rss)
{
	char path[64], buffer[2048], *p;
	ssize_t len;
	int fd, field;

	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (len <= 0)
		return false;
	buffer[len] = '\0';
	p = strrchr(buffer, ')');
	if (!p)
		return false;
	/* p + 2 is field 3 (state). */
	p += 2;
	for (field = 3; field < 24; field++) {
		p = strchr(p, ' ');
		if (!p)
			return false;
		p++;
	}
	return sscanf(p, "%lu", rss) == 1;
}

static bool read_view(pid_t pid, struct mem_view *view)
{
	view->vmsize_kb = ppps_status_kb(pid, "VmSize");
	view->vmrss_kb = ppps_status_kb(pid, "VmRSS");
	return read_statm(pid, &view->statm_size, &view->statm_resident) &&
	       read_stat_rss(pid, &view->stat_rss) &&
	       view->vmsize_kb > 0 && view->vmrss_kb > 0;
}

/* Within a factor of two: page rounding is fine, 4x is not. */
static bool close_enough(unsigned long long a, unsigned long long b)
{
	return a <= 2 * b && b <= 2 * a;
}

/* Check the target as seen through this process's page size. */
static int check_view(pid_t pid, const char *who)
{
	unsigned long page = getpagesize();
	struct mem_view view;
	int ret = 0;

	if (!read_view(pid, &view)) {
		ksft_print_msg("%s: cannot read /proc/%d\n", who, (int)pid);
		return KSFT_FAIL;
	}
	ksft_print_msg("%s: reader page %lu: statm size %lu resident %lu, stat rss %lu, status VmSize %ld kB VmRSS %ld kB\n",
		       who, page, view.statm_size, view.statm_resident,
		       view.stat_rss, view.vmsize_kb, view.vmrss_kb);
	if (!close_enough((unsigned long long)view.statm_resident * page,
			  (unsigned long long)view.vmrss_kb * 1024)) {
		ksft_print_msg("%s: statm resident * %lu = %llu bytes, VmRSS = %llu bytes\n",
			       who, page,
			       (unsigned long long)view.statm_resident * page,
			       (unsigned long long)view.vmrss_kb * 1024);
		ret = KSFT_FAIL;
	}
	if (!close_enough((unsigned long long)view.stat_rss * page,
			  (unsigned long long)view.vmrss_kb * 1024)) {
		ksft_print_msg("%s: stat rss * %lu = %llu bytes, VmRSS = %llu bytes\n",
			       who, page, (unsigned long long)view.stat_rss * page,
			       (unsigned long long)view.vmrss_kb * 1024);
		ret = KSFT_FAIL;
	}
	if (!close_enough((unsigned long long)view.statm_size * page,
			  (unsigned long long)view.vmsize_kb * 1024)) {
		ksft_print_msg("%s: statm size * %lu = %llu bytes, VmSize = %llu bytes\n",
			       who, page,
			       (unsigned long long)view.statm_size * page,
			       (unsigned long long)view.vmsize_kb * 1024);
		ret = KSFT_FAIL;
	}
	return ret;
}

static void *touch_memory(void)
{
	unsigned char *p = mmap(NULL, TARGET_BYTES, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED)
		return NULL;
	memset(p, 0x5a, TARGET_BYTES);
	return p;
}

/* --target <ready-fd> <release-fd>: a compat process holding memory. */
static int run_target(int argc, char **argv)
{
	int ready, release;
	char byte = 'r';

	if (argc != 4)
		return KSFT_FAIL;
	ready = atoi(argv[2]);
	release = atoi(argv[3]);
	if (!ppps_is_compat_process() || !touch_memory())
		byte = 'x';
	if (write(ready, &byte, 1) != 1)
		return KSFT_FAIL;
	/* Wait for EOF. */
	while (read(release, &byte, 1) > 0)
		;
	return KSFT_PASS;
}

/* --reader <pid>: a compat process reading a native target. */
static int run_reader(int argc, char **argv)
{
	if (argc != 3 || !ppps_is_compat_process())
		return KSFT_SKIP;
	return check_view(atoi(argv[2]), "compat reader, native target");
}

static int native_reads_compat(void)
{
	int ready[2], release[2], status, ret;
	char fd_ready[16], fd_release[16], byte = 0;
	pid_t pid;

	if (pipe(ready) || pipe(release))
		return KSFT_FAIL;
	pid = fork();
	if (pid < 0)
		return KSFT_FAIL;
	if (!pid) {
		close(ready[0]);
		close(release[1]);
		snprintf(fd_ready, sizeof(fd_ready), "%d", ready[1]);
		snprintf(fd_release, sizeof(fd_release), "%d", release[0]);
		ppps_execl(true, NULL, "--target", fd_ready, fd_release, NULL);
		_exit(KSFT_SKIP);
	}
	close(ready[1]);
	close(release[0]);
	if (read(ready[0], &byte, 1) != 1 || byte != 'r') {
		ksft_print_msg("compat target did not start (%c)\n", byte);
		ret = KSFT_SKIP;
	} else {
		ret = check_view(pid, "native reader, compat target");
	}
	close(release[1]);
	close(ready[0]);
	waitpid(pid, &status, 0);
	return ret;
}

static int compat_reads_native(void)
{
	char pid_arg[16];
	int status;
	pid_t pid;

	if (!touch_memory())
		return KSFT_FAIL;
	snprintf(pid_arg, sizeof(pid_arg), "%d", (int)getpid());
	pid = fork();
	if (pid < 0)
		return KSFT_FAIL;
	if (!pid) {
		ppps_execl(true, NULL, "--reader", pid_arg, NULL);
		_exit(KSFT_SKIP);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return KSFT_FAIL;
	return WEXITSTATUS(status);
}

static void report(int ret, const char *what)
{
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("%s\n", what);
	else
		ksft_test_result(ret == KSFT_PASS, "%s\n", what);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (mode && !strcmp(mode, "--target"))
		return run_target(argc, argv);
	if (mode && !strcmp(mode, "--reader"))
		return run_reader(argc, argv);
	if (mode || argc != 1)
		return KSFT_FAIL;
	if (getpagesize() != (int)NATIVE_PAGE_SIZE) {
		if (ppps_is_compat_process())
			exec_native(NULL, NULL);
		ksft_print_header();
		ksft_exit_skip("requires a native 16K PPPS kernel\n");
	}

	ksft_print_header();
	ksft_set_plan(3);
	fflush(stdout);
	report(check_view(getpid(), "native reader, native target"),
	       "native reader sees its own statm/stat in 16K pages");
	report(native_reads_compat(),
	       "native reader sees compat statm/stat in 16K pages");
	report(compat_reads_native(),
	       "compat reader sees native statm/stat in 4K pages");
	ksft_finished();
}
