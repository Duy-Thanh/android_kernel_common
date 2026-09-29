// SPDX-License-Identifier: GPL-2.0
/*
 * The four 4K slices of a compat process's anonymous native page share one
 * folio (a "packed tuple") and stay consistent in smaps, across fork COW,
 * mprotect, mlock, pageout, MADV_DONTNEED/MADV_FREE, UFFDIO_MOVE,
 * process_vm_readv/writev and mremap.
 */
#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "ppps_tuple_test.h"

#define DISCARD_GROUPS	8
#define DEPACK_RACE_ATTEMPTS 1000
#define MOVE_RACE_ITERATIONS 128
#define MOVE_BENCH_TUPLES 256
#define MOVE_BENCH_LENGTH (MOVE_BENCH_TUPLES * NATIVE_PAGE_SIZE)

static void populate(unsigned char *base)
{
	fill_tuple(base, 0x31);
}

static bool verify(const unsigned char *base)
{
	return check_tuple(base, 0x31);
}

static bool verify_groups(const unsigned char *base, unsigned int groups)
{
	unsigned int i;

	for (i = 0; i < groups; i++)
		if (!verify(base + i * NATIVE_PAGE_SIZE))
			return false;
	return true;
}

static bool packed_smaps_matches(const unsigned char *base,
				 unsigned long pss,
				 unsigned long shared_dirty,
				 unsigned long private_dirty)
{
	unsigned long value;

	return ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &value) &&
		value == NATIVE_PAGE_SIZE &&
		ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Pss", &value) &&
		value == pss &&
		ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Pss_Dirty", &value) &&
		value == pss &&
		ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Shared_Dirty", &value) &&
		value == shared_dirty &&
		ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Private_Dirty", &value) &&
		value == private_dirty;
}

static bool fork_smaps_shares_tuple(unsigned char *base)
{
	int child_ready[2];
	int child_done[2];
	char byte;
	pid_t child;
	int status;
	bool passed;

	if (pipe(child_ready))
		return false;
	if (pipe(child_done)) {
		close(child_ready[0]);
		close(child_ready[1]);
		return false;
	}
	child = fork();
	if (child < 0) {
		close(child_ready[0]);
		close(child_ready[1]);
		close(child_done[0]);
		close(child_done[1]);
		return false;
	}
	if (!child) {
		close(child_ready[0]);
		close(child_done[1]);
		if (write(child_ready[1], "R", 1) != 1 ||
		    read(child_done[0], &byte, 1) != 1)
			_exit(1);
		_exit(verify(base) ? 0 : 1);
	}

	close(child_ready[1]);
	close(child_done[0]);
	passed = read(child_ready[0], &byte, 1) == 1 &&
		 packed_smaps_matches(base, NATIVE_PAGE_SIZE / 2, NATIVE_PAGE_SIZE, 0);
	if (write(child_done[1], "D", 1) != 1)
		passed = false;
	close(child_ready[0]);
	close(child_done[1]);
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		passed = false;
	return passed;
}

static bool read_moved_pfns(const unsigned char *dst,
			    const unsigned char *src, uint64_t pfn[PPPS_SLICES])
{
	const unsigned char *address[PPPS_SLICES] = {
		dst,
		src + PROCESS_PAGE_SIZE,
		src + 2 * PROCESS_PAGE_SIZE,
		src + 3 * PROCESS_PAGE_SIZE,
	};

	return read_pagemap_pfns(address, pfn);
}

static bool split_one_three_pfns(const uint64_t pfn[PPPS_SLICES])
{
	return pfn[0] && pfn[1] && pfn[0] != pfn[1] &&
		pfn[1] == pfn[2] && pfn[1] == pfn[3];
}

static bool shifted_two_tuple_pfns(const uint64_t pfn[PPPS_SLICES])
{
	return pfn[0] && pfn[0] == pfn[1] && pfn[0] == pfn[2] &&
		pfn[3] && pfn[3] != pfn[0];
}

static bool verify_process_vm_pattern(const unsigned char *base,
				      unsigned char first)
{
	unsigned long offset;

	for (offset = 0; offset < NATIVE_PAGE_SIZE; offset++)
		if (base[offset] != first + offset / PROCESS_PAGE_SIZE)
			return false;
	return true;
}

static void process_vm_rw_preserves_packed_slices(unsigned char *base,
						  bool *read_ok, bool *write_ok)
{
	unsigned char local[NATIVE_PAGE_SIZE];
	struct iovec local_iov = {
		.iov_base = local,
		.iov_len = sizeof(local),
	};
	struct iovec remote_iov = {
		.iov_base = base,
		.iov_len = NATIVE_PAGE_SIZE,
	};
	uint64_t pfn[PPPS_SLICES];
	unsigned int i;
	bool packed;

	populate(base);
	packed = read_pfns(base, pfn) && same_pfn(pfn);
	memset(local, 0, sizeof(local));
	*read_ok = packed &&
		process_vm_readv(getpid(), &local_iov, 1, &remote_iov, 1, 0) ==
		NATIVE_PAGE_SIZE && verify(local);

	for (i = 0; i < PPPS_SLICES; i++)
		memset(local + i * PROCESS_PAGE_SIZE, 0x61 + i, PROCESS_PAGE_SIZE);
	*write_ok = packed &&
		process_vm_writev(getpid(), &local_iov, 1, &remote_iov, 1, 0) ==
		NATIVE_PAGE_SIZE && verify_process_vm_pattern(base, 0x61);
}

static bool mremap_handles_packed_tuple(bool same_slice)
{
	unsigned char *src_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *dst_base;
	unsigned char *src;
	unsigned char *dst;
	uint64_t pfn[PPPS_SLICES];
	void *moved;
	bool passed = false;

	src = map_aligned(2 * NATIVE_PAGE_SIZE, &src_reservation);
	dst_base = map_aligned(3 * NATIVE_PAGE_SIZE, &dst_reservation);
	if (src == MAP_FAILED || dst_base == MAP_FAILED)
		goto out;
	dst = dst_base + (same_slice ? 0 : PROCESS_PAGE_SIZE);
	populate(src);
	if (!read_pfns(src, pfn) || !same_pfn(pfn))
		goto out;

	moved = mremap(src, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED, dst);
	passed = moved == dst && verify(dst) && read_pfns(dst, pfn) &&
		 (same_slice ? same_pfn(pfn) : shifted_two_tuple_pfns(pfn));
	if (!passed && moved == dst && !same_slice && read_pfns(dst, pfn))
		ksft_print_msg("cross-slice mremap PFNs: %llx %llx %llx %llx\n",
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
out:
	if (src_reservation)
		munmap(src_reservation, 3 * NATIVE_PAGE_SIZE);
	if (dst_reservation)
		munmap(dst_reservation, 4 * NATIVE_PAGE_SIZE);
	return passed;
}

static bool fork_cow_preserves_parent(unsigned char *base)
{
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		return false;
	if (!child) {
		base[PROCESS_PAGE_SIZE] = 0xa5;
		if (base[PROCESS_PAGE_SIZE] != 0xa5 ||
		    base[0] != 0x31 || base[2 * PROCESS_PAGE_SIZE] != 0x33 ||
		    base[3 * PROCESS_PAGE_SIZE] != 0x34)
			_exit(1);
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child)
		return false;
	return WIFEXITED(status) && !WEXITSTATUS(status) && verify(base);
}

struct concurrent_write_args {
	unsigned char *base;
	unsigned int slice;
	atomic_bool *start;
};

static void *write_zero_tuple_slice(void *data)
{
	struct concurrent_write_args *args = data;

	while (!atomic_load_explicit(args->start, memory_order_acquire))
		sched_yield();
	memset(args->base + args->slice * PROCESS_PAGE_SIZE, 0x31 + args->slice,
	       PROCESS_PAGE_SIZE);
	return NULL;
}

static bool concurrent_zero_tuple_promotion(unsigned char *base)
{
	struct concurrent_write_args args[PPPS_SLICES];
	pthread_t threads[PPPS_SLICES];
	atomic_bool start = false;
	unsigned char value;
	unsigned int created = 0;
	bool passed = true;

	value = base[2 * PROCESS_PAGE_SIZE];
	if (value)
		return false;

	for (created = 0; created < PPPS_SLICES; created++) {
		args[created].base = base;
		args[created].slice = created;
		args[created].start = &start;
		if (pthread_create(&threads[created], NULL,
				   write_zero_tuple_slice, &args[created])) {
			passed = false;
			break;
		}
	}
	atomic_store_explicit(&start, true, memory_order_release);
	while (created)
		passed &= !pthread_join(threads[--created], NULL);

	return passed && verify(base);
}

struct depack_race_args {
	unsigned char *base;
	atomic_bool ready;
	atomic_bool stop;
};

static void *write_during_depack(void *data)
{
	struct depack_race_args *args = data;
	atomic_uchar *target = (atomic_uchar *)args->base;
	unsigned char value = 0;

	atomic_store_explicit(&args->ready, true, memory_order_release);
	while (!atomic_load_explicit(&args->stop, memory_order_acquire))
		atomic_store_explicit(target, ++value, memory_order_relaxed);
	return NULL;
}

static bool concurrent_partial_madvise(void)
{
	unsigned int attempt;

	for (attempt = 0; attempt < DEPACK_RACE_ATTEMPTS; attempt++) {
		struct depack_race_args args;
		unsigned char *reservation;
		unsigned char *base;
		pthread_t thread;
		bool passed;
		int error;
		int ret;

		base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
		if (base == MAP_FAILED)
			return false;
		populate(base);
		args.base = base;
		atomic_init(&args.ready, false);
		atomic_init(&args.stop, false);
		ret = pthread_create(&thread, NULL, write_during_depack, &args);
		if (ret) {
			munmap(reservation, 3 * NATIVE_PAGE_SIZE);
			return false;
		}
		while (!atomic_load_explicit(&args.ready, memory_order_acquire))
			sched_yield();
		for (ret = 0; ret < 64; ret++)
			sched_yield();

		errno = 0;
		ret = madvise(base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			      MADV_DONTNEED);
		error = errno;
		atomic_store_explicit(&args.stop, true, memory_order_release);
		passed = !pthread_join(thread, NULL) && !ret &&
			 base[PROCESS_PAGE_SIZE] == 0 &&
			 base[2 * PROCESS_PAGE_SIZE] == 0x33 &&
			 base[3 * PROCESS_PAGE_SIZE] == 0x34;
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
		if (!passed) {
			errno = error;
			return false;
		}
	}
	return true;
}

struct move_thread_args {
	struct uffdio_move move;
	pthread_barrier_t *barrier;
	int result;
	int error;
	int uffd;
};

static void *move_tuple_thread(void *data)
{
	struct move_thread_args *args = data;

	pthread_barrier_wait(args->barrier);
	args->result = ioctl(args->uffd, UFFDIO_MOVE, &args->move);
	args->error = errno;
	return NULL;
}

static bool userfaultfd_tuple_move_races(int uffd)
{
	struct uffdio_register reg = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	const size_t length = MOVE_RACE_ITERATIONS * NATIVE_PAGE_SIZE;
	unsigned char *src_a_reservation = NULL;
	unsigned char *src_b_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *src_a = MAP_FAILED;
	unsigned char *src_b = MAP_FAILED;
	unsigned char *dst = MAP_FAILED;
	uint64_t pfn[PPPS_SLICES];
	bool passed = true;

	src_a = map_aligned(length, &src_a_reservation);
	src_b = map_aligned(length, &src_b_reservation);
	dst = map_aligned(length, &dst_reservation);
	if (src_a == MAP_FAILED || src_b == MAP_FAILED || dst == MAP_FAILED) {
		passed = false;
		goto out;
	}
	for (unsigned int i = 0; i < MOVE_RACE_ITERATIONS; i++) {
		fill_tuple(src_a + i * NATIVE_PAGE_SIZE, 0x31);
		fill_tuple(src_b + i * NATIVE_PAGE_SIZE, 0x71);
	}
	reg.range.start = (uintptr_t)dst;
	reg.range.len = length;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg)) {
		passed = false;
		goto out;
	}

	for (unsigned int i = 0; i < MOVE_RACE_ITERATIONS; i++) {
		unsigned char *cur_a = src_a + i * NATIVE_PAGE_SIZE;
		unsigned char *cur_b = src_b + i * NATIVE_PAGE_SIZE;
		unsigned char *cur_dst = dst + i * NATIVE_PAGE_SIZE;
		struct move_thread_args args[2] = {
			{
				.move = {
					.dst = (uintptr_t)cur_dst,
					.src = (uintptr_t)cur_a,
					.len = NATIVE_PAGE_SIZE,
				},
				.uffd = uffd,
			},
			{
				.move = {
					.dst = (uintptr_t)cur_dst,
					.src = (uintptr_t)cur_b,
					.len = NATIVE_PAGE_SIZE,
				},
				.uffd = uffd,
			},
		};
		pthread_barrier_t barrier;
		pthread_t threads[2];
		unsigned char *loser;
		unsigned char first;
		bool a_won, b_won;

		if (!read_pfns(cur_a, pfn) || !same_pfn(pfn) ||
		    !read_pfns(cur_b, pfn) || !same_pfn(pfn) ||
		    pthread_barrier_init(&barrier, NULL, 3)) {
			passed = false;
			break;
		}
		args[0].barrier = &barrier;
		args[1].barrier = &barrier;
		if (pthread_create(&threads[0], NULL, move_tuple_thread, &args[0]) ||
		    pthread_create(&threads[1], NULL, move_tuple_thread, &args[1]))
			ksft_exit_fail_msg("UFFDIO_MOVE race thread creation failed\n");
		pthread_barrier_wait(&barrier);
		pthread_join(threads[0], NULL);
		pthread_join(threads[1], NULL);
		pthread_barrier_destroy(&barrier);

		a_won = !args[0].result && args[0].move.move == NATIVE_PAGE_SIZE &&
			 args[1].result == -1 && args[1].error == EEXIST &&
			 args[1].move.move == -EEXIST;
		b_won = !args[1].result && args[1].move.move == NATIVE_PAGE_SIZE &&
			 args[0].result == -1 && args[0].error == EEXIST &&
			 args[0].move.move == -EEXIST;
		loser = a_won ? cur_b : cur_a;
		first = a_won ? 0x31 : 0x71;
		if ((!a_won && !b_won) || !check_tuple(cur_dst, first) ||
		    !read_pfns(cur_dst, pfn) || !same_pfn(pfn) ||
		    !check_tuple(loser, a_won ? 0x71 : 0x31) ||
		    !read_pfns(loser, pfn) || !same_pfn(pfn)) {
			ksft_print_msg("MOVE race %u: ret %d/%d moved %lld/%lld errno %d/%d\n",
				       i, args[0].result, args[1].result,
				       (long long)args[0].move.move,
				       (long long)args[1].move.move,
				       args[0].error, args[1].error);
			passed = false;
			break;
		}
	}

out:
	if (src_a_reservation && src_a != MAP_FAILED)
		munmap(src_a_reservation, length + NATIVE_PAGE_SIZE);
	if (src_b_reservation && src_b != MAP_FAILED)
		munmap(src_b_reservation, length + NATIVE_PAGE_SIZE);
	if (dst_reservation && dst != MAP_FAILED)
		munmap(dst_reservation, length + NATIVE_PAGE_SIZE);
	return passed;
}

struct uffd_move_result {
	bool supported;
	bool register_preserved;
	bool move_preserved;
	bool cross_slice_move_preserved;
	bool unregistered_source_preserved;
	bool tuple_move_preserved;
	bool tuple_move_rss_preserved;
	bool tuple_move_partial_preserved;
	bool tuple_move_hole_fallback;
	bool tuple_move_race_preserved;
	unsigned long tuple_rss_before;
	unsigned long tuple_rss_after;
};

static void unregister_range(int uffd, unsigned char *base, size_t length)
{
	struct uffdio_range range = {
		.start = (uintptr_t)base,
		.len = length,
	};

	ioctl(uffd, UFFDIO_UNREGISTER, &range);
}

static bool userfaultfd_full_tuple_move(int uffd, bool *rss_preserved,
					unsigned long *rss_before,
					unsigned long *rss_after)
{
	struct uffdio_register reg = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_move move = {};
	unsigned char *src_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *src = MAP_FAILED;
	unsigned char *dst = MAP_FAILED;
	uint64_t (*before)[PPPS_SLICES] = NULL;
	uint64_t after[PPPS_SLICES];
	unsigned long dst_anon, src_anon;
	unsigned long mapping_anon_before;
	unsigned long mapping_anon_after;
	bool passed = false;
	unsigned int i;
	int move_ret;

	*rss_preserved = false;
	*rss_before = 0;
	*rss_after = 0;
	before = calloc(MOVE_BENCH_TUPLES, sizeof(*before));
	if (!before) {
		ksft_print_msg("full MOVE setup: calloc failed\n");
		goto out;
	}
	src = map_exact_edge(MOVE_BENCH_LENGTH, &src_reservation);
	dst = map_exact_edge(MOVE_BENCH_LENGTH, &dst_reservation);
	if (src == MAP_FAILED || dst == MAP_FAILED) {
		ksft_print_msg("full MOVE setup: mmap failed: %s\n",
			       strerror(errno));
		goto out;
	}
	for (i = 0; i < MOVE_BENCH_TUPLES; i++) {
		populate(src + i * NATIVE_PAGE_SIZE);
		if (!read_pfns(src + i * NATIVE_PAGE_SIZE, before[i]) ||
		    !same_pfn(before[i])) {
			ksft_print_msg("full MOVE setup: tuple %u is not packed\n", i);
			goto out;
		}
	}
	reg.range.start = (uintptr_t)dst;
	reg.range.len = MOVE_BENCH_LENGTH;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg)) {
		ksft_print_msg("full MOVE setup: register failed: %s\n",
			       strerror(errno));
		goto out;
	}
	if (!ppps_smaps_bytes(src, MOVE_BENCH_LENGTH, "Anonymous",
			      &src_anon) ||
	    !ppps_smaps_bytes(dst, MOVE_BENCH_LENGTH, "Anonymous",
			      &dst_anon)) {
		ksft_print_msg("full MOVE setup: Anonymous read failed\n");
		goto out;
	}
	mapping_anon_before = src_anon + dst_anon;
	move.dst = (uintptr_t)dst;
	move.src = (uintptr_t)src;
	move.len = MOVE_BENCH_LENGTH;
	move_ret = ioctl(uffd, UFFDIO_MOVE, &move);
	passed = !move_ret && move.move == MOVE_BENCH_LENGTH;
	if (!passed)
		ksft_print_msg("full MOVE ioctl failed: ret=%d moved=%lld errno=%s\n",
			       move_ret, move.move, strerror(errno));
	if (!ppps_smaps_bytes(src, MOVE_BENCH_LENGTH, "Anonymous",
			      &src_anon) ||
	    !ppps_smaps_bytes(dst, MOVE_BENCH_LENGTH, "Anonymous",
			      &dst_anon)) {
		ksft_print_msg("full MOVE result: Anonymous read failed\n");
		goto out;
	}
	mapping_anon_after = src_anon + dst_anon;
	for (i = 0; passed && i < MOVE_BENCH_TUPLES; i++) {
		passed = verify(dst + i * NATIVE_PAGE_SIZE) &&
			 read_pfns(dst + i * NATIVE_PAGE_SIZE, after) &&
			 same_pfn(after);
		if (!passed)
			ksft_print_msg("full MOVE verify failed at tuple %u\n", i);
	}
	*rss_before = mapping_anon_before;
	*rss_after = mapping_anon_after;
	*rss_preserved = *rss_before == *rss_after;
	unregister_range(uffd, dst, MOVE_BENCH_LENGTH);
out:
	if (src_reservation && src != MAP_FAILED)
		munmap(src_reservation, MOVE_BENCH_LENGTH + 2 * NATIVE_PAGE_SIZE);
	if (dst_reservation && dst != MAP_FAILED)
		munmap(dst_reservation, MOVE_BENCH_LENGTH + 2 * NATIVE_PAGE_SIZE);
	free(before);
	return passed;
}

static bool userfaultfd_partial_tuple_move(int uffd)
{
	struct uffdio_register reg = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_copy copy = {};
	struct uffdio_move move = {};
	unsigned char conflict[PROCESS_PAGE_SIZE];
	unsigned char *src_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *src = MAP_FAILED;
	unsigned char *dst = MAP_FAILED;
	uint64_t pfn[PPPS_SLICES];
	long moved;
	bool passed = false;
	int ret;

	src = map_aligned(2 * NATIVE_PAGE_SIZE, &src_reservation);
	dst = map_aligned(2 * NATIVE_PAGE_SIZE, &dst_reservation);
	if (src == MAP_FAILED || dst == MAP_FAILED)
		goto out;
	populate(src);
	if (!read_pfns(src, pfn) || !same_pfn(pfn))
		goto out;
	reg.range.start = (uintptr_t)dst;
	reg.range.len = NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg))
		goto out;

	memset(conflict, 0x91, sizeof(conflict));
	copy.dst = (uintptr_t)(dst + PROCESS_PAGE_SIZE);
	copy.src = (uintptr_t)conflict;
	copy.len = PROCESS_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_COPY, &copy) || copy.copy != PROCESS_PAGE_SIZE)
		goto unregister;
	move.dst = (uintptr_t)dst;
	move.src = (uintptr_t)src;
	move.len = NATIVE_PAGE_SIZE;
	errno = 0;
	ret = ioctl(uffd, UFFDIO_MOVE, &move);
	moved = move.move > 0 ? move.move : 0;
	passed = ret == -1 && (moved == 0 || moved == PROCESS_PAGE_SIZE) &&
		 dst[PROCESS_PAGE_SIZE] == 0x91;
	if (passed && moved == PROCESS_PAGE_SIZE)
		passed = dst[0] == 0x31 &&
			 src[PROCESS_PAGE_SIZE] == 0x32 &&
			 src[2 * PROCESS_PAGE_SIZE] == 0x33 &&
			 src[3 * PROCESS_PAGE_SIZE] == 0x34;
	else if (passed)
		passed = verify(src);
	if (!passed)
		ksft_print_msg("busy MOVE: ret=%d errno=%d moved=%lld\n",
			       ret, errno, (long long)move.move);
unregister:
	unregister_range(uffd, dst, NATIVE_PAGE_SIZE);
out:
	if (src_reservation && src != MAP_FAILED)
		munmap(src_reservation, 3 * NATIVE_PAGE_SIZE);
	if (dst_reservation && dst != MAP_FAILED)
		munmap(dst_reservation, 3 * NATIVE_PAGE_SIZE);
	return passed;
}

static bool userfaultfd_holey_tuple_move(int uffd)
{
	struct uffdio_register reg = {
		.mode = UFFDIO_REGISTER_MODE_MISSING,
	};
	struct uffdio_move move = {};
	unsigned char *src_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *src = MAP_FAILED;
	unsigned char *dst = MAP_FAILED;
	uint64_t pfn[PPPS_SLICES];
	bool passed = false;
	int ret;

	src = map_aligned(2 * NATIVE_PAGE_SIZE, &src_reservation);
	dst = map_aligned(2 * NATIVE_PAGE_SIZE, &dst_reservation);
	if (src == MAP_FAILED || dst == MAP_FAILED)
		goto out;
	populate(src);
	if (!read_pfns(src, pfn) || !same_pfn(pfn) ||
	    madvise(src + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
		    MADV_DONTNEED))
		goto out;

	reg.range.start = (uintptr_t)dst;
	reg.range.len = NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg))
		goto out;
	move.dst = (uintptr_t)dst;
	move.src = (uintptr_t)src;
	move.len = NATIVE_PAGE_SIZE;
	errno = 0;
	ret = ioctl(uffd, UFFDIO_MOVE, &move);
	passed = ret == -1 && errno == EAGAIN &&
		 move.move == PROCESS_PAGE_SIZE && !src[0] && dst[0] == 0x31 &&
		 src[2 * PROCESS_PAGE_SIZE] == 0x33 &&
		 src[3 * PROCESS_PAGE_SIZE] == 0x34;
	if (!passed) {
		ksft_print_msg("holey MOVE: ret=%d errno=%d moved=%lld\n",
			       ret, errno, (long long)move.move);
		ksft_print_msg("source slices: %02x/%02x/%02x\n", src[0],
			       src[2 * PROCESS_PAGE_SIZE],
			       src[3 * PROCESS_PAGE_SIZE]);
	}
	unregister_range(uffd, dst, NATIVE_PAGE_SIZE);
out:
	if (src_reservation && src != MAP_FAILED)
		munmap(src_reservation, 3 * NATIVE_PAGE_SIZE);
	if (dst_reservation && dst != MAP_FAILED)
		munmap(dst_reservation, 3 * NATIVE_PAGE_SIZE);
	return passed;
}

static struct uffd_move_result userfaultfd_move_depacks(void)
{
	struct uffd_move_result result = {};
	struct uffdio_register uffd_register = {};
	struct uffdio_range uffd_unregister = {};
	struct uffdio_move uffd_move = {};
	struct uffdio_api uffd_api = {};
	unsigned char *reservation;
	unsigned char *src_reservation = NULL;
	unsigned char *dst_reservation = NULL;
	unsigned char *register_base;
	unsigned char *base;
	unsigned char *dst;
	unsigned char *src_unregistered = MAP_FAILED;
	unsigned char *dst_registered = MAP_FAILED;
	uint64_t pfn[PPPS_SLICES];
	bool initial_packed;
	int uffd = -1;

	register_base = map_aligned(5 * NATIVE_PAGE_SIZE, &reservation);
	if (register_base == MAP_FAILED)
		return result;
	base = register_base + NATIVE_PAGE_SIZE;
	dst = register_base + 3 * NATIVE_PAGE_SIZE;
	populate(base);
	initial_packed = read_pfns(base, pfn) && same_pfn(pfn);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
	if (uffd < 0)
		goto out;
	uffd_api.api = UFFD_API;
	if (ioctl(uffd, UFFDIO_API, &uffd_api) ||
	    !(uffd_api.features & UFFD_FEATURE_MOVE))
		goto out;
	result.supported = true;

	uffd_register.range.start = (uintptr_t)register_base;
	uffd_register.range.len = 5 * NATIVE_PAGE_SIZE;
	uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffd_register))
		goto out;

	result.register_preserved = initial_packed && read_pfns(base, pfn) &&
				      same_pfn(pfn) && verify(base);

	uffd_move.dst = (uintptr_t)dst;
	uffd_move.src = (uintptr_t)base;
	uffd_move.len = PROCESS_PAGE_SIZE;
	if (!ioctl(uffd, UFFDIO_MOVE, &uffd_move) &&
	    uffd_move.move == PROCESS_PAGE_SIZE && dst[0] == 0x31 &&
	    base[PROCESS_PAGE_SIZE] == 0x32 &&
	    base[2 * PROCESS_PAGE_SIZE] == 0x33 &&
	    base[3 * PROCESS_PAGE_SIZE] == 0x34 &&
	    read_moved_pfns(dst, base, pfn) && split_one_three_pfns(pfn))
		result.move_preserved = true;

	uffd_unregister.start = (uintptr_t)register_base;
	uffd_unregister.len = 5 * NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_UNREGISTER, &uffd_unregister))
		result.move_preserved = false;
	else if (base[0] || base[PROCESS_PAGE_SIZE] != 0x32 ||
		 base[2 * PROCESS_PAGE_SIZE] != 0x33 ||
		 base[3 * PROCESS_PAGE_SIZE] != 0x34)
		result.move_preserved = false;

	if (madvise(base, NATIVE_PAGE_SIZE, MADV_DONTNEED) ||
	    madvise(dst, NATIVE_PAGE_SIZE, MADV_DONTNEED))
		goto out;
	populate(base);
	uffd_register.range.start = (uintptr_t)register_base;
	uffd_register.range.len = 5 * NATIVE_PAGE_SIZE;
	uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffd_register))
		goto out;
	memset(&uffd_move, 0, sizeof(uffd_move));
	uffd_move.dst = (uintptr_t)(dst + PROCESS_PAGE_SIZE);
	uffd_move.src = (uintptr_t)base;
	uffd_move.len = PROCESS_PAGE_SIZE;
	if (!ioctl(uffd, UFFDIO_MOVE, &uffd_move) &&
	    uffd_move.move == PROCESS_PAGE_SIZE &&
	    dst[PROCESS_PAGE_SIZE] == 0x31 &&
	    base[PROCESS_PAGE_SIZE] == 0x32 &&
	    base[2 * PROCESS_PAGE_SIZE] == 0x33 &&
	    base[3 * PROCESS_PAGE_SIZE] == 0x34)
		result.cross_slice_move_preserved = true;
	uffd_unregister.start = (uintptr_t)register_base;
	uffd_unregister.len = 5 * NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_UNREGISTER, &uffd_unregister))
		result.cross_slice_move_preserved = false;

	src_unregistered = map_aligned(2 * NATIVE_PAGE_SIZE, &src_reservation);
	dst_registered = map_aligned(2 * NATIVE_PAGE_SIZE, &dst_reservation);
	if (src_unregistered == MAP_FAILED || dst_registered == MAP_FAILED)
		goto out;
	populate(src_unregistered);
	if (!read_pfns(src_unregistered, pfn) || !same_pfn(pfn))
		goto out;

	memset(&uffd_register, 0, sizeof(uffd_register));
	uffd_register.range.start = (uintptr_t)dst_registered;
	uffd_register.range.len = NATIVE_PAGE_SIZE;
	uffd_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffd_register))
		goto out;

	memset(&uffd_move, 0, sizeof(uffd_move));
	uffd_move.dst = (uintptr_t)dst_registered;
	uffd_move.src = (uintptr_t)src_unregistered;
	uffd_move.len = PROCESS_PAGE_SIZE;
	if (!ioctl(uffd, UFFDIO_MOVE, &uffd_move) &&
	    uffd_move.move == PROCESS_PAGE_SIZE && dst_registered[0] == 0x31 &&
	    src_unregistered[PROCESS_PAGE_SIZE] == 0x32 &&
	    src_unregistered[2 * PROCESS_PAGE_SIZE] == 0x33 &&
	    src_unregistered[3 * PROCESS_PAGE_SIZE] == 0x34 &&
	    read_moved_pfns(dst_registered, src_unregistered, pfn) &&
	    split_one_three_pfns(pfn))
		result.unregistered_source_preserved = true;

	uffd_unregister.start = (uintptr_t)dst_registered;
	uffd_unregister.len = NATIVE_PAGE_SIZE;
	if (ioctl(uffd, UFFDIO_UNREGISTER, &uffd_unregister) ||
	    src_unregistered[0])
		result.unregistered_source_preserved = false;

	result.tuple_move_preserved =
		userfaultfd_full_tuple_move(uffd,
					    &result.tuple_move_rss_preserved,
					    &result.tuple_rss_before,
					    &result.tuple_rss_after);
	result.tuple_move_partial_preserved =
		userfaultfd_partial_tuple_move(uffd);
	result.tuple_move_hole_fallback =
		userfaultfd_holey_tuple_move(uffd);
	result.tuple_move_race_preserved =
		userfaultfd_tuple_move_races(uffd);

out:
	if (uffd >= 0)
		close(uffd);
	if (src_reservation && src_unregistered != MAP_FAILED)
		munmap(src_reservation, 3 * NATIVE_PAGE_SIZE);
	if (dst_reservation && dst_registered != MAP_FAILED)
		munmap(dst_reservation, 3 * NATIVE_PAGE_SIZE);
	munmap(reservation, 6 * NATIVE_PAGE_SIZE);
	return result;
}

static int run_test(void)
{
	struct uffd_move_result uffd_move;
	unsigned char *reservation;
	unsigned char *base;
	uint64_t pfn[PPPS_SLICES];
	uint64_t pfn_before[PPPS_SLICES];
	unsigned long rss = 0;
	unsigned long rss_before = 0;
	unsigned long rss_after = 0;
	unsigned long swap = 0;
	bool have_pfns;
	bool passed;
	bool swapped;
	bool process_vm_read_ok;
	bool process_vm_write_ok;
	unsigned char value = 1;

	ksft_print_header();
	ksft_set_plan(31);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	ksft_test_result(base != MAP_FAILED, "map anonymous test range\n");
	if (base == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));
	populate(base);
	if (!verify(base))
		ksft_exit_fail_msg("writable packed slices corrupted\n");
	if (madvise(base, NATIVE_PAGE_SIZE, MADV_DONTNEED) ||
	    !ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss_before))
		rss_before = ~0UL;
	populate(base);
	if (!ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss_after))
		rss_after = 0;
	have_pfns = read_pfns(base, pfn);
	ksft_test_result(have_pfns && same_pfn(pfn),
			 "four adjacent PTEs map one native folio\n");
	ksft_test_result(rss_before != ~0UL && rss_after >= rss_before &&
			 rss_after - rss_before == NATIVE_PAGE_SIZE,
			 "packed tuple accounts one native RSS page (%lu bytes)\n",
			 rss_after - rss_before);
	ksft_test_result(packed_smaps_matches(base, NATIVE_PAGE_SIZE, 0,
					      NATIVE_PAGE_SIZE),
			 "smaps counts one private packed tuple at full PSS\n");
	ksft_test_result(fork_smaps_shares_tuple(base),
			 "smaps shares packed tuple PSS once across fork\n");

	ksft_test_result(fork_cow_preserves_parent(base),
			 "fork COW preserves all four slices\n");

	passed = !mprotect(base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, PROT_READ) &&
		 read_pfns(base, pfn) && same_pfn(pfn) && verify(base) &&
		 !mprotect(base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			   PROT_READ | PROT_WRITE);
	ksft_test_result(passed,
			 "partial mprotect preserves a shared tuple folio\n");
	munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		populate(base);
	passed = base != MAP_FAILED && !mlock(base, NATIVE_PAGE_SIZE) &&
		 read_pfns(base, pfn) && same_pfn(pfn) && verify(base) &&
		 !munlock(base, NATIVE_PAGE_SIZE);
	ksft_test_result(passed, "mlock preserves a shared tuple folio\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	passed = base != MAP_FAILED && concurrent_zero_tuple_promotion(base);
	ksft_test_result(passed,
			 "concurrent zero-slice writes preserve tuple data\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		populate(base);
	passed = base != MAP_FAILED && read_pfns(base, pfn_before) &&
		 same_pfn(pfn_before) &&
		 pageout_reaches_swap(base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) &&
		 verify(base) && read_pfns(base, pfn) && same_pfn(pfn) &&
		 ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss) &&
		 rss == NATIVE_PAGE_SIZE &&
		 ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Swap", &swap) && !swap;
	ksft_test_result(passed,
			 "pageout swaps and restores one packed tuple\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		populate(base);
	passed = base != MAP_FAILED &&
		 !madvise(base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, MADV_DONTNEED) &&
		 base[0] == 0x31 && base[PROCESS_PAGE_SIZE] == 0 &&
		 base[2 * PROCESS_PAGE_SIZE] == 0x33 &&
		 base[3 * PROCESS_PAGE_SIZE] == 0x34;
	ksft_test_result(passed,
			 "partial MADV_DONTNEED affects only requested slice\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	ksft_test_result(concurrent_partial_madvise(),
			 "partial MADV_DONTNEED excludes concurrent write faults\n");

	uffd_move = userfaultfd_move_depacks();
	if (!uffd_move.supported) {
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
		ksft_test_result_skip("UFFDIO_MOVE is unavailable\n");
	} else {
		ksft_test_result(uffd_move.register_preserved,
				 "userfaultfd registration preserves tuple folios\n");
		ksft_test_result(uffd_move.move_preserved,
				 "single-slice UFFDIO_MOVE preserves tuple data\n");
		ksft_test_result(uffd_move.cross_slice_move_preserved,
				 "cross-slice UFFDIO_MOVE preserves tuple data\n");
		ksft_test_result(uffd_move.unregistered_source_preserved,
				 "UFFDIO_MOVE depacks an unregistered source tuple\n");
		ksft_test_result(uffd_move.tuple_move_preserved,
				 "full UFFDIO_MOVE preserves one packed tuple\n");
		ksft_test_result(uffd_move.tuple_move_rss_preserved,
				 "4MiB UFFDIO_MOVE Anonymous %lu -> %lu bytes\n",
				 uffd_move.tuple_rss_before,
				 uffd_move.tuple_rss_after);
		ksft_test_result(uffd_move.tuple_move_partial_preserved,
				 "busy destination preserves MOVE data\n");
		ksft_test_result(uffd_move.tuple_move_hole_fallback,
				 "holey tuple MOVE falls back with partial progress\n");
		ksft_test_result(uffd_move.tuple_move_race_preserved,
				 "concurrent full-tuple MOVEs publish one winner\n");
	}

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		value = base[2 * PROCESS_PAGE_SIZE];
	passed = base != MAP_FAILED && !value && only_slice_present(base, 2);
	ksft_test_result(passed,
			 "one read fault installs only its zero-page PTE\n");
	if (base != MAP_FAILED)
		populate(base);
	passed = base != MAP_FAILED && verify(base) && read_pfns(base, pfn) &&
		 same_pfn(pfn);
	ksft_test_result(passed,
			 "first write promotes the zero tuple to one packed folio\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		populate(base);
	passed = base != MAP_FAILED && read_pfns(base, pfn_before) &&
		 same_pfn(pfn_before) &&
		 !madvise(base, NATIVE_PAGE_SIZE, MADV_FREE) && verify(base) &&
		 read_pfns(base, pfn) && same_pfn(pfn) &&
		 !memcmp(pfn_before, pfn, sizeof(pfn)) &&
		 ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss) &&
		 rss == NATIVE_PAGE_SIZE;
	ksft_test_result(passed,
			 "full MADV_FREE preserves a complete packed tuple\n");
	if (base != MAP_FAILED)
		populate(base);
	/*
	 * PAGEOUT can first clear recency and then convert a redirtied lazyfree
	 * folio back to swap-backed before a following attempt can swap it.
	 */
	swapped = pageout_reaches_swap(base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) ||
		  pageout_reaches_swap(base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) ||
		  pageout_reaches_swap(base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE);
	if (!swapped &&
	    ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss) &&
	    ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Swap", &swap)) {
		bool preserved = verify(base);

		ksft_print_msg("redirtied lazyfree tuple stayed Rss=%lu Swap=%lu "
			       "data=%s\n", rss, swap,
			       preserved ? "preserved" : "corrupt");
	}
	passed = passed && verify(base) && read_pfns(base, pfn) && same_pfn(pfn) &&
		 ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Rss", &rss) &&
		 rss == NATIVE_PAGE_SIZE &&
		 ppps_smaps_bytes(base, NATIVE_PAGE_SIZE, "Swap", &swap) && !swap;
	ksft_test_result(passed,
			 "pageout preserves a redirtied MADV_FREE tuple\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	base = map_aligned(DISCARD_GROUPS * NATIVE_PAGE_SIZE, &reservation);
	if (base != MAP_FAILED)
		for (unsigned int i = 0; i < DISCARD_GROUPS; i++)
			populate(base + i * NATIVE_PAGE_SIZE);
	passed = base != MAP_FAILED &&
		 !madvise(base, DISCARD_GROUPS * NATIVE_PAGE_SIZE, MADV_PAGEOUT) &&
		 verify_groups(base, DISCARD_GROUPS) &&
		 ppps_smaps_bytes(base, DISCARD_GROUPS * NATIVE_PAGE_SIZE,
				  "Rss", &rss) &&
		 rss == DISCARD_GROUPS * NATIVE_PAGE_SIZE &&
		 ppps_smaps_bytes(base, DISCARD_GROUPS * NATIVE_PAGE_SIZE,
				  "Swap", &swap) && !swap &&
		 !madvise(base, NATIVE_PAGE_SIZE, MADV_FREE) &&
		 verify_groups(base, DISCARD_GROUPS);
	ksft_test_result(passed,
			 "MADV_FREE keeps resident packed tuples intact\n");
	passed = passed && !munmap(base + 4 * NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE) &&
		 verify(base + 5 * NATIVE_PAGE_SIZE);
	ksft_test_result(passed,
			 "munmap preserves an adjacent resident packed tuple\n");
	if (base != MAP_FAILED)
		munmap(reservation, (DISCARD_GROUPS + 1) * NATIVE_PAGE_SIZE);

	base = map_aligned(2 * NATIVE_PAGE_SIZE, &reservation);
	process_vm_read_ok = false;
	process_vm_write_ok = false;
	if (base != MAP_FAILED)
		process_vm_rw_preserves_packed_slices(base, &process_vm_read_ok,
						      &process_vm_write_ok);
	ksft_test_result(process_vm_read_ok,
			 "process_vm_readv preserves all packed slices\n");
	ksft_test_result(process_vm_write_ok,
			 "process_vm_writev preserves all packed slices\n");
	if (base != MAP_FAILED)
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);

	ksft_test_result(mremap_handles_packed_tuple(true),
			 "same-slice mremap preserves a complete packed tuple\n");
	ksft_test_result(mremap_handles_packed_tuple(false),
			 "cross-slice mremap regroups into two tuple folios\n");

	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
