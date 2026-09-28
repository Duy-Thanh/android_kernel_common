// SPDX-License-Identifier: GPL-2.0
/*
 * brk ASLR of a 4K compat process places the initial brk at every 4K
 * residue within a native 16K page across repeated execs.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define SAMPLE_COUNT 64

static char sample_path[PATH_MAX];

static bool find_sample(void)
{
	char *filename;
	size_t dir_length;
	ssize_t length;

	length = readlink("/proc/self/exe", sample_path,
			  sizeof(sample_path) - 1);
	if (length < 0 || (size_t)length >= sizeof(sample_path) - 1)
		return false;
	sample_path[length] = '\0';
	filename = strrchr(sample_path, '/');
	if (!filename)
		return false;
	dir_length = filename + 1 - sample_path;
	if (dir_length + sizeof("brk_aslr_ppps_sample") > sizeof(sample_path))
		return false;
	memcpy(sample_path + dir_length, "brk_aslr_ppps_sample",
	       sizeof("brk_aslr_ppps_sample"));
	return true;
}

static bool collect_samples(unsigned int *residue_mask,
			    uintptr_t *minimum, uintptr_t *maximum)
{
	unsigned int i;

	*residue_mask = 0;
	*minimum = UINTPTR_MAX;
	*maximum = 0;
	for (i = 0; i < SAMPLE_COUNT; i++) {
		uintptr_t brk;
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
			execl(sample_path, "brk_aslr_ppps_sample", NULL);
			_exit(127);
		}
		close(pipefd[1]);
		if (!read_full(pipefd[0], &brk, sizeof(brk))) {
			close(pipefd[0]);
			waitpid(pid, &status, 0);
			return false;
		}
		close(pipefd[0]);
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
		    WEXITSTATUS(status))
			return false;

		*residue_mask |= 1U << ((brk / PROCESS_PAGE_SIZE) & 3);
		if (brk < *minimum)
			*minimum = brk;
		if (brk > *maximum)
			*maximum = brk;
	}
	return true;
}

static bool brk_aslr_enabled(void)
{
	int randomize_va_space;
	FILE *file;

	file = fopen("/proc/sys/kernel/randomize_va_space", "r");
	if (!file)
		return false;
	if (fscanf(file, "%d", &randomize_va_space) != 1)
		randomize_va_space = 0;
	fclose(file);
	return randomize_va_space > 1;
}

static int run_test(void)
{
	uintptr_t minimum, maximum;
	unsigned int residue_mask;
	bool collected;

	ksft_print_header();
	if (!brk_aslr_enabled())
		ksft_exit_skip("brk ASLR is disabled\n");
	ksft_set_plan(2);

	collected = find_sample() &&
		collect_samples(&residue_mask, &minimum, &maximum);
	ksft_test_result(collected, "collect randomized brk samples across exec\n");
	if (!collected)
		ksft_exit_fail_msg("failed to collect brk samples: %s\n",
				   strerror(errno));

	ksft_print_msg("samples=%u min=%#lx max=%#lx 16K-residue-mask=%#x\n",
		       SAMPLE_COUNT, (unsigned long)minimum,
		       (unsigned long)maximum, residue_mask);
	ksft_test_result(residue_mask == 0xf,
			 "brk ASLR uses all process-page residues within 16K\n");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
