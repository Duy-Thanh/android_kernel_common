// SPDX-License-Identifier: GPL-2.0
/*
 * mmap ASLR of a 4K compat process randomizes the anonymous mapping base
 * across fresh execs at 4K process-page alignment.  The base is randomized
 * with the native mmap granularity, so its residue within a 16K page is not
 * required to vary.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define SAMPLE_COUNT 64

static bool collect_samples(unsigned int *residue_mask,
			    unsigned int *distinct_bases)
{
	uintptr_t first = 0;
	unsigned int i;

	*residue_mask = 0;
	*distinct_bases = 0;
	for (i = 0; i < SAMPLE_COUNT; i++) {
		uintptr_t address;
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
		if (read(pipefd[0], &address, sizeof(address)) !=
		    sizeof(address)) {
			close(pipefd[0]);
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return false;
		}
		close(pipefd[0]);
		if (waitpid(pid, &status, 0) != pid ||
		    !WIFEXITED(status) || WEXITSTATUS(status))
			return false;
		if (address & (PROCESS_PAGE_SIZE - 1))
			return false;
		*residue_mask |= 1U << ((address / PROCESS_PAGE_SIZE) & 3);
		if (!i)
			first = address;
		if (!i || address != first)
			(*distinct_bases)++;
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
	unsigned int distinct_bases;
	bool collected;

	ppps_require_compat();
	ksft_print_header();
	if (!aslr_enabled())
		ksft_exit_skip("address randomization is disabled\n");
	ksft_set_plan(2);

	collected = collect_samples(&residue_mask, &distinct_bases);
	ksft_test_result(collected,
			 "collect process-page-aligned mmap bases across exec\n");
	if (!collected)
		ksft_exit_fail_msg("could not collect mmap samples\n");
	ksft_print_msg("16K mmap residue mask: %#x, %u distinct bases\n",
		       residue_mask, distinct_bases);
	ksft_test_result(distinct_bases > 1,
			 "mmap ASLR randomizes the base across execs\n");
	ksft_finished();
}

static int sample(void)
{
	void *mapping;
	uintptr_t address;

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return EXIT_FAILURE;
	address = (uintptr_t)mapping;
	if (write(3, &address, sizeof(address)) != sizeof(address))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], "--compat", NULL);
	if (argc == 2 && !strcmp(mode, "--compat"))
		return run_test();
	if (argc == 2 && !strcmp(mode, "--sample"))
		return sample();
	return EXIT_FAILURE;
}
