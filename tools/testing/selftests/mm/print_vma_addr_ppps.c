// SPDX-License-Identifier: GPL-2.0
/*
 * An unhandled exception trace for a 4K compat process reports the mapping's
 * 4K file offset in print_vma_addr(), checked by executing UDF from a private
 * memfd mapping at file offset 4K and reading the kernel log.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/klog.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#ifndef SYSLOG_ACTION_READ_ALL
#define SYSLOG_ACTION_READ_ALL 3
#endif

#ifndef SYSLOG_ACTION_SIZE_BUFFER
#define SYSLOG_ACTION_SIZE_BUFFER 10
#endif

#define TEST_COMM "vmaoff-ppps"

static char *read_kernel_log(void)
{
	char *log;
	int size;
	int len;

	size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (size < 0)
		return NULL;
	log = malloc(size + 1);
	if (!log)
		return NULL;
	len = klogctl(SYSLOG_ACTION_READ_ALL, log, size);
	if (len < 0) {
		free(log);
		return NULL;
	}
	log[len] = '\0';
	return log;
}

static char *last_exception_message(char *log)
{
	char *message = NULL;
	char *next = log;

	while ((next = strstr(next, TEST_COMM))) {
		char *exception = strstr(next, "unhandled exception:");
		char *newline = strchr(next, '\n');

		if (exception && (!newline || exception < newline))
			message = next;
		if (!newline)
			break;
		next = newline + 1;
	}
	return message;
}

static int run_test(void)
{
	const uint32_t udf_instruction = 0;
	void (*trigger)(void);
	char *message;
	char *log;
	char *newline;
	void *mapping;
	pid_t child;
	int status;
	int trace_fd;
	int memfd;

	ksft_print_header();
	ksft_set_plan(2);

	trace_fd = open("/proc/sys/debug/exception-trace", O_WRONLY | O_CLOEXEC);
	if (trace_fd < 0 || write(trace_fd, "1\n", 2) != 2)
		ksft_exit_skip("cannot enable exception trace: %s\n",
			       strerror(errno));
	close(trace_fd);

	memfd = memfd_create("print-vma-addr-ppps", MFD_CLOEXEC);
	if (memfd < 0 || ftruncate(memfd, 2 * PROCESS_PAGE_SIZE) ||
	    pwrite(memfd, &udf_instruction, sizeof(udf_instruction),
		   PROCESS_PAGE_SIZE) != (ssize_t)sizeof(udf_instruction))
		ksft_exit_fail_msg("create executable memfd failed: %s\n",
				   strerror(errno));
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_EXEC,
		       MAP_PRIVATE, memfd, PROCESS_PAGE_SIZE);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child) {
		/* The kernel only logs signals that are unhandled. */
		signal(SIGILL, SIG_DFL);
		prctl(PR_SET_NAME, TEST_COMM, 0, 0, 0);
		trigger = mapping;
		trigger();
		_exit(EXIT_FAILURE);
	}
	if (waitpid(child, &status, 0) != child)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	ksft_test_result(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL,
			 "execute UDF from the file mapping\n");

	log = read_kernel_log();
	if (!log)
		ksft_exit_fail_msg("read kernel log failed: %s\n",
				   strerror(errno));
	message = last_exception_message(log);
	newline = message ? strchr(message, '\n') : NULL;
	if (newline)
		*newline = '\0';
	ksft_print_msg("exception trace: %s\n", message ?: "not found");
	ksft_test_result(message && strstr(message, "[1000,"),
			 "report the 4K file offset in print_vma_addr()\n");

	free(log);
	munmap(mapping, PROCESS_PAGE_SIZE);
	close(memfd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
