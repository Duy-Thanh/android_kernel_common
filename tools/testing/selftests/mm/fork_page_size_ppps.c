// SPDX-License-Identifier: GPL-2.0
/*
 * fork() preserves the parent's page size, native or 4K compat, even when
 * the ADDR_4KB_COMPAT_PAGE_SIZE personality bit has been flipped before the
 * fork: the child sees the parent's data and smaps MMUPageSize.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define WAIT_STEP_US	1000
#define WAIT_TIMEOUT_US	2000000

static bool wait_for_child(pid_t child)
{
	int elapsed, status;

	for (elapsed = 0; elapsed < WAIT_TIMEOUT_US;
	     elapsed += WAIT_STEP_US) {
		pid_t waited = waitpid(child, &status, WNOHANG);

		if (waited == child)
			return WIFEXITED(status) &&
			       WEXITSTATUS(status) == EXIT_SUCCESS;
		if (waited < 0)
			return false;
		usleep(WAIT_STEP_US);
	}
	kill(child, SIGKILL);
	waitpid(child, &status, 0);
	return false;
}

static int run_fork_case(bool compat)
{
	unsigned long page_size = sysconf(_SC_PAGESIZE);
	unsigned char *mapping;
	int persona, previous;
	pid_t child;
	size_t i;

	if (page_size != (compat ? PROCESS_PAGE_SIZE : NATIVE_PAGE_SIZE))
		return EXIT_FAILURE;
	mapping = mmap(NULL, 4 * page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return EXIT_FAILURE;
	for (i = 0; i < 4 * page_size; i++)
		mapping[i] = (unsigned char)(i * 37 + 11);

	persona = personality(0xffffffffUL);
	if (persona < 0 || !!(persona & ADDR_4KB_COMPAT_PAGE_SIZE) != compat)
		return EXIT_FAILURE;
	previous = persona;
	if (compat)
		persona &= ~ADDR_4KB_COMPAT_PAGE_SIZE;
	else
		persona |= ADDR_4KB_COMPAT_PAGE_SIZE;
	/* Exercise libc's signed int return and an unrelated personality flag. */
	persona |= UNAME26;
	if (personality((unsigned int)persona) != previous ||
	    personality(0xffffffffUL) != persona)
		return EXIT_FAILURE;

	child = fork();
	if (child < 0)
		return EXIT_FAILURE;
	if (!child) {
		unsigned long mmu_page_size;

		if (personality(0xffffffffUL) != persona)
			_exit(EXIT_FAILURE);
		for (i = 0; i < 4 * page_size; i++) {
			if (mapping[i] != (unsigned char)(i * 37 + 11))
				_exit(EXIT_FAILURE);
		}
		if (!ppps_smaps_bytes(mapping, 1, "MMUPageSize",
				      &mmu_page_size))
			mmu_page_size = 0;
		dprintf(STDOUT_FILENO,
			"# %s fork: exec page size %lu, MMU page size %lu\n",
			compat ? "compat" : "native", page_size,
			mmu_page_size);
		_exit(mmu_page_size == page_size ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return wait_for_child(child) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int reexec_case(bool compat)
{
	ppps_execl(compat, NULL, compat ? "--compat" : "--native", NULL);
	return EXIT_FAILURE;
}

static bool run_case(bool compat)
{
	pid_t child = fork();

	if (child < 0)
		return false;
	if (!child)
		_exit(reexec_case(compat));
	return wait_for_child(child);
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (argc == 2 && mode && !strcmp(mode, "--native"))
		return run_fork_case(false);
	if (argc == 2 && mode && !strcmp(mode, "--compat"))
		return run_fork_case(true);
	if (argc != 1)
		return EXIT_FAILURE;

	ksft_print_header();
	ksft_set_plan(2);
	ksft_test_result(run_case(false),
			 "fork preserves a native process page size\n");
	ksft_test_result(run_case(true),
			 "fork preserves a 4K compat process page size\n");
	ksft_finished();
}
