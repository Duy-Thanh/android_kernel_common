// SPDX-License-Identifier: GPL-2.0
/*
 * UFFDIO_MOVE of a single swapped-out 4K slice to a destination with a
 * different position inside its 16K window must not keep the source's slice
 * number: after the destination is swapped back in, every 4K page of a
 * compat anonymous mapping must map the slice that matches its own address,
 * so that filling a neighbouring page in the same window (by a fault or by
 * UFFDIO_COPY) can never overwrite or alias the moved data.
 *
 * Either the kernel refuses the cross-slice move (any errno), or the moved
 * page keeps its contents and stays independent of its neighbours.  Swap
 * must be active; the test skips when the source cannot be swapped out.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "ppps_tuple_test.h"

#define SRC_FIRST	0x21
#define COPY_BYTE	0x7e
#define TOUCH_BYTE	0x6d

static long uffd_move(int uffd, void *dst, void *src, unsigned long len)
{
	struct uffdio_move move = {
		.dst = (uintptr_t)dst,
		.src = (uintptr_t)src,
		.len = len,
	};

	if (ioctl(uffd, UFFDIO_MOVE, &move))
		return move.move > 0 ? move.move : -errno;
	return move.move;
}

static bool swap_out(unsigned char *base)
{
	unsigned int i;

	if (!pageout_reaches_swap(base, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE))
		return false;
	for (i = 0; i < PPPS_SLICES; i++)
		if (!ppps_page_swapped(base + i * PROCESS_PAGE_SIZE))
			return false;
	return true;
}

struct setup {
	unsigned char *src_res, *src, *dst_res, *dst;
	int uffd;
};

static bool prepare(struct setup *s)
{
	s->src = map_exact_edge(NATIVE_PAGE_SIZE, &s->src_res);
	s->dst = map_exact_edge(NATIVE_PAGE_SIZE, &s->dst_res);
	if (s->src == MAP_FAILED || s->dst == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	fill_tuple(s->src, SRC_FIRST);
	s->uffd = uffd_open_register(s->dst, NATIVE_PAGE_SIZE);
	if (s->uffd < 0)
		ksft_exit_skip("userfaultfd: %s\n", strerror(errno));
	return swap_out(s->src);
}

static void teardown(struct setup *s)
{
	close(s->uffd);
	munmap(s->src_res, NATIVE_PAGE_SIZE + 2 * NATIVE_PAGE_SIZE);
	munmap(s->dst_res, NATIVE_PAGE_SIZE + 2 * NATIVE_PAGE_SIZE);
}

/*
 * Move one swapped slice to slice @dst_slice of the destination window.
 * Returns the source slice that moved, -1 when the kernel refused every
 * cross-slice move, -2 on setup trouble.
 */
static int move_one_slice(struct setup *s, unsigned int *dst_slice)
{
	unsigned int e;
	long ret;

	for (e = 0; e < PPPS_SLICES; e++) {
		*dst_slice = (e + 1) % PPPS_SLICES;
		ret = uffd_move(s->uffd, s->dst + *dst_slice * PROCESS_PAGE_SIZE,
				s->src + e * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE);
		if (ret == PROCESS_PAGE_SIZE)
			return e;
		ksft_print_msg("move slice %u -> %u: %s\n", e, *dst_slice,
			       ret < 0 ? strerror(-ret) : "short");
	}
	return -1;
}

/* Same-slice control: is any swapped slice movable at all? */
static bool same_slice_move_works(void)
{
	struct setup s;
	unsigned int e;
	bool moved = false;

	if (!prepare(&s)) {
		teardown(&s);
		return false;
	}
	for (e = 0; e < PPPS_SLICES && !moved; e++)
		moved = uffd_move(s.uffd, s.dst + e * PROCESS_PAGE_SIZE,
				  s.src + e * PROCESS_PAGE_SIZE,
				  PROCESS_PAGE_SIZE) == PROCESS_PAGE_SIZE;
	teardown(&s);
	return moved;
}

/*
 * After the move, drop the rest of the source, swap the moved page back in
 * and fill the destination's slice @e with @fill.  The moved page must keep
 * the source slice's contents.
 */
static int run_case(bool use_copy)
{
	unsigned char *moved, *other, page[PROCESS_PAGE_SIZE];
	unsigned int dst_slice;
	struct setup s;
	int e;
	bool ok = true;

	if (!prepare(&s)) {
		teardown(&s);
		ksft_print_msg("could not swap the source window out\n");
		return KSFT_SKIP;
	}
	e = move_one_slice(&s, &dst_slice);
	if (e < 0) {
		teardown(&s);
		if (!same_slice_move_works()) {
			ksft_print_msg("no swapped slice is movable at all\n");
			return KSFT_SKIP;
		}
		ksft_print_msg("cross-slice move of a swapped slice is refused\n");
		return KSFT_PASS;
	}
	ksft_print_msg("moved swapped source slice %d to destination slice %u\n",
		       e, dst_slice);
	madvise(s.src, NATIVE_PAGE_SIZE, MADV_DONTNEED);

	moved = s.dst + dst_slice * PROCESS_PAGE_SIZE;
	/* Swap the moved page back in. */
	if (!all_bytes_are(moved, PROCESS_PAGE_SIZE, SRC_FIRST + e)) {
		ksft_print_msg("moved page reads %#x, expected %#x\n", moved[0],
			       SRC_FIRST + e);
		ok = false;
	}

	/* Fill the destination slice that has the source slice's number. */
	other = s.dst + e * PROCESS_PAGE_SIZE;
	if (use_copy) {
		memset(page, COPY_BYTE, sizeof(page));
		if (uffd_copy(s.uffd, other, page, PROCESS_PAGE_SIZE) !=
		    PROCESS_PAGE_SIZE) {
			ksft_print_msg("UFFDIO_COPY: %s\n", strerror(errno));
			ok = false;
		}
	} else {
		struct uffdio_range range = {
			.start = (uintptr_t)s.dst, .len = NATIVE_PAGE_SIZE,
		};

		if (ioctl(s.uffd, UFFDIO_UNREGISTER, &range))
			ksft_exit_fail_msg("UFFDIO_UNREGISTER: %s\n",
					   strerror(errno));
		other[0] = TOUCH_BYTE;
	}

	if (!all_bytes_are(moved, PROCESS_PAGE_SIZE, SRC_FIRST + e)) {
		ksft_print_msg("after filling destination slice %d the moved page reads %#x %#x, expected %#x\n",
			       e, moved[0], moved[1], SRC_FIRST + e);
		ok = false;
	}
	if (use_copy ? !all_bytes_are(other, PROCESS_PAGE_SIZE, COPY_BYTE) :
		       other[0] != TOUCH_BYTE ||
		       !all_bytes_are(other + 1, PROCESS_PAGE_SIZE - 1, 0)) {
		ksft_print_msg("filled destination slice %d reads %#x %#x\n", e,
			       other[0], other[1]);
		ok = false;
	}
	teardown(&s);
	return ok ? KSFT_PASS : KSFT_FAIL;
}

static void report(int ret, const char *what)
{
	if (ret == KSFT_SKIP)
		ksft_test_result_skip("%s\n", what);
	else
		ksft_test_result(ret == KSFT_PASS, "%s\n", what);
}

static int run_test(void)
{
	ksft_print_header();
	ksft_set_plan(2);
	report(run_case(false),
	       "a neighbouring fault does not clobber a moved swapped slice");
	report(run_case(true),
	       "UFFDIO_COPY to a neighbour does not clobber a moved swapped slice");
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
