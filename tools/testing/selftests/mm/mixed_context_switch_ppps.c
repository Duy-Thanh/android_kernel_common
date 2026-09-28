// SPDX-License-Identifier: GPL-2.0
/*
 * A native (16K) parent and a 4K compat child pinned to the same CPU hand
 * off through a shared futex for many rounds, each writing and re-reading
 * every 4K slice of a private native page, without memory or futex errors.
 */
#define _GNU_SOURCE

#include <linux/futex.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "kselftest_ppps.h"

#define DEFAULT_ROUNDS 50000

struct shared_state {
	int turn;
	int ready;
	int child_error;
	long child_page_size;
	unsigned long parent_rounds;
	unsigned long child_rounds;
};

static int futex_wait(int *address, int value)
{
	int ret;

	do {
		ret = syscall(SYS_futex, address, FUTEX_WAIT, value,
			      NULL, NULL, 0);
	} while (ret && errno == EINTR);
	return ret && errno != EAGAIN ? -1 : 0;
}

static int futex_wake(int *address)
{
	return syscall(SYS_futex, address, FUTEX_WAKE, 1, NULL, NULL, 0) < 0 ?
		-1 : 0;
}

static int wait_for_value(int *address, int value)
{
	for (;;) {
		int current = __atomic_load_n(address, __ATOMIC_ACQUIRE);

		if (current == value)
			return 0;
		if (futex_wait(address, current))
			return -1;
	}
}

static int pin_to_cpu_zero(void)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	return sched_setaffinity(0, sizeof(set), &set);
}

static int exercise_mapping(unsigned char *mapping, unsigned long round)
{
	unsigned int slice;

	for (slice = 0; slice < NATIVE_PAGE_SIZE / PROCESS_PAGE_SIZE; slice++) {
		unsigned char value = (unsigned char)(round + 17 * slice);

		mapping[slice * PROCESS_PAGE_SIZE] = value;
		if (mapping[slice * PROCESS_PAGE_SIZE] != value)
			return -1;
	}
	return 0;
}

static int run_child(int fd, unsigned long rounds)
{
	struct shared_state *shared;
	unsigned char *mapping;
	unsigned long round;
	int error = 0;

	shared = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, 0);
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED || mapping == MAP_FAILED || pin_to_cpu_zero())
		error = errno ? errno : EFAULT;
	if (!error && sysconf(_SC_PAGESIZE) != PROCESS_PAGE_SIZE)
		error = EINVAL;
	shared->child_page_size = sysconf(_SC_PAGESIZE);
	shared->child_error = error;
	__atomic_store_n(&shared->ready, 1, __ATOMIC_RELEASE);
	futex_wake(&shared->ready);
	if (error)
		return EXIT_FAILURE;

	for (round = 0; round < rounds; round++) {
		if (wait_for_value(&shared->turn, 1) ||
		    exercise_mapping(mapping, round)) {
			shared->child_error = errno ? errno : EIO;
			return EXIT_FAILURE;
		}
		shared->child_rounds = round + 1;
		__atomic_store_n(&shared->turn, 0, __ATOMIC_RELEASE);
		if (futex_wake(&shared->turn)) {
			shared->child_error = errno;
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int exec_compat_child(int fd, unsigned long rounds)
{
	char fd_arg[16];
	char rounds_arg[32];

	snprintf(fd_arg, sizeof(fd_arg), "%d", fd);
	snprintf(rounds_arg, sizeof(rounds_arg), "%lu", rounds);
	ppps_execl(true, NULL, "--child", fd_arg, rounds_arg, NULL);
	return EXIT_FAILURE;
}

static int run_parent(unsigned long rounds)
{
	struct shared_state *shared;
	unsigned char *mapping;
	int child_status = 0;
	unsigned long round;
	int fd;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(9);
	ksft_test_result(sysconf(_SC_PAGESIZE) == NATIVE_PAGE_SIZE,
			 "parent uses native 16K pages\n");
	fd = syscall(SYS_memfd_create, "mixed-context-switch", 0);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		ksft_exit_fail_msg("memfd setup failed: %s\n", strerror(errno));
	shared = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED, fd, 0);
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ksft_test_result(shared != MAP_FAILED && mapping != MAP_FAILED,
			 "map native private and cross-process shared pages\n");
	if (shared == MAP_FAILED || mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	memset(shared, 0, NATIVE_PAGE_SIZE);
	if (pin_to_cpu_zero())
		ksft_exit_fail_msg("sched_setaffinity failed: %s\n",
				   strerror(errno));
	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork failed: %s\n", strerror(errno));
	if (!child)
		_exit(exec_compat_child(fd, rounds));
	ksft_test_result(!wait_for_value(&shared->ready, 1),
			 "start a same-CPU 4K compat peer\n");
	ksft_test_result(shared->child_page_size == PROCESS_PAGE_SIZE &&
			 !shared->child_error,
			 "peer enters 4K compat mode (%ld, error=%d)\n",
			 shared->child_page_size, shared->child_error);

	for (round = 0; round < rounds; round++) {
		if (wait_for_value(&shared->turn, 0) ||
		    exercise_mapping(mapping, round))
			break;
		shared->parent_rounds = round + 1;
		__atomic_store_n(&shared->turn, 1, __ATOMIC_RELEASE);
		if (futex_wake(&shared->turn))
			break;
	}
	ksft_test_result(round == rounds,
			 "native peer completes %lu memory-checked turns\n",
			 rounds);
	waitpid(child, &child_status, 0);
	ksft_test_result(WIFEXITED(child_status) &&
			 WEXITSTATUS(child_status) == EXIT_SUCCESS,
			 "4K peer exits cleanly\n");
	ksft_test_result(shared->parent_rounds == rounds &&
			 shared->child_rounds == rounds,
			 "both peers complete every handoff (%lu/%lu)\n",
			 shared->parent_rounds, shared->child_rounds);
	ksft_test_result(!shared->child_error,
			 "mixed process handoff reports no memory or futex error\n");
	ksft_test_result(!exercise_mapping(mapping, rounds),
			 "native mapping remains writable after mixed switching\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);
	unsigned long rounds = DEFAULT_ROUNDS;

	if (mode && argc == 4 && !strcmp(mode, "--child"))
		return run_child(atoi(argv[2]), strtoul(argv[3], NULL, 0));
	if (mode)
		return EXIT_FAILURE;
	if (argc == 2)
		rounds = strtoul(argv[1], NULL, 0);
	if ((argc != 1 && argc != 2) || !rounds)
		return EXIT_FAILURE;
	return run_parent(rounds);
}
