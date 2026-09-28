// SPDX-License-Identifier: GPL-2.0
/*
 * Stack ASLR of a 4K compat process places the stack end at every 4K
 * residue within a native 16K page across repeated execs.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <signal.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define SAMPLE_COUNT 64

static bool read_stack_end(pid_t pid, uintptr_t *stack_end)
{
	char path[64];
	char *line = NULL;
	size_t line_size = 0;
	bool found = false;
	FILE *file;

	if (snprintf(path, sizeof(path), "/proc/%d/maps", pid) >=
	    (int)sizeof(path))
		return false;
	file = fopen(path, "re");
	if (!file)
		return false;
	while (getline(&line, &line_size, file) >= 0) {
		unsigned long start, end;

		if (strstr(line, "[stack]") &&
		    sscanf(line, "%lx-%lx", &start, &end) == 2) {
			*stack_end = end;
			found = true;
			break;
		}
	}
	free(line);
	fclose(file);
	return found;
}

static bool collect_samples(unsigned int *residue_mask)
{
	unsigned int i;

	*residue_mask = 0;
	for (i = 0; i < SAMPLE_COUNT; i++) {
		uintptr_t stack_end;
		unsigned char ready;
		int pipefd[2];
		int status;
		pid_t pid;

		if (pipe(pipefd))
			return false;
		pid = fork();
		if (pid < 0) {
			close(pipefd[0]);
			close(pipefd[1]);
			return false;
		}
		if (!pid) {
			close(pipefd[0]);
			if (pipefd[1] != 3) {
				if (dup2(pipefd[1], 3) < 0)
					_exit(126);
				close(pipefd[1]);
			}
			ppps_execl(true, NULL, "--sample", NULL);
			_exit(127);
		}
		close(pipefd[1]);
		if (read(pipefd[0], &ready, 1) != 1 ||
		    !read_stack_end(pid, &stack_end)) {
			close(pipefd[0]);
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return false;
		}
		close(pipefd[0]);
		*residue_mask |= 1U << ((stack_end / PROCESS_PAGE_SIZE) & 3);
		kill(pid, SIGKILL);
		if (waitpid(pid, &status, 0) != pid)
			return false;
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
	unsigned int residue_mask;
	bool collected;

	ppps_require_compat();
	ksft_print_header();
	if (!aslr_enabled())
		ksft_exit_skip("address randomization is disabled\n");
	ksft_set_plan(2);

	collected = collect_samples(&residue_mask);
	ksft_test_result(collected, "collect stack VMA ends across exec\n");
	if (!collected)
		ksft_exit_fail_msg("could not collect stack samples\n");
	ksft_print_msg("16K stack-end residue mask: %#x\n", residue_mask);
	ksft_test_result(residue_mask == 0xf,
			 "stack ASLR uses every process-page residue within 16K\n");
	ksft_finished();
}

static int sample(void)
{
	unsigned char ready = 1;

	if (write(3, &ready, 1) != 1)
		return EXIT_FAILURE;
	for (;;)
		pause();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	if (argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	if (argc == 2 && !strcmp(mode, "--sample"))
		return sample();
	return EXIT_FAILURE;
}
