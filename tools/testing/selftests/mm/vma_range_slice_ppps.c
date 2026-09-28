// SPDX-License-Identifier: GPL-2.0
/*
 * A native parent observes a 4K compat child through exec and verifies that
 * file-backed VMA split/merge operations preserve the starting file slice.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define CASES		4
#define TIMEOUT_MS	5000

struct child_report {
	unsigned int cases;
	unsigned int merged;
	unsigned int refaulted;
};

static unsigned long maps_offset(void *address, unsigned int *segments)
{
	unsigned long first = ULONG_MAX;
	unsigned long start, end, offset;
	unsigned long low = (unsigned long)address;
	unsigned long high = low + NATIVE_PAGE_SIZE;
	char line[512];
	FILE *maps;

	*segments = 0;
	maps = fopen("/proc/self/maps", "re");
	if (!maps)
		return ULONG_MAX;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &offset) != 3 ||
		    end <= low || start >= high)
			continue;
		if (!*segments)
			first = offset + low - start;
		(*segments)++;
	}
	fclose(maps);
	return first;
}

static bool contents_match(const unsigned char *mapping)
{
	unsigned int slice;

	for (slice = 0; slice < PPPS_SLICES; slice++)
		if (mapping[slice * PROCESS_PAGE_SIZE] != 0x41 + slice)
			return false;
	return true;
}

static bool run_case(int fd, unsigned int which, bool *refaulted)
{
	unsigned char *mapping;
	unsigned int segments;
	unsigned long offset;
	bool merged = false;
	void *reservation;
	uintptr_t aligned;

	reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return false;
	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	mapping = mmap((void *)aligned, NATIVE_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	if (mapping == MAP_FAILED)
		goto out;

	switch (which) {
	case 0:
		if (mprotect(mapping, PROCESS_PAGE_SIZE, PROT_READ) ||
		    mprotect(mapping, PROCESS_PAGE_SIZE,
			     PROT_READ | PROT_WRITE))
			goto out;
		break;
	case 1:
		if (mprotect(mapping + PROCESS_PAGE_SIZE,
			     3 * PROCESS_PAGE_SIZE, PROT_READ) ||
		    mprotect(mapping, PROCESS_PAGE_SIZE, PROT_READ))
			goto out;
		break;
	case 2:
		if (mprotect(mapping, 3 * PROCESS_PAGE_SIZE, PROT_READ) ||
		    mprotect(mapping + 3 * PROCESS_PAGE_SIZE,
			     PROCESS_PAGE_SIZE, PROT_READ))
			goto out;
		break;
	case 3:
		if (mprotect(mapping + PROCESS_PAGE_SIZE,
			     2 * PROCESS_PAGE_SIZE, PROT_READ) ||
		    mprotect(mapping + PROCESS_PAGE_SIZE,
			     2 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE))
			goto out;
		break;
	default:
		goto out;
	}

	offset = maps_offset(mapping, &segments);
	merged = segments == 1 && offset == 0;
	*refaulted = contents_match(mapping) &&
		!madvise(mapping, NATIVE_PAGE_SIZE, MADV_DONTNEED) &&
		contents_match(mapping);
out:
	munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	return merged;
}

static int run_child(int report_fd)
{
	unsigned char page[PROCESS_PAGE_SIZE];
	struct child_report report = {};
	unsigned char ready = 0xa5;
	unsigned int slice;
	int fd;

	ppps_require_compat();
	if (!write_full(report_fd, &ready, sizeof(ready)))
		return EXIT_FAILURE;
	fd = syscall(__NR_memfd_create, "vma-range-slice", 0);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		return EXIT_FAILURE;
	for (slice = 0; slice < PPPS_SLICES; slice++) {
		memset(page, 0x41 + slice, sizeof(page));
		if (pwrite(fd, page, sizeof(page), slice * PROCESS_PAGE_SIZE) !=
		    sizeof(page))
			return EXIT_FAILURE;
	}
	for (report.cases = 0; report.cases < CASES; report.cases++) {
		bool refaulted = false;

		if (run_case(fd, report.cases, &refaulted))
			report.merged++;
		if (refaulted)
			report.refaulted++;
	}
	close(fd);
	if (!write_full(report_fd, &report, sizeof(report)))
		return EXIT_FAILURE;
	return report.merged == CASES && report.refaulted == CASES ?
		EXIT_SUCCESS : EXIT_FAILURE;
}

static bool read_timeout(int fd, void *buffer, size_t size)
{
	struct pollfd pollfd = {
		.fd = fd,
		.events = POLLIN | POLLHUP,
	};

	return poll(&pollfd, 1, TIMEOUT_MS) > 0 &&
	       read_full(fd, buffer, size);
}

static int run_parent(void)
{
	struct child_report report = {};
	unsigned char ready = 0;
	char report_fd[16];
	bool entered, reported;
	int pipefd[2];
	int status;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(4);
	if (getpagesize() != NATIVE_PAGE_SIZE)
		ksft_exit_skip("requires a native 16K process\n");
	if (pipe(pipefd))
		ksft_exit_fail_msg("pipe failed: %s\n", strerror(errno));
	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child) {
		close(pipefd[0]);
		snprintf(report_fd, sizeof(report_fd), "%d", pipefd[1]);
		ppps_execl(true, NULL, "--child", report_fd, NULL);
		_exit(EXIT_FAILURE);
	}
	close(pipefd[1]);
	entered = read_timeout(pipefd[0], &ready, sizeof(ready)) && ready == 0xa5;
	reported = entered && read_timeout(pipefd[0], &report, sizeof(report));
	if (!reported)
		kill(child, SIGKILL);
	if (waitpid(child, &status, 0) != child)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	close(pipefd[0]);

	ksft_test_result(entered,
			 "4K compat child enters main after exec\n");
	ksft_test_result(reported && report.cases == CASES &&
			 report.merged == CASES,
			 "file VMAs merge with their original starting offset\n");
	ksft_test_result(reported && report.refaulted == CASES,
			 "merged file VMAs refault their original slice data\n");
	ksft_test_result(WIFEXITED(status) && !WEXITSTATUS(status),
			 "4K compat child exits successfully\n");
	if (WIFSIGNALED(status))
		ksft_print_msg("4K compat child died from signal %d\n",
			       WTERMSIG(status));
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		return run_parent();
	if (argc == 3 && !strcmp(mode, "--child"))
		return run_child(atoi(argv[2]));
	return EXIT_FAILURE;
}
