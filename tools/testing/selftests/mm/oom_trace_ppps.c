// SPDX-License-Identifier: GPL-2.0
/*
 * The oom:mark_victim trace record for a forced-OOM 4K compat victim reports
 * total-vm, file RSS and shmem RSS in process-page bytes matching its
 * /proc status, and anonymous RSS in native physical bytes.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define CHILD_MAP_SIZE (32UL * 1024 * 1024)
#define TRACE_ENABLE "/sys/kernel/tracing/events/oom/mark_victim/enable"
#define TRACE_FILE "/sys/kernel/tracing/trace"
#define TRACING_ON "/sys/kernel/tracing/tracing_on"

struct victim_stats {
	long total_vm;
	long anon_rss;
	long file_rss;
	long shmem_rss;
};

struct victim_ready {
	uintptr_t mapping;
};

static int write_text(const char *path, const char *text)
{
	ssize_t length = strlen(text);
	ssize_t written;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	written = write(fd, text, length);
	close(fd);
	return written == length ? 0 : -1;
}

static int clear_trace(void)
{
	int fd = open(TRACE_FILE, O_WRONLY | O_TRUNC | O_CLOEXEC);

	if (fd < 0)
		return -1;
	close(fd);
	return 0;
}

static char *read_trace(void)
{
	size_t capacity = 64 * 1024;
	size_t length = 0;
	char *buffer;
	int fd;

	fd = open(TRACE_FILE, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	buffer = malloc(capacity);
	if (!buffer) {
		close(fd);
		return NULL;
	}
	for (;;) {
		ssize_t bytes;

		if (length + 1 == capacity) {
			char *larger;

			capacity *= 2;
			larger = realloc(buffer, capacity);
			if (!larger) {
				free(buffer);
				close(fd);
				return NULL;
			}
			buffer = larger;
		}
		bytes = read(fd, buffer + length, capacity - length - 1);
		if (bytes < 0) {
			free(buffer);
			close(fd);
			return NULL;
		}
		if (!bytes)
			break;
		length += bytes;
	}
	close(fd);
	buffer[length] = '\0';
	return buffer;
}

static bool find_victim_stats(const char *trace, pid_t target,
			      struct victim_stats *stats)
{
	const char *record = trace;

	while ((record = strstr(record, "pid="))) {
		const char *memory;
		char comm[64];
		int pid;

		memory = strstr(record, "anon-rss=");
		if (sscanf(record, "pid=%d comm=%63s total-vm=%ldkB",
			   &pid, comm, &stats->total_vm) == 3 &&
		    memory &&
		    sscanf(memory, "anon-rss=%ldkB file-rss:%ldkB shmem-rss:%ldkB",
			   &stats->anon_rss, &stats->file_rss,
			   &stats->shmem_rss) == 3 && pid == target)
			return true;
		record += 4;
	}
	return false;
}

static void run_victim(int ready_fd)
{
	struct victim_ready ready;
	unsigned char *mapping;
	size_t offset;

	if (write_text("/proc/self/oom_score_adj", "1000"))
		_exit(2);
	mapping = mmap(NULL, CHILD_MAP_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		_exit(3);
	for (offset = 0; offset < CHILD_MAP_SIZE; offset += PROCESS_PAGE_SIZE)
		mapping[offset] = (unsigned char)offset;
	ready.mapping = (uintptr_t)mapping;
	if (write(ready_fd, &ready, sizeof(ready)) != sizeof(ready))
		_exit(4);
	close(ready_fd);
	for (;;)
		pause();
}

static bool within_tolerance(long actual, long expected)
{
	long tolerance = expected / 10;

	if (tolerance < 1024)
		tolerance = 1024;
	return labs(actual - expected) <= tolerance;
}

static int run_test(void)
{
	struct victim_ready ready = { 0 };
	struct victim_stats trace_stats = { 0 };
	long expected_anon, expected_file, expected_shmem, expected_total;
	unsigned long kernel_page_size;
	char *trace = NULL;
	char saved_tracing_on[2] = { 0 };
	int trace_fd;
	bool anon_matches;
	bool child_ready;
	bool trace_ready;
	bool found = false;
	bool shared_matches;
	int pipefd[2];
	int status = 0;
	int attempts;
	bool victim_killed = false;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(7);

	trace_fd = open(TRACING_ON, O_RDONLY | O_CLOEXEC);
	if (trace_fd < 0 || read(trace_fd, saved_tracing_on, 1) != 1)
		ksft_exit_fail_msg("cannot read tracing state: %s\n", strerror(errno));
	close(trace_fd);
	trace_ready = !write_text("/proc/self/oom_score_adj", "-1000") &&
		!write_text(TRACING_ON, "1") &&
		!write_text(TRACE_ENABLE, "1") && !clear_trace();
	ksft_test_result(trace_ready, "enable OOM victim tracing\n");
	if (!trace_ready) {
		write_text(TRACING_ON, saved_tracing_on);
		ksft_exit_fail_msg("trace setup failed: %s\n", strerror(errno));
	}
	if (pipe(pipefd))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child) {
		close(pipefd[0]);
		run_victim(pipefd[1]);
	}
	close(pipefd[1]);
	child_ready = read(pipefd[0], &ready, sizeof(ready)) == sizeof(ready);
	close(pipefd[0]);
	if (!ppps_smaps_sum(child, ready.mapping, 1, "KernelPageSize",
			    &kernel_page_size))
		kernel_page_size = 0;
	expected_total = ppps_status_kb(child, "VmSize");
	expected_anon = ppps_status_kb(child, "RssAnon");
	expected_file = ppps_status_kb(child, "RssFile");
	expected_shmem = ppps_status_kb(child, "RssShmem");
	child_ready = child_ready && kernel_page_size >= PROCESS_PAGE_SIZE &&
		!(kernel_page_size % PROCESS_PAGE_SIZE) && expected_total > 0 &&
		expected_anon > 0 && expected_file >= 0 && expected_shmem >= 0;
	ksft_test_result(child_ready, "prepare a stable OOM victim\n");

	errno = 0;
	if (write_text("/proc/sysrq-trigger", "f"))
		kill(child, SIGKILL);
	/* A busy VM can select a different victim; never hang waiting for ours. */
	for (attempts = 0; attempts < 50; attempts++) {
		pid_t reaped = waitpid(child, &status, WNOHANG);

		if (reaped == child) {
			victim_killed = WIFSIGNALED(status) &&
				WTERMSIG(status) == SIGKILL;
			break;
		}
		if (reaped < 0 && errno != EINTR)
			break;
		usleep(100000);
	}
	if (!victim_killed) {
		kill(child, SIGKILL);
		waitpid(child, &status, 0);
	}
	ksft_test_result(victim_killed,
			 "force OOM to kill the selected child\n");

	trace = read_trace();
	if (trace)
		found = find_victim_stats(trace, child, &trace_stats);
	ksft_test_result(found, "capture the OOM mark_victim record\n");
	/* /proc status and trace both already report anonymous bytes in KB. */
	ksft_print_msg("native_page=%lu; status total=%ld anon=%ld file=%ld shmem=%ld kB; "
		       "trace total=%ld anon=%ld file=%ld shmem=%ld kB\n",
		       kernel_page_size, expected_total, expected_anon,
		       expected_file, expected_shmem,
		       trace_stats.total_vm, trace_stats.anon_rss,
		       trace_stats.file_rss, trace_stats.shmem_rss);
	ksft_test_result(found && trace_stats.total_vm == expected_total,
			 "report OOM victim virtual memory in process-page bytes\n");
	shared_matches = within_tolerance(trace_stats.file_rss, expected_file) &&
		within_tolerance(trace_stats.shmem_rss, expected_shmem);
	ksft_test_result(found && shared_matches,
			 "report file and shmem RSS in process-page bytes\n");
	anon_matches = within_tolerance(trace_stats.anon_rss, expected_anon);
	ksft_test_result(found && anon_matches,
			 "report anonymous RSS in native physical bytes\n");

	free(trace);
	write_text(TRACE_ENABLE, "0");
	write_text(TRACING_ON, saved_tracing_on);
	ksft_finished();
}

/* The environment gate precedes the compat re-exec, as it always did. */
int main(int argc, char **argv)
{
	if (!getenv("PPPS_OOM_TRACE_TEST_FORCE"))
		ksft_exit_skip("forced OOM test requires an isolated VM\n");
	return ppps_compat_main(argc, argv, run_test);
}
