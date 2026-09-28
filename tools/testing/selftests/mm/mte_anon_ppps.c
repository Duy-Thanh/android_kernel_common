// SPDX-License-Identifier: GPL-2.0
/*
 * MTE tags on a 4K compat process's packed anonymous tuples survive a
 * punched hole, fork COW, pageout, a 4K-shifted mremap, partial PROT_MTE,
 * private shmem COW and a UFFDIO_COPY into a hole, with hole slices untagged.
 */
#define _GNU_SOURCE

#include <asm/hwcap.h>
#include <linux/mman.h>
#include <linux/prctl.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include "ppps_tuple_test.h"

#define MTE_GRANULE 16UL
#define TAG_SHIFT 56
#define TAG_MASK (UINT64_C(0xf) << TAG_SHIFT)

static void *tag_pointer(const void *pointer, unsigned int tag)
{
	return (void *)(((uintptr_t)pointer & ~TAG_MASK) |
			((uint64_t)(tag & 0xf) << TAG_SHIFT));
}

static void store_tag(void *address, unsigned int tag)
{
	void *tagged = tag_pointer(address, tag);

	asm volatile(".arch_extension memtag\n\t"
		     "stg %0, [%0]"
		     : : "r" (tagged) : "memory");
}

static unsigned int load_tag(void *address)
{
	uintptr_t tagged;

	asm volatile(".arch_extension memtag\n\t"
		     "ldg %0, [%1]"
		     : "=r" (tagged) : "r" (address));
	return (tagged >> TAG_SHIFT) & 0xf;
}

static void tag_slice(unsigned char *slice, unsigned int tag)
{
	unsigned long offset;

	for (offset = 0; offset < PROCESS_PAGE_SIZE; offset += MTE_GRANULE)
		store_tag(slice + offset, tag);
}

static unsigned char *map_mte_aligned(size_t size,
				      unsigned char **reservation,
				      size_t *reservation_size)
{
	*reservation_size = size + NATIVE_PAGE_SIZE;
	return map_aligned_prot(size, PROT_READ | PROT_WRITE | PROT_MTE,
				reservation);
}

static bool mte_tuple_and_hole(void)
{
	unsigned char *reservation = NULL, *base;
	size_t reservation_size;
	uint64_t pfn[PPPS_SLICES];
	unsigned char *tagged;
	bool passed;

	base = map_mte_aligned(NATIVE_PAGE_SIZE, &reservation, &reservation_size);
	if (base == MAP_FAILED)
		return false;
	memset(base, 0x31, NATIVE_PAGE_SIZE);
	tag_slice(base + PROCESS_PAGE_SIZE, 5);
	tagged = tag_pointer(base + PROCESS_PAGE_SIZE, 5);
	passed = tagged[0] == 0x31 && read_pfns(base, pfn) && same_pfn(pfn) &&
		!madvise(base + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, MADV_DONTNEED) &&
		base[PROCESS_PAGE_SIZE] == 0 &&
		load_tag(base + PROCESS_PAGE_SIZE) == 0;
	munmap(reservation, reservation_size);
	return passed;
}

static bool mte_fork_cow(void)
{
	unsigned char *reservation = NULL, *base, *tagged;
	size_t reservation_size;
	int ready[2], done[2], status;
	char byte;
	pid_t child;
	bool passed;

	base = map_mte_aligned(NATIVE_PAGE_SIZE, &reservation, &reservation_size);
	if (base == MAP_FAILED || pipe(ready) || pipe(done))
		return false;
	memset(base, 0x41, NATIVE_PAGE_SIZE);
	tag_slice(base + PROCESS_PAGE_SIZE, 5);
	tagged = tag_pointer(base + PROCESS_PAGE_SIZE, 5);
	child = fork();
	if (!child) {
		close(ready[0]);
		close(done[1]);
		if (write(ready[1], "R", 1) != 1 ||
		    read(done[0], &byte, 1) != 1)
			_exit(1);
		_exit(tagged[0] == 0x41 &&
		      load_tag(base + PROCESS_PAGE_SIZE) == 5 ? 0 : 1);
	}
	close(ready[1]);
	close(done[0]);
	passed = child > 0 && read(ready[0], &byte, 1) == 1;
	base[0] = 0x61;
	passed &= tagged[0] == 0x41 &&
		write(done[1], "D", 1) == 1 &&
		waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		!WEXITSTATUS(status);
	close(ready[0]);
	close(done[1]);
	munmap(reservation, reservation_size);
	return passed;
}

static bool mte_pageout(bool hole)
{
	unsigned char *reservation = NULL, *base, *tagged;
	size_t reservation_size;
	bool passed;

	base = map_mte_aligned(NATIVE_PAGE_SIZE, &reservation, &reservation_size);
	if (base == MAP_FAILED)
		return false;
	memset(base, 0x51, NATIVE_PAGE_SIZE);
	tag_slice(base + 2 * PROCESS_PAGE_SIZE, 6);
	tagged = tag_pointer(base + 2 * PROCESS_PAGE_SIZE, 6);
	passed = !hole || !madvise(base + 2 * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				       MADV_DONTNEED);
	passed &= !madvise(base, NATIVE_PAGE_SIZE, MADV_PAGEOUT);
	passed &= ppps_page_swapped(base);
	if (hole) {
		uint64_t pfn0 = 0, pfn1 = 0, pfn2 = 0, pfn3 = 0;
		unsigned char data = 0, tag = 0;
		bool clean = true, joined = false;
		unsigned int attempt;

		/*
		 * Write the present slices first so the folio is swapped back
		 * in, its swap slot released and the tags saved at pageout
		 * restored, stale hole tags included.  A read fault still uses the
		 * zero page; write the hole afterwards to promote it into that same
		 * tagged folio with zero data and zero tags.
		 */
		base[0] = 0x52;
		base[PROCESS_PAGE_SIZE] = 0x52;
		base[3 * PROCESS_PAGE_SIZE] = 0x52;
		for (attempt = 0; attempt < 100; attempt++) {
			data = base[2 * PROCESS_PAGE_SIZE];
			base[2 * PROCESS_PAGE_SIZE] = 0;
			tag = load_tag(base + 2 * PROCESS_PAGE_SIZE);
			clean = !data && !tag;
			joined = ppps_pfn(base, &pfn0) &&
				ppps_pfn(base + 2 * PROCESS_PAGE_SIZE, &pfn2) &&
				pfn0 == pfn2;
			if (!clean || joined)
				break;
			if (madvise(base + 2 * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
				    MADV_DONTNEED))
				break;
			usleep(1000);
		}
		ppps_pfn(base + PROCESS_PAGE_SIZE, &pfn1);
		ppps_pfn(base + 3 * PROCESS_PAGE_SIZE, &pfn3);
		if (!clean || !joined) {
			ksft_print_msg("hole-after-pageout data=%#x tag=%u\n",
				       data, tag);
			ksft_print_msg("hole PFNs=%#llx,%#llx,%#llx,%#llx\n",
				       (unsigned long long)pfn0,
				       (unsigned long long)pfn1,
				       (unsigned long long)pfn2,
				       (unsigned long long)pfn3);
		}
		passed &= clean && joined;
	} else {
		passed &= tagged[0] == 0x51 &&
			load_tag(base + 2 * PROCESS_PAGE_SIZE) == 6;
	}
	munmap(reservation, reservation_size);
	return passed;
}

static bool mte_shifted_mremap(void)
{
	unsigned char *src_reservation = NULL, *src;
	unsigned char *dst_reservation, *dst;
	size_t src_size;
	void *moved;
	bool passed = false;

	src = map_mte_aligned(NATIVE_PAGE_SIZE, &src_reservation, &src_size);
	dst_reservation = mmap(NULL, 3 * NATIVE_PAGE_SIZE, PROT_NONE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (src == MAP_FAILED || dst_reservation == MAP_FAILED)
		goto out;
	dst = (unsigned char *)(((uintptr_t)dst_reservation + NATIVE_PAGE_SIZE - 1) &
				       ~(NATIVE_PAGE_SIZE - 1)) + PROCESS_PAGE_SIZE;
	memset(src, 0x62, NATIVE_PAGE_SIZE);
	tag_slice(src + PROCESS_PAGE_SIZE, 7);
	moved = mremap(src, NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE,
		       MREMAP_MAYMOVE | MREMAP_FIXED, dst);
	passed = moved == dst &&
		((unsigned char *)tag_pointer(dst + PROCESS_PAGE_SIZE, 7))[0] == 0x62 &&
		load_tag(dst + PROCESS_PAGE_SIZE) == 7;
out:
	if (src_reservation)
		munmap(src_reservation, src_size);
	if (dst_reservation != MAP_FAILED)
		munmap(dst_reservation, 3 * NATIVE_PAGE_SIZE);
	return passed;
}

static bool partial_mprotect(void)
{
	unsigned char *reservation = NULL, *base;
	size_t reservation_size;
	bool passed;

	reservation_size = 2 * NATIVE_PAGE_SIZE;
	reservation = mmap(NULL, reservation_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return false;
	base = (unsigned char *)(((uintptr_t)reservation + NATIVE_PAGE_SIZE - 1) &
				       ~(NATIVE_PAGE_SIZE - 1));
	memset(base, 0x71, NATIVE_PAGE_SIZE);
	passed = !mprotect(base, 2 * PROCESS_PAGE_SIZE,
			   PROT_READ | PROT_WRITE | PROT_MTE) &&
		base[0] == 0x71 &&
		!madvise(base + 3 * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			 MADV_DONTNEED) && base[3 * PROCESS_PAGE_SIZE] == 0;
	munmap(reservation, reservation_size);
	return passed;
}

static bool mte_private_shmem_cow(void)
{
	unsigned char *shared = MAP_FAILED, *private = MAP_FAILED, *tagged;
	uint64_t pfn[PPPS_SLICES];
	bool passed = false;
	int fd = -1;

	fd = memfd_create("ppps-mte-file-cow", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, NATIVE_PAGE_SIZE))
		goto out;
	shared = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_MTE,
		      MAP_SHARED, fd, 0);
	private = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_MTE,
		       MAP_PRIVATE, fd, 0);
	if (shared == MAP_FAILED || private == MAP_FAILED)
		goto out;
	memset(shared, 0x31, NATIVE_PAGE_SIZE);
	tag_slice(shared + PROCESS_PAGE_SIZE, 9);
	tagged = tag_pointer(private + PROCESS_PAGE_SIZE, 9);
	passed = tagged[0] == 0x31;
	tagged[0] = 0x79;
	for (unsigned int i = 0; i < PPPS_SLICES; i++) {
		if (i != 1)
			private[i * PROCESS_PAGE_SIZE] = 0x70 + i;
	}
	passed &= tagged[0] == 0x79 && read_pfns(private, pfn) &&
		same_pfn(pfn) && load_tag(private + PROCESS_PAGE_SIZE) == 9;
	for (unsigned int i = 0; i < PPPS_SLICES; i++)
		if (i != 1)
			passed &= load_tag(private + i * PROCESS_PAGE_SIZE) == 0;
out:
	if (shared != MAP_FAILED)
		munmap(shared, NATIVE_PAGE_SIZE);
	if (private != MAP_FAILED)
		munmap(private, NATIVE_PAGE_SIZE);
	if (fd >= 0)
		close(fd);
	return passed;
}

static void mte_uffd_copy_hole(void)
{
	unsigned char *reservation = NULL, *base, *src = MAP_FAILED;
	size_t reservation_size;
	uint64_t pfn0, pfn2;
	bool passed;
	int uffd;

	base = map_mte_aligned(NATIVE_PAGE_SIZE, &reservation, &reservation_size);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("UFFDIO_COPY test mapping failed\n");
		return;
	}
	/* Populate the tuple before arming MISSING: nobody serves faults here. */
	memset(base, 0x81, NATIVE_PAGE_SIZE);
	tag_slice(base + PROCESS_PAGE_SIZE, 5);
	tag_slice(base + 2 * PROCESS_PAGE_SIZE, 7);
	uffd = uffd_open_register(base, NATIVE_PAGE_SIZE);
	if (uffd < 0) {
		munmap(reservation, reservation_size);
		ksft_test_result_skip("userfaultfd unavailable: %s\n",
				      strerror(errno));
		return;
	}
	src = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	passed = src != MAP_FAILED;
	if (passed)
		memset(src, 0x82, PROCESS_PAGE_SIZE);
	/*
	 * Punch a hole into the tagged tuple and let UFFDIO_COPY fill it.
	 * The copied slice must join the existing folio with zero tags while
	 * the neighbouring slice keeps its own tags.
	 */
	passed = passed &&
		!madvise(base + 2 * PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, MADV_DONTNEED) &&
		uffd_copy(uffd, base + 2 * PROCESS_PAGE_SIZE, src, PROCESS_PAGE_SIZE) ==
			(long)PROCESS_PAGE_SIZE &&
		base[2 * PROCESS_PAGE_SIZE] == 0x82 &&
		load_tag(base + 2 * PROCESS_PAGE_SIZE) == 0 &&
		load_tag(base + PROCESS_PAGE_SIZE) == 5 &&
		ppps_pfn(base, &pfn0) &&
		ppps_pfn(base + 2 * PROCESS_PAGE_SIZE, &pfn2) && pfn0 == pfn2;
	if (src != MAP_FAILED)
		munmap(src, PROCESS_PAGE_SIZE);
	close(uffd);
	munmap(reservation, reservation_size);
	ksft_test_result(passed,
			 "UFFDIO_COPY into a tagged tuple hole clears its tags\n");
}

static int run_test(void)
{
	unsigned long control;

	ksft_print_header();
	if (!(getauxval(AT_HWCAP2) & HWCAP2_MTE))
		ksft_exit_skip("MTE is not available\n");
	ksft_set_plan(8);
	control = PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC |
		  (UINT64_C(0xfffe) << PR_MTE_TAG_SHIFT);
	if (prctl(PR_SET_TAGGED_ADDR_CTRL, control, 0, 0, 0))
		ksft_exit_fail_msg("PR_SET_TAGGED_ADDR_CTRL failed\n");

	ksft_test_result(mte_tuple_and_hole(),
			 "MTE tuple sharing and tagged hole refault\n");
	ksft_test_result(mte_fork_cow(), "MTE tags survive tuple COW\n");
	ksft_test_result(mte_pageout(false), "MTE tags survive pageout\n");
	ksft_test_result(mte_pageout(true),
			 "MTE hole tags are cleared after pageout\n");
	ksft_test_result(mte_shifted_mremap(),
			 "MTE tags follow a 4K-shifted mremap\n");
	ksft_test_result(partial_mprotect(),
			 "partial PROT_MTE keeps mixed tuple semantics\n");
	ksft_test_result(mte_private_shmem_cow(),
			 "MTE tags survive private shmem tuple COW\n");
	mte_uffd_copy_hole();
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
