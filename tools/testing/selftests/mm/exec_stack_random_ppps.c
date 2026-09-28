// SPDX-License-Identifier: GPL-2.0
/*
 * With ASLR enabled, page-local stack randomization of a 4K compat exec
 * stays within a two-process-page RLIMIT_STACK across repeated execs.  The
 * probe runs with an empty environment: the 8K budget must hold the
 * arguments, the ELF tables and up to one process page of randomization.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kselftest_ppps.h"

#define ATTEMPTS 64

static bool get_probe_path(char path[PATH_MAX])
{
	char *separator;
	ssize_t length;

	length = readlink("/proc/self/exe", path, PATH_MAX - 1);
	if (length < 0 || length == PATH_MAX - 1)
		return false;
	path[length] = '\0';
	separator = strrchr(path, '/');
	if (!separator ||
	    (size_t)(separator - path) + sizeof("/elf_4k_align_probe") >
	    PATH_MAX)
		return false;
	strcpy(separator, "/elf_4k_align_probe");
	return true;
}

static bool collect_execs(const char *probe, unsigned int *failed)
{
	struct rlimit limit = {
		.rlim_cur = 2 * PROCESS_PAGE_SIZE,
		.rlim_max = 2 * PROCESS_PAGE_SIZE,
	};
	unsigned int i;

	*failed = 0;
	for (i = 0; i < ATTEMPTS; i++) {
		int status;
		pid_t pid = fork();

		if (pid < 0)
			return false;
		if (!pid) {
			char *const argv[] = { "elf_4k_align_probe", NULL };
			char *const envp[] = { NULL };

			if (setrlimit(RLIMIT_STACK, &limit))
				_exit(126);
			execve(probe, argv, envp);
			_exit(127);
		}
		if (waitpid(pid, &status, 0) != pid)
			return false;
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			(*failed)++;
	}
	return true;
}

static bool aslr_enabled(void)
{
	int randomize_va_space;
	FILE *file = fopen("/proc/sys/kernel/randomize_va_space", "re");

	if (!file)
		return false;
	if (fscanf(file, "%d", &randomize_va_space) != 1)
		randomize_va_space = 0;
	fclose(file);
	return randomize_va_space > 0;
}

static int run_test(void)
{
	char probe[PATH_MAX];
	unsigned int failed;
	bool collected;

	ksft_print_header();
	if (!aslr_enabled())
		ksft_exit_skip("address randomization is disabled\n");
	ksft_set_plan(2);

	collected = get_probe_path(probe) && collect_execs(probe, &failed);
	ksft_test_result(collected, "execute tight-stack samples with ASLR\n");
	if (!collected)
		ksft_exit_fail_msg("could not collect exec samples\n");
	ksft_print_msg("failed exec samples: %u/%u\n", failed, ATTEMPTS);
	ksft_test_result(!failed,
			 "page-local stack randomization stays within 8K limit\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
