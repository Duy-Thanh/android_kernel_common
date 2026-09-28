// SPDX-License-Identifier: GPL-2.0
/*
 * With address randomization disabled, a 4K compat process can exec a 4K
 * ELF under a one-process-page RLIMIT_STACK.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/resource.h>
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
	struct rlimit limit = {
		.rlim_cur = PROCESS_PAGE_SIZE,
		.rlim_max = PROCESS_PAGE_SIZE,
	};
	char *const argv[] = { (char *)"elf_4k_align_probe", NULL };
	char *const envp[] = { NULL };
	int status = 0;
	pid_t pid;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(1);
	if (!get_probe_path(probe_path))
		ksft_exit_fail_msg("could not locate the ELF probe\n");

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_msg("fork failed\n");
	if (!pid) {
		if (setrlimit(RLIMIT_STACK, &limit))
			_exit(126);
		execve(probe_path, argv, envp);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) != pid)
		ksft_exit_fail_msg("waitpid failed\n");
	if (WIFSIGNALED(status))
		ksft_print_msg("tight-stack ELF died from signal %d\n",
			       WTERMSIG(status));
	else if (WIFEXITED(status) && WEXITSTATUS(status))
		ksft_print_msg("tight-stack ELF exited with status %d\n",
			       WEXITSTATUS(status));
	ksft_test_result(WIFEXITED(status) && !WEXITSTATUS(status),
			 "execute a 4K ELF with a one-page stack limit\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode) {
		/* The probe inherits ADDR_NO_RANDOMIZE through the re-exec. */
		int persona = personality(0xffffffffUL);

		if (persona < 0 ||
		    personality(persona | ADDR_NO_RANDOMIZE) < 0)
			ksft_exit_fail_msg("personality set failed\n");
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	}
	if (argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	return EXIT_FAILURE;
}
