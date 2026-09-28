// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat owner repeatedly execs a native (16K) child whose argv array
 * lives in a cold, not-yet-faulted 4K file page; every exec completes and
 * the child sees a valid page size.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define TRANSITION_COUNT	32
#define EXEC_TIMEOUT_US		500000
#define WAIT_STEP_US		1000

static const char *const native_argv[] = {
	"/proc/self/exe",
	"--native",
	NULL,
};

static int exec_native_with_cold_argv(void)
{
	char **exec_argv;
	int memfd;
	int persona = personality(0xffffffffUL);

	if (persona < 0 ||
	    personality(persona & ~ADDR_4KB_COMPAT_PAGE_SIZE) < 0)
		return EXIT_FAILURE;
	memfd = memfd_create("exec-argv", 0);
	if (memfd < 0 || ftruncate(memfd, PROCESS_PAGE_SIZE) ||
	    pwrite(memfd, native_argv, sizeof(native_argv), 0) !=
	    sizeof(native_argv))
		return EXIT_FAILURE;
	exec_argv = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_PRIVATE, memfd, 0);
	close(memfd);
	if (exec_argv == MAP_FAILED)
		return EXIT_FAILURE;
	if (madvise(exec_argv, PROCESS_PAGE_SIZE, MADV_DONTNEED))
		return EXIT_FAILURE;
	syscall(SYS_execve, native_argv[0], exec_argv, NULL);
	return EXIT_FAILURE;
}

static bool wait_for_exec(pid_t child)
{
	int elapsed;
	int status;

	for (elapsed = 0; elapsed < EXEC_TIMEOUT_US; elapsed += WAIT_STEP_US) {
		pid_t waited = waitpid(child, &status, WNOHANG);

		if (waited == child)
			return WIFEXITED(status) &&
			       WEXITSTATUS(status) == EXIT_SUCCESS;
		if (waited < 0)
			return false;
		usleep(WAIT_STEP_US);
	}
	kill(child, SIGKILL);
	ksft_exit_fail_msg("4K-to-native exec timed out\n");
}

static int run_compat_owner(void)
{
	int i;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(TRANSITION_COUNT);

	for (i = 0; i < TRANSITION_COUNT; i++) {
		pid_t child = fork();

		if (child < 0)
			ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
		if (!child)
			_exit(exec_native_with_cold_argv());
		ksft_test_result(wait_for_exec(child),
				 "4K-to-native exec iteration %d\n", i + 1);
	}
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], "--owner", NULL);
	if (argc == 2 && !strcmp(mode, "--owner"))
		return run_compat_owner();
	if (argc == 2 && !strcmp(mode, "--native"))
		return sysconf(_SC_PAGESIZE) == NATIVE_PAGE_SIZE ?
			EXIT_SUCCESS : EXIT_FAILURE;
	return EXIT_FAILURE;
}
