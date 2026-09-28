// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process executes a 4K-aligned ELF whose segments must not be
 * mapped past its BSS, and the probe exits cleanly.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

static bool get_probe_path(char path[PATH_MAX])
{
	char *separator;
	ssize_t length;

	length = readlink("/proc/self/exe", path, PATH_MAX - 1);
	if (length < 0 || length == PATH_MAX - 1)
		return false;
	path[length] = '\0';
	separator = strrchr(path, '/');
	if (!separator)
		return false;
	if ((size_t)(separator - path) + sizeof("/elf_4k_align_probe") >
	    PATH_MAX)
		return false;
	strcpy(separator, "/elf_4k_align_probe");
	return true;
}

static int run_test(void)
{
	char probe_path[PATH_MAX];
	int status = 0;
	pid_t pid;

	ksft_print_header();
	ksft_set_plan(1);
	if (!get_probe_path(probe_path))
		ksft_exit_fail_msg("could not locate the ELF probe: %s\n",
				   strerror(errno));

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!pid) {
		execl(probe_path, "elf_4k_align_probe", NULL);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) != pid)
		ksft_exit_fail_msg("waitpid failed: %s\n", strerror(errno));
	if (WIFSIGNALED(status))
		ksft_print_msg("4K-aligned ELF died from signal %d\n",
			       WTERMSIG(status));
	else if (WIFEXITED(status) && WEXITSTATUS(status))
		ksft_print_msg("4K-aligned ELF exited with status %d\n",
			       WEXITSTATUS(status));
	ksft_test_result(WIFEXITED(status) && !WEXITSTATUS(status),
			 "execute a 4K-aligned ELF without mapping past its BSS\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
