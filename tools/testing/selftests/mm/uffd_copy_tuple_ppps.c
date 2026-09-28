// SPDX-License-Identifier: GPL-2.0
/*
 * PPPS: UFFDIO_COPY must install whole native tuples, not singletons.
 *
 * A compat (4K) process runs on 4K page tables while the kernel's native
 * PAGE_SIZE is 16K, so every anonymous PTE is backed by a slice of a native
 * folio.  mfill_atomic() walks the request in MM_PAGE_SIZE(mm) (4K) steps and
 * allocates one native folio per step, so a UFFDIO_COPY-installed page used to
 * become a "singleton": a 16K folio carrying exactly one 4K PTE.  Multi-page
 * UFFDIO_COPY calls carry most of the bytes installed by ART's userfaultfd GC,
 * so that inflated the collector's anonymous footprint 4x.
 *
 * K1 ("tuple-aware mfill") adds a fast path: when a private anonymous COPY is
 * 16K-aligned and at least 16K long, one folio is copied whole and mapped
 * behind all four compat PTEs under a single pte lock.
 *
 * What this test asserts, from a compat process:
 *
 *   1. a 16K UFFDIO_COPY reports copy == 16K,
 *   2. all four destination 4K pages read back the bytes that were sent,
 *   3. RssAnon grows by exactly 16 kB across that ioctl -- 64 kB is the
 *      singleton (pre-K1) signature, since RssAnon counts native folios,
 *   4. /proc/self/pagemap reports the same pfn for the four consecutive 4K
 *      pages, i.e. they really share one native folio (needs CAP_SYS_ADMIN;
 *      skipped when pfns read back as zero),
 *   5. the fallback keeps the old stop-at-first-EEXIST semantics: a 16K COPY
 *      across a tuple whose second slice is already populated must report
 *      copy == 4096 and must not disturb the populated slice,
 *   6. a source missing fault exercises the mmap-lock-outside retry and still
 *      installs one correct packed tuple,
 *   7. two concurrent COPYs to the same empty tuple produce exactly one 16K
 *      winner and one -EEXIST loser, without mixing their source bytes.
 *
 * Run it on a pre-K1 kernel and leg 3 fails with 65536 (and leg 4 with four
 * distinct pfns); legs 5 and 6 pass on both, which is the point of them.
 */
#define _GNU_SOURCE

#include <pthread.h>

#include "ppps_tuple_test.h"

#ifndef MREMAP_DONTUNMAP
#define MREMAP_DONTUNMAP 4
#endif

#define RACE_ITERATIONS	128UL
#define FIXED_TUPLES	5UL
#define REGION_LEN	((FIXED_TUPLES + RACE_ITERATIONS) * NATIVE_PAGE_SIZE)

struct copy_args {
	pthread_barrier_t *barrier;
	void *dst;
	void *src;
	unsigned long len;
	long copied;
	int uffd;
};

static bool read_page_sizes(uintptr_t address, unsigned long *kernel_size,
			    unsigned long *mmu_size)
{
	char *line = NULL;
	size_t line_size = 0;
	bool in_mapping = false;
	FILE *file;

	*kernel_size = 0;
	*mmu_size = 0;
	file = fopen("/proc/self/smaps", "re");
	if (!file)
		return false;
	while (getline(&line, &line_size, file) >= 0) {
		unsigned long start, end, size_kb;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_mapping = address >= start && address < end;
			continue;
		}
		if (!in_mapping)
			continue;
		if (sscanf(line, "KernelPageSize: %lu kB", &size_kb) == 1)
			*kernel_size = size_kb * 1024;
		else if (sscanf(line, "MMUPageSize: %lu kB", &size_kb) == 1)
			*mmu_size = size_kb * 1024;
		if (*kernel_size && *mmu_size)
			break;
	}
	free(line);
	fclose(file);
	return *kernel_size && *mmu_size;
}

static bool has_ppps_geometry(void)
{
	unsigned long kernel_size, mmu_size;
	unsigned char *probe;
	bool parsed;

	probe = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (probe == MAP_FAILED)
		return false;
	*probe = 0x5a;
	parsed = read_page_sizes((uintptr_t)probe, &kernel_size, &mmu_size);
	munmap(probe, NATIVE_PAGE_SIZE);
	if (!parsed)
		return false;

	return kernel_size == NATIVE_PAGE_SIZE && mmu_size == PROCESS_PAGE_SIZE;
}

static void *copy_thread(void *data)
{
	struct copy_args *args = data;

	if (args->barrier)
		pthread_barrier_wait(args->barrier);
	args->copied = uffd_copy(args->uffd, args->dst, args->src, args->len);
	return NULL;
}

static bool test_source_retry(int dst_uffd, unsigned char *dst)
{
	unsigned char resident;
	unsigned char *source;
	long copied;
	bool cold;

	/* The first source PTE is absent, forcing the mmap-lock-outside retry. */
	source = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (source == MAP_FAILED)
		return false;
	resident = 1;
	cold = !mincore(source, PROCESS_PAGE_SIZE, &resident) && !(resident & 1);
	copied = cold ? uffd_copy(dst_uffd, dst, source, NATIVE_PAGE_SIZE) : -1;
	cold = cold && copied == NATIVE_PAGE_SIZE &&
	       !memcmp(dst, source, NATIVE_PAGE_SIZE);
	munmap(source, NATIVE_PAGE_SIZE);
	return cold;
}

static bool race_one_tuple(int uffd, unsigned char *dst,
			   unsigned char *source_a, unsigned char *source_b,
			   unsigned long iteration)
{
	pthread_barrier_t barrier;
	struct copy_args args[2] = {
		{
			.dst = dst,
			.src = source_a,
			.len = NATIVE_PAGE_SIZE,
			.copied = -1,
			.uffd = uffd,
		},
		{
			.dst = dst,
			.src = source_b,
			.len = NATIVE_PAGE_SIZE,
			.copied = -1,
			.uffd = uffd,
		},
	};
	pthread_t threads[2];
	unsigned char *winner;
	bool a_won, b_won;

	if (pthread_barrier_init(&barrier, NULL, 3))
		return false;
	args[0].barrier = &barrier;
	args[1].barrier = &barrier;
	if (pthread_create(&threads[0], NULL, copy_thread, &args[0]) ||
	    pthread_create(&threads[1], NULL, copy_thread, &args[1]))
		ksft_exit_fail_msg("race thread creation failed\n");
	pthread_barrier_wait(&barrier);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	pthread_barrier_destroy(&barrier);

	a_won = args[0].copied == NATIVE_PAGE_SIZE && args[1].copied == -EEXIST;
	b_won = args[1].copied == NATIVE_PAGE_SIZE && args[0].copied == -EEXIST;
	winner = a_won ? source_a : source_b;
	if ((!a_won && !b_won) || memcmp(dst, winner, NATIVE_PAGE_SIZE)) {
		ksft_print_msg("race %lu: copy results %ld/%ld\n", iteration,
			       args[0].copied, args[1].copied);
		return false;
	}
	return true;
}

static bool test_same_tuple_race(int uffd, unsigned char *race_region,
				 unsigned char *source_a,
				 unsigned char *source_b)
{
	for (unsigned long i = 0; i < RACE_ITERATIONS; i++) {
		if (!race_one_tuple(uffd, race_region + i * NATIVE_PAGE_SIZE,
				    source_a, source_b, i))
			return false;
	}
	return true;
}

/*
 * ART's userfaultfd GC mixes UFFDIO_ZEROPAGE and 4K UFFDIO_COPY slices in one
 * tuple, then mremap()s the live part of the space -- a 4K, not 16K, multiple
 * -- with MREMAP_DONTUNMAP.  @tuples spans two tuples: the first is moved
 * whole and the second only partly, so the whole one is regrouped purely
 * because of its zero-page slice.  Every moved slice must keep its bytes.
 */
static bool test_zeropage_mremap(int uffd, unsigned char *tuples,
				 const unsigned char *src)
{
	struct uffdio_zeropage zp = {
		.range = {
			.start = (uintptr_t)(tuples + 2 * PROCESS_PAGE_SIZE),
			.len = PROCESS_PAGE_SIZE,
		},
	};
	const size_t moved_len = (PPPS_SLICES + 1) * PROCESS_PAGE_SIZE;
	unsigned char *reservation, *dst;
	unsigned int i;
	bool ok;

	if (ioctl(uffd, UFFDIO_ZEROPAGE, &zp) || zp.zeropage != (long)PROCESS_PAGE_SIZE)
		return false;
	for (i = 0; i <= PPPS_SLICES; i++) {
		if (i == 2)
			continue;
		if (uffd_copy(uffd, tuples + i * PROCESS_PAGE_SIZE,
			      (void *)(src + i * PROCESS_PAGE_SIZE),
			      PROCESS_PAGE_SIZE) != (long)PROCESS_PAGE_SIZE)
			return false;
	}

	reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return false;
	dst = (unsigned char *)(((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
				~(uintptr_t)(NATIVE_PAGE_SIZE - 1));
	if (mremap(tuples, moved_len, moved_len,
		   MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP, dst) != dst) {
		munmap(reservation, 3 * NATIVE_PAGE_SIZE);
		return false;
	}
	ok = true;
	for (i = 0; i <= PPPS_SLICES; i++)
		ok = ok && all_bytes_are(dst + i * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
					 i == 2 ? 0 : src[i * PROCESS_PAGE_SIZE]);
	munmap(reservation, 3 * NATIVE_PAGE_SIZE);
	return ok;
}

static int run_test(void)
{
	unsigned char *reservation, *region, *src;
	unsigned char *tuple_a, *tuple_b, *tuple_retry, *tuple_move, *race_region;
	long rss_before, rss_after, copied;
	uint64_t pfn[PPPS_SLICES];
	unsigned int i;
	bool ok;
	int uffd;

	ksft_print_header();

	if (!has_ppps_geometry())
		ksft_exit_skip("need native-16K/compat-4K PPPS geometry\n");

	/* Oversize the reservation so the region can start 16K-aligned. */
	reservation = mmap(NULL, REGION_LEN + NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		ksft_exit_fail_perror("mmap(region)");
	region = (unsigned char *)(((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
				   ~(uintptr_t)(NATIVE_PAGE_SIZE - 1));
	tuple_a = region;
	tuple_b = region + NATIVE_PAGE_SIZE;
	tuple_retry = region + 2 * NATIVE_PAGE_SIZE;
	tuple_move = region + 3 * NATIVE_PAGE_SIZE;
	race_region = region + FIXED_TUPLES * NATIVE_PAGE_SIZE;

	/*
	 * The source is faulted in and filled up front: an unpopulated source
	 * would fault (and grow RssAnon) inside the measured ioctl.
	 */
	src = mmap(NULL, 4 * NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (src == MAP_FAILED)
		ksft_exit_fail_perror("mmap(src)");
	for (i = 0; i < 2 * PPPS_SLICES; i++)
		memset(src + i * PROCESS_PAGE_SIZE, 0xa0 + i, PROCESS_PAGE_SIZE);
	memset(src + 2 * NATIVE_PAGE_SIZE, 0x5a, NATIVE_PAGE_SIZE);
	memset(src + 3 * NATIVE_PAGE_SIZE, 0xa5, NATIVE_PAGE_SIZE);

	uffd = uffd_open_register(region, REGION_LEN);
	if (uffd < 0)
		ksft_exit_skip("userfaultfd MISSING registration failed: %s\n",
			       strerror(errno));
	ksft_set_plan(8);

	/* Warm the status read so the measured pair allocates nothing. */
	if (rss_anon_bytes() < 0)
		ksft_exit_fail_msg("cannot read RssAnon from /proc/self/status\n");

	rss_before = rss_anon_bytes();
	copied = uffd_copy(uffd, tuple_a, src, NATIVE_PAGE_SIZE);
	rss_after = rss_anon_bytes();

	ksft_test_result(copied == (long)NATIVE_PAGE_SIZE,
			 "16K UFFDIO_COPY installed the whole tuple (copy=%ld, want %lu)\n",
			 copied, NATIVE_PAGE_SIZE);

	ok = copied == (long)NATIVE_PAGE_SIZE;
	for (i = 0; ok && i < PPPS_SLICES; i++)
		ok = all_bytes_are(tuple_a + i * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				   (unsigned char)(0xa0 + i));
	ksft_test_result(ok, "all %u destination slices carry the copied bytes\n",
			 (unsigned int)PPPS_SLICES);

	if (rss_before < 0 || rss_after < 0)
		ksft_test_result_skip("RssAnon unreadable\n");
	else
		ksft_test_result(rss_after - rss_before == (long)NATIVE_PAGE_SIZE,
				 "RssAnon grew by one native page: %ld kB (packed=%lu, singletons=%lu)\n",
				 (rss_after - rss_before) / 1024, NATIVE_PAGE_SIZE / 1024,
				 PPPS_SLICES * NATIVE_PAGE_SIZE / 1024);

	if (!read_pfns(tuple_a, pfn)) {
		ksft_test_result_skip("pagemap: destination slices not all present\n");
	} else if (!pfn[0]) {
		ksft_test_result_skip("pagemap: pfns hidden (need CAP_SYS_ADMIN)\n");
	} else {
		ok = true;
		for (i = 1; i < PPPS_SLICES; i++)
			ok = ok && pfn[i] == pfn[0];
		ksft_test_result(ok,
				 "the %u slices share one native folio (pfn %#llx %#llx %#llx %#llx)\n",
				 (unsigned int)PPPS_SLICES,
				 (unsigned long long)pfn[0],
				 (unsigned long long)pfn[1],
				 (unsigned long long)pfn[2],
				 (unsigned long long)pfn[3]);
	}

	/*
	 * Fallback leg.  Populate slice 1 of a fresh tuple with a 4K COPY, then
	 * ask for the whole 16K tuple: the tuple path must decline (not report
	 * EEXIST for the range) and the per-compat-page path must fill slice 0,
	 * stop at slice 1 and report exactly 4096 -- byte-for-byte what the
	 * pre-K1 kernel reported.
	 */
	copied = uffd_copy(uffd, tuple_b + PROCESS_PAGE_SIZE, src + NATIVE_PAGE_SIZE,
			   PROCESS_PAGE_SIZE);
	if (copied != (long)PROCESS_PAGE_SIZE) {
		ksft_test_result_fail("seeding slice 1 failed (copy=%ld)\n",
				      copied);
	} else {
		copied = uffd_copy(uffd, tuple_b, src, NATIVE_PAGE_SIZE);
		ok = copied == (long)PROCESS_PAGE_SIZE &&
		     all_bytes_are(tuple_b, PROCESS_PAGE_SIZE, 0xa0) &&
		     all_bytes_are(tuple_b + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, 0xa4);
		ksft_test_result(ok,
				 "16K COPY over a populated slice stops at it (copy=%ld, want %lu)\n",
				 copied, PROCESS_PAGE_SIZE);
	}

	ksft_test_result(test_source_retry(uffd, tuple_retry),
			 "source faults retry outside mmap lock and preserve the tuple\n");

	ksft_test_result(test_same_tuple_race(uffd, race_region,
					      src + 2 * NATIVE_PAGE_SIZE,
					      src + 3 * NATIVE_PAGE_SIZE),
			 "same-tuple COPY races have one winner and one EEXIST loser\n");

	ksft_test_result(test_zeropage_mremap(uffd, tuple_move, src),
			 "unaligned mremap keeps COPY slices beside a ZEROPAGE slice\n");

	close(uffd);
	munmap(src, 4 * NATIVE_PAGE_SIZE);
	munmap(reservation, REGION_LEN + NATIVE_PAGE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
