// SPDX-License-Identifier: GPL-2.0
/*
 * Shared futexes of a 4K compat process key on the file page, not the
 * virtual address: a waiter on one alias of a 4K file slice is woken through
 * a second alias mapped at a different native-page offset, and a tagged
 * anonymous address is accepted by FUTEX_WAKE.
 */
#define _GNU_SOURCE

#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>

#include "kselftest_ppps.h"

#define POINTER_TAG 0xb4UL
#define WAKE_RETRIES 1000

struct alias_mapping {
	void *reservation;
	uint32_t *word;
};

struct waiter_state {
	uint32_t *word;
	atomic_int ready;
	atomic_int done;
	int result;
	int error;
};

static int futex_wait(uint32_t *word, uint32_t expected,
		      const struct timespec *timeout)
{
	return syscall(SYS_futex, word, FUTEX_WAIT, expected, timeout,
		       NULL, 0);
}

static int futex_wake(uint32_t *word, int count)
{
	return syscall(SYS_futex, word, FUTEX_WAKE, count, NULL, NULL, 0);
}

static struct alias_mapping map_alias(int fd, unsigned long native_offset)
{
	struct alias_mapping alias = {
		.reservation = MAP_FAILED,
		.word = MAP_FAILED,
	};
	uintptr_t aligned;
	void *reservation;
	void *mapping;

	reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return alias;

	aligned = ((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
		  ~(NATIVE_PAGE_SIZE - 1);
	mapping = mmap((void *)(aligned + native_offset), PROCESS_PAGE_SIZE,
		       PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
	if (mapping == MAP_FAILED) {
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
		return alias;
	}

	alias.reservation = reservation;
	alias.word = mapping;
	return alias;
}

static void unmap_alias(struct alias_mapping *alias)
{
	if (alias->reservation != MAP_FAILED)
		munmap(alias->reservation, 3 * NATIVE_PAGE_SIZE);
}

static void *waiter_thread(void *arg)
{
	struct waiter_state *state = arg;
	struct timespec timeout = {
		.tv_sec = 2,
	};

	atomic_store_explicit(&state->ready, 1, memory_order_release);
	state->result = futex_wait(state->word, 0, &timeout);
	state->error = state->result < 0 ? errno : 0;
	atomic_store_explicit(&state->done, 1, memory_order_release);
	return NULL;
}

static int run_test(void)
{
	struct alias_mapping alias_a = { .reservation = MAP_FAILED };
	struct alias_mapping alias_b = { .reservation = MAP_FAILED };
	struct waiter_state waiter = {};
	uint32_t *anonymous_word;
	char path[] = "/tmp/futex-shared-XXXXXX";
	struct timespec delay = { .tv_nsec = 1000000 };
	pthread_t thread;
	int wake_error = 0;
	int wake_result = 0;
	int thread_created = 0;
	int fd;
	int i;

	ksft_print_header();
	ksft_set_plan(6);

	anonymous_word = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (anonymous_word == MAP_FAILED)
		ksft_exit_fail_msg("anonymous mapping failed: %s\n",
				   strerror(errno));
#ifdef __aarch64__
	if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0))
		ksft_exit_fail_msg("enable tagged-address ABI: %s\n",
				   strerror(errno));
	*anonymous_word = 0;
	errno = 0;
	wake_result = futex_wake((uint32_t *)((uintptr_t)anonymous_word |
				      (POINTER_TAG << 56)), 1);
	ksft_test_result(wake_result == 0,
			 "tagged anonymous shared wake succeeds (ret=%d errno=%d)\n",
			 wake_result, errno);
#else
	ksft_test_result_skip("tagged-address futex test requires arm64\n");
#endif
	munmap(anonymous_word, PROCESS_PAGE_SIZE);

	fd = mkstemp(path);
	if (fd >= 0)
		unlink(path);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		ksft_exit_fail_msg("backing file setup failed: %s\n",
				   strerror(errno));

	alias_a = map_alias(fd, PROCESS_PAGE_SIZE);
	alias_b = map_alias(fd, 2 * PROCESS_PAGE_SIZE);
	ksft_test_result(alias_a.word != MAP_FAILED &&
			 alias_b.word != MAP_FAILED,
			 "map shared file slice at two native-page offsets\n");
	if (alias_a.word == MAP_FAILED || alias_b.word == MAP_FAILED)
		ksft_exit_fail_msg("alias mapping failed: %s\n", strerror(errno));

	*alias_a.word = 0;
	ksft_test_result(*alias_b.word == 0,
			 "aliases reference the same futex word\n");

	waiter.word = alias_a.word;
	thread_created = pthread_create(&thread, NULL, waiter_thread, &waiter) == 0;
	ksft_test_result(thread_created, "create futex waiter thread\n");
	if (!thread_created)
		ksft_exit_fail_msg("pthread_create failed\n");

	while (!atomic_load_explicit(&waiter.ready, memory_order_acquire))
		sched_yield();

	for (i = 0; i < WAKE_RETRIES; i++) {
		wake_result = futex_wake(alias_b.word, 1);
		if (wake_result == 1)
			break;
		if (wake_result < 0) {
			wake_error = errno;
			break;
		}
		if (atomic_load_explicit(&waiter.done, memory_order_acquire))
			break;
		nanosleep(&delay, NULL);
	}

	pthread_join(thread, NULL);
	ksft_test_result(wake_result == 1,
			 "wake waiter through second alias (ret=%d errno=%d)\n",
			 wake_result, wake_error);
	ksft_test_result(waiter.result == 0,
			 "shared waiter returns after alias wake (ret=%d errno=%d)\n",
			 waiter.result, waiter.error);

	unmap_alias(&alias_a);
	unmap_alias(&alias_b);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
