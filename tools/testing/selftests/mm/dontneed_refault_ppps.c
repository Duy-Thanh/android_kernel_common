// SPDX-License-Identifier: GPL-2.0
/*
 * A discarded 4K anonymous page of a compat process must read as zero when
 * it is next touched, even while the other 4K slices of its 16K native folio
 * stay mapped.  The first touch is a write: a read would map the shared zero
 * page and let a later COW hide stale data, so every case except the read
 * control faults the slice in for writing.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "ppps_tuple_test.h"

#define TOUCH_BYTE	0xa5

enum discard {
	DISCARD_DONTNEED,
	DISCARD_REMAP,
};

/* Offset of the first byte in @slice after the touched one that is not 0. */
static long stale_offset(const unsigned char *slice)
{
	unsigned long i;

	for (i = 1; i < PROCESS_PAGE_SIZE; i++)
		if (slice[i])
			return i;
	return -1;
}

static bool discard_slice(unsigned char *slice, enum discard how)
{
	void *p;

	if (how == DISCARD_DONTNEED)
		return !madvise(slice, PROCESS_PAGE_SIZE, MADV_DONTNEED);
	if (munmap(slice, PROCESS_PAGE_SIZE))
		return false;
	p = mmap(slice, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	return p == slice;
}

/*
 * Fill a tuple, discard @victim (and @also, if different), touch @victim
 * and check it is zero apart from the touched byte.  The untouched slices
 * must keep their contents.
 */
static void check_refault(unsigned int victim, unsigned int also,
			  enum discard how, bool read_first, const char *name)
{
	unsigned char *reservation = NULL, *base, *slice;
	uint64_t pfn[PPPS_SLICES];
	bool passed = false;
	unsigned int i;
	long stale;

	base = map_aligned(NATIVE_PAGE_SIZE, &reservation);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("%s: mmap failed\n", name);
		return;
	}
	slice = base + victim * PROCESS_PAGE_SIZE;
	fill_tuple(base, 0x31);
	if (!discard_slice(slice, how) ||
	    (also != victim &&
	     !discard_slice(base + also * PROCESS_PAGE_SIZE, how))) {
		ksft_test_result_fail("%s: discard failed\n", name);
		goto out;
	}

	if (read_first && *(volatile unsigned char *)slice)
		ksft_print_msg("%s: read fault returned stale data\n", name);
	slice[0] = TOUCH_BYTE;

	stale = stale_offset(slice);
	passed = slice[0] == TOUCH_BYTE && stale < 0;
	if (stale >= 0)
		ksft_print_msg("%s: slice %u byte %ld is 0x%02x, want 0\n",
			       name, victim, stale, slice[stale]);
	for (i = 0; i < PPPS_SLICES; i++) {
		const unsigned char *p = base + i * PROCESS_PAGE_SIZE;

		if (i == victim || i == also)
			continue;
		if (p[0] != 0x31 + i || p[PROCESS_PAGE_SIZE - 1] != 0x31 + i) {
			ksft_print_msg("%s: untouched slice %u changed\n",
				       name, i);
			passed = false;
		}
	}
	if (!passed && also == victim && read_pfns(base, pfn))
		ksft_print_msg("%s: native PFNs %#llx %#llx %#llx %#llx\n",
			       name, (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
	ksft_test_result(passed, "%s\n", name);
out:
	munmap(reservation, 2 * NATIVE_PAGE_SIZE);
}

static int run_test(void)
{
	static const char *const dontneed_names[PPPS_SLICES] = {
		"write after DONTNEED of slice 0 is zero-filled",
		"write after DONTNEED of slice 1 is zero-filled",
		"write after DONTNEED of slice 2 is zero-filled",
		"write after DONTNEED of slice 3 is zero-filled",
	};
	unsigned int i;

	ksft_print_header();
	ksft_set_plan(PPPS_SLICES + 3);

	for (i = 0; i < PPPS_SLICES; i++)
		check_refault(i, i, DISCARD_DONTNEED, false, dontneed_names[i]);
	check_refault(1, 2, DISCARD_DONTNEED, false,
		      "write after DONTNEED of two slices is zero-filled");
	check_refault(1, 1, DISCARD_REMAP, false,
		      "write after munmap and MAP_FIXED remap is zero-filled");
	check_refault(1, 1, DISCARD_DONTNEED, true,
		      "read then write after DONTNEED is zero-filled");

	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
