// SPDX-License-Identifier: GPL-2.0
/*
 * With shmem THP forced on, a 4K compat process's large memfd mappings are
 * placed PMD-aligned on every sampled exec despite address randomization.
 */
#define _GNU_SOURCE

#include <linux/memfd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define MIN_ARM64_PMD_SIZE (2UL * 1024 * 1024)
#define MAPPING_SIZE (64UL * 1024 * 1024)
#define SAMPLE_COUNT 16
#define SHMEM_POLICY_PATH "/sys/kernel/mm/transparent_hugepage/shmem_enabled"

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

static bool get_shmem_policy(char *policy, size_t policy_size)
{
	char buffer[256];
	char *end;
	char *start;
	size_t length;
	FILE *file;

	file = fopen(SHMEM_POLICY_PATH, "re");
	if (!file)
		return false;
	if (!fgets(buffer, sizeof(buffer), file)) {
		fclose(file);
		return false;
	}
	fclose(file);
	start = strchr(buffer, '[');
	end = start ? strchr(start, ']') : NULL;
	if (!start || !end)
		return false;
	length = end - ++start;
	if (!length || length >= policy_size)
		return false;
	memcpy(policy, start, length);
	policy[length] = '\0';
	return true;
}

static bool set_shmem_policy(const char *policy)
{
	int fd;
	ssize_t length = strlen(policy);
	bool success;

	fd = open(SHMEM_POLICY_PATH, O_WRONLY);
	if (fd < 0)
		return false;
	success = write(fd, policy, length) == length;
	close(fd);
	return success;
}

static bool collect_samples(unsigned int *aligned)
{
	unsigned int i;

	*aligned = 0;
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
		if (!(address & (MIN_ARM64_PMD_SIZE - 1)))
			(*aligned)++;
	}
	return true;
}

static int run_test(void)
{
	char original_policy[32];
	unsigned int aligned;
	bool collected;
	bool restored;

	ppps_require_compat();
	ksft_print_header();
	if (!aslr_enabled())
		ksft_exit_skip("address randomization is disabled\n");
	if (!get_shmem_policy(original_policy, sizeof(original_policy)) ||
	    !set_shmem_policy("force"))
		ksft_exit_skip("could not enable shmem THP policy\n");
	ksft_set_plan(3);

	collected = collect_samples(&aligned);
	ksft_test_result(collected, "collect memfd mmap bases across exec\n");
	ksft_print_msg("PMD-aligned memfd mappings: %u/%u\n", aligned,
		       SAMPLE_COUNT);
	ksft_test_result(collected && aligned == SAMPLE_COUNT,
			 "shmem THP mappings retain PMD alignment\n");
	restored = set_shmem_policy(original_policy);
	ksft_test_result(restored, "restore shmem THP policy\n");
	ksft_finished();
}

static int sample(void)
{
	uintptr_t address;
	void *mapping;
	int fd;

	fd = memfd_create("shmem-thp-mmap-ppps", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, MAPPING_SIZE))
		return EXIT_FAILURE;
	mapping = mmap(NULL, MAPPING_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
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
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	if (argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	if (argc == 2 && !strcmp(mode, "--sample"))
		return sample();
	return EXIT_FAILURE;
}
