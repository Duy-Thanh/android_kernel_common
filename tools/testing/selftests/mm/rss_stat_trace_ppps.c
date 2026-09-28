// SPDX-License-Identifier: GPL-2.0
/*
 * The kmem:rss_stat tracepoint reports a 4K compat process's anonymous RSS
 * in bytes.  Anonymous pages are packed into native 16K tuples, so the RSS
 * grows in multiples of the 4K process page no larger than a native page.
 */
#define _GNU_SOURCE

#include <ctype.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TRACEFS "/sys/kernel/tracing"
#define TEST_PAGES 32

static char trace_data[1024 * 1024];

static int write_text(const char *path, const char *text)
{
	ssize_t length = strlen(text);
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (write(fd, text, length) != length) {
		close(fd);
		return -1;
	}
	return close(fd);
}

static int prepare_tracefs(void)
{
	(void)mkdir("/sys", 0755);
	(void)mkdir("/sys/kernel", 0755);
	(void)mkdir(TRACEFS, 0755);
	if (mount("tracefs", TRACEFS, "tracefs", 0, NULL) && errno != EBUSY)
		return -1;
	if (write_text(TRACEFS "/tracing_on", "0\n"))
		return -1;
	if (write_text(TRACEFS "/events/kmem/rss_stat/enable", "0\n"))
		return -1;
	if (write_text(TRACEFS "/events/kmem/rss_stat/filter", "0\n"))
		return -1;
	return write_text(TRACEFS "/trace", "\n");
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
	while (b) {
		uint64_t remainder = a % b;

		a = b;
		b = remainder;
	}
	return a;
}

static pid_t line_pid(const char *line, const char *end)
{
	const char *bracket = memchr(line, '[', end - line);
	const char *number_end;
	const char *number;

	if (!bracket)
		return -1;
	number_end = bracket;
	while (number_end > line && isspace((unsigned char)number_end[-1]))
		number_end--;
	number = number_end;
	while (number > line && isdigit((unsigned char)number[-1]))
		number--;
	if (number == number_end || number == line || number[-1] != '-')
		return -1;
	return strtol(number, NULL, 10);
}

static uint64_t read_reported_granule(pid_t child, unsigned int *events)
{
	uint64_t previous = 0;
	uint64_t granule = 0;
	char *cursor;
	ssize_t length;
	int fd;

	*events = 0;
	fd = open(TRACEFS "/trace", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	length = read(fd, trace_data, sizeof(trace_data) - 1);
	close(fd);
	if (length <= 0)
		return 0;
	trace_data[length] = '\0';

	for (cursor = trace_data; cursor < trace_data + length;) {
		char *line_end = memchr(cursor, '\n', trace_data + length - cursor);
		char *event;
		char *size_field;
		uint64_t size;

		if (!line_end)
			line_end = trace_data + length;
		event = strstr(cursor, ": rss_stat:");
		if (!event || event >= line_end || line_pid(cursor, line_end) != child)
			goto next;
		if (!memmem(event, line_end - event, "curr=1", 6) ||
		    !memmem(event, line_end - event, "type=MM_ANONPAGES", 17))
			goto next;
		size_field = memmem(event, line_end - event, "size=", 5);
		if (!size_field)
			goto next;
		size = strtoull(size_field + 5, NULL, 10);
		if (previous && size > previous)
			granule = gcd64(granule, size - previous);
		previous = size;
		(*events)++;
next:
		cursor = line_end + (line_end < trace_data + length);
	}
	return granule;
}

static int child_workload(int start_fd, int done_fd, int finish_fd)
{
	unsigned char *mapping;
	char byte;
	int i;

	if (read(start_fd, &byte, 1) != 1)
		return 1;
	mapping = mmap(NULL, (TEST_PAGES + 1) * PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return 1;
	for (i = 0; i <= TEST_PAGES; i++)
		__atomic_store_n(&mapping[i * PROCESS_PAGE_SIZE], i,
				 __ATOMIC_RELAXED);
	if (write(done_fd, "d", 1) != 1)
		return 1;
	if (read(finish_fd, &byte, 1) != 1)
		return 1;
	return 0;
}

static int run_test(void)
{
	int start_pipe[2], done_pipe[2], finish_pipe[2];
	unsigned int events = 0;
	uint64_t granule = 0;
	char filter[64];
	char byte;
	pid_t child;
	int status;

	ksft_print_header();
	ksft_set_plan(6);
	ksft_test_result(prepare_tracefs() == 0,
			 "prepare the rss_stat tracepoint\n");
	if (pipe(start_pipe) || pipe(done_pipe) || pipe(finish_pipe))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child) {
		close(start_pipe[1]);
		close(done_pipe[0]);
		close(finish_pipe[1]);
		_exit(child_workload(start_pipe[0], done_pipe[1], finish_pipe[0]));
	}
	close(start_pipe[0]);
	close(done_pipe[1]);
	close(finish_pipe[0]);

	snprintf(filter, sizeof(filter), "common_pid == %d\n", child);
	status = write_text(TRACEFS "/events/kmem/rss_stat/filter", filter) ||
		 write_text(TRACEFS "/trace", "\n") ||
		 write_text(TRACEFS "/events/kmem/rss_stat/enable", "1\n") ||
		 write_text(TRACEFS "/tracing_on", "1\n");
	ksft_test_result(status == 0, "enable tracing for the child\n");
	if (status || write(start_pipe[1], "s", 1) != 1 ||
	    read(done_pipe[0], &byte, 1) != 1)
		ksft_exit_fail_msg("could not run traced child workload\n");

	status = write_text(TRACEFS "/tracing_on", "0\n");
	granule = read_reported_granule(child, &events);
	ksft_test_result(status == 0, "stop tracing and read the trace\n");
	ksft_test_result(events >= (TEST_PAGES + 1) / PPPS_SLICES,
			 "capture anonymous RSS updates (%u events)\n", events);
	ksft_print_msg("reported_rss_granule=%llu bytes\n",
		       (unsigned long long)granule);
	ksft_test_result(granule && granule % PROCESS_PAGE_SIZE == 0 &&
			 granule <= NATIVE_PAGE_SIZE,
			 "rss_stat reports bytes in process-page multiples up to a native page\n");

	(void)write(finish_pipe[1], "f", 1);
	waitpid(child, &status, 0);
	ksft_test_result(WIFEXITED(status) && WEXITSTATUS(status) == 0,
			 "child exits cleanly\n");
	(void)write_text(TRACEFS "/events/kmem/rss_stat/enable", "0\n");
	(void)write_text(TRACEFS "/events/kmem/rss_stat/filter", "0\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
