// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process mapping a shmem (memfd) or secretmem file that is one
 * 4K page long can access that page but takes SIGBUS on a fault at its 4K
 * EOF, even though the backing native page is 16K.  The file kind is
 * selected on the command line: "shmem" or "secretmem".
 */
#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest_ppps.h"

#ifndef __NR_memfd_secret
#define __NR_memfd_secret 447
#endif

struct memfd_case {
	const char *name;
	int (*create)(void);
	const char *create_name;
};

static int create_shmem(void)
{
	return memfd_create("shmem-eof-ppps", 0);
}

static int create_secretmem(void)
{
	return syscall(__NR_memfd_secret, 0);
}

static const struct memfd_case memfd_cases[] = {
	{ "shmem", create_shmem, "memfd_create" },
	{ "secretmem", create_secretmem, "memfd_secret" },
};

static bool faults_with_sigbus(void *mapping)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!pid) {
		unsigned char value;

		memcpy(&value, mapping, sizeof(value));
		_exit(value == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (waitpid(pid, &status, 0) != pid)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS;
}

static int run_test(const struct memfd_case *test)
{
	unsigned char *valid;
	void *past_eof;
	int fd;

	ksft_print_header();
	ksft_set_plan(3);

	fd = test->create();
	if (fd < 0)
		ksft_exit_fail_msg("%s failed: %s\n", test->create_name,
				   strerror(errno));
	if (ftruncate(fd, PROCESS_PAGE_SIZE))
		ksft_exit_fail_msg("ftruncate failed: %s\n", strerror(errno));

	valid = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_SHARED, fd, 0);
	ksft_test_result(valid != MAP_FAILED, "map the first %s page\n",
			 test->name);
	if (valid == MAP_FAILED)
		ksft_exit_fail_msg("valid mmap failed: %s\n", strerror(errno));
	valid[0] = 0x5a;
	ksft_test_result(valid[0] == 0x5a,
			 "fault and access %s data before EOF\n", test->name);

	past_eof = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
			PROCESS_PAGE_SIZE);
	if (past_eof == MAP_FAILED)
		ksft_exit_fail_msg("past-EOF mmap failed: %s\n", strerror(errno));
	ksft_test_result(faults_with_sigbus(past_eof),
			 "SIGBUS a %s fault at the 4K EOF\n", test->name);

	munmap(past_eof, PROCESS_PAGE_SIZE);
	munmap(valid, PROCESS_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

static const struct memfd_case *find_case(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(memfd_cases); i++)
		if (!strcmp(memfd_cases[i].name, name))
			return &memfd_cases[i];
	return NULL;
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	const struct memfd_case *test = NULL;

	if (mode) {
		if (!strcmp(mode, PPPS_RUN_FLAG) && argc == 3)
			test = find_case(argv[2]);
	} else if (argc == 2) {
		test = find_case(argv[1]);
	}
	if (!test) {
		fprintf(stderr, "usage: %s shmem|secretmem\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (mode)
		ppps_require_compat();
	else if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, test->name, NULL);
	return run_test(test);
}
