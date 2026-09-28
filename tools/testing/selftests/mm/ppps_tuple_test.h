/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared helpers for the PPPS packed-tuple selftests.
 *
 * These tests run as a 4K compat process on a 16K native kernel and reason
 * about the four 4K slices ("a tuple") backed by one native page.  The generic
 * PPPS helpers (re-exec, /proc readers) live in kselftest_ppps.h; every helper
 * here is static inline so each test stays a single translation unit.
 */
#ifndef PPPS_TUPLE_TEST_H
#define PPPS_TUPLE_TEST_H

#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

/* MADV_PAGEOUT is asynchronous; poll smaps this often, 10ms apart. */
#define PAGEOUT_POLLS		1000

/* /proc/self/pagemap */

/* PFNs of four arbitrary present pages; pfn may be 0 without CAP_SYS_ADMIN. */
static inline bool read_pagemap_pfns(const unsigned char *const address[PPPS_SLICES],
				     uint64_t pfn[PPPS_SLICES])
{
	uint64_t entry;
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++) {
		if (!ppps_pagemap_entry(address[i], &entry) ||
		    !(entry & PAGEMAP_PRESENT))
			return false;
		pfn[i] = ppps_native_pfn(entry & PAGEMAP_PFN_MASK);
	}
	return true;
}

/* PFNs of the four slices of the tuple at @base. */
static inline bool read_pfns(const unsigned char *base, uint64_t pfn[PPPS_SLICES])
{
	const unsigned char *address[PPPS_SLICES];
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++)
		address[i] = base + i * PROCESS_PAGE_SIZE;
	return read_pagemap_pfns(address, pfn);
}

/* All four slices map the same (visible) native page. */
static inline bool same_pfn(const uint64_t pfn[PPPS_SLICES])
{
	unsigned int i;

	if (!pfn[0])
		return false;
	for (i = 1; i < PPPS_SLICES; i++)
		if (pfn[i] != pfn[0])
			return false;
	return true;
}

/* The listed slices are present and share one native page. */
static inline bool same_present_pfns(const unsigned char *base,
				     const unsigned int *slices,
				     unsigned int nr)
{
	uint64_t first = 0, pfn;
	unsigned int i;

	for (i = 0; i < nr; i++) {
		if (!ppps_pfn(base + slices[i] * PROCESS_PAGE_SIZE, &pfn))
			return false;
		if (i && pfn != first)
			return false;
		first = pfn;
	}
	return true;
}

/* Exactly one slice of the tuple at @base is present. */
static inline bool only_slice_present(const unsigned char *base,
				      unsigned int present_slice)
{
	uint64_t entry;
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++) {
		if (!ppps_pagemap_entry(base + i * PROCESS_PAGE_SIZE, &entry) ||
		    !!(entry & PAGEMAP_PRESENT) != (i == present_slice))
			return false;
	}
	return true;
}

/* /proc/self/status */

/* RssAnon of this process in bytes, or -1. */
static inline long rss_anon_bytes(void)
{
	long kb = ppps_status_kb(0, "RssAnon");

	return kb < 0 ? -1 : kb * 1024;
}

/* Wait for MADV_PAGEOUT to move at least @minimum bytes of the range to swap. */
static inline bool pageout_reaches_swap(void *address, unsigned long length,
					unsigned long minimum)
{
	unsigned long swap;
	unsigned int attempt;

	if (madvise(address, length, MADV_PAGEOUT))
		return false;
	for (attempt = 0; attempt < PAGEOUT_POLLS; attempt++) {
		if (!ppps_smaps_bytes(address, length, "Swap", &swap))
			return false;
		if (swap >= minimum)
			return true;
		usleep(10000);
	}
	return false;
}

/* Mappings */

/*
 * Map @size bytes starting on a native-page boundary.  The over-sized
 * reservation to unmap is returned in @reservation; it is size + NATIVE_PAGE_SIZE.
 */
static inline unsigned char *map_aligned_prot(size_t size, int prot,
					      unsigned char **reservation)
{
	unsigned char *mapping;
	uintptr_t aligned;

	mapping = mmap(NULL, size + NATIVE_PAGE_SIZE, prot,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return MAP_FAILED;
	aligned = ((uintptr_t)mapping + NATIVE_PAGE_SIZE - 1) & ~(NATIVE_PAGE_SIZE - 1);
	*reservation = mapping;
	return (unsigned char *)aligned;
}

static inline unsigned char *map_aligned(size_t size,
					 unsigned char **reservation)
{
	return map_aligned_prot(size, PROT_READ | PROT_WRITE, reservation);
}

/*
 * Map exactly @size accessible bytes on a native-page boundary with PROT_NONE
 * guards on both sides, so the VMA starts and ends where the test says it
 * does.  The reservation to unmap spans size + 2 * NATIVE_PAGE_SIZE.
 */
static inline unsigned char *map_exact_edge(size_t size,
					    unsigned char **reservation)
{
	unsigned char *mapping, *base;
	size_t length = size + 2 * NATIVE_PAGE_SIZE;

	mapping = mmap(NULL, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
		       -1, 0);
	if (mapping == MAP_FAILED)
		return MAP_FAILED;
	base = (unsigned char *)(((uintptr_t)mapping + 2 * NATIVE_PAGE_SIZE - 1) &
				 ~(NATIVE_PAGE_SIZE - 1));
	if (mprotect(base, size, PROT_READ | PROT_WRITE)) {
		munmap(mapping, length);
		return MAP_FAILED;
	}
	*reservation = mapping;
	return base;
}

/* Tuple contents: slice i is filled with the byte @first + i. */

static inline void fill_tuple(unsigned char *base, unsigned char first)
{
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++)
		memset(base + i * PROCESS_PAGE_SIZE, first + i, PROCESS_PAGE_SIZE);
}

static inline bool check_tuple(const unsigned char *base, unsigned char first)
{
	unsigned long offset;

	for (offset = 0; offset < NATIVE_PAGE_SIZE; offset++)
		if (base[offset] != first + offset / PROCESS_PAGE_SIZE)
			return false;
	return true;
}

static inline bool all_bytes_are(const unsigned char *p, unsigned long len,
				 unsigned char want)
{
	unsigned long i;

	for (i = 0; i < len; i++)
		if (p[i] != want)
			return false;
	return true;
}

/* userfaultfd */

/* A MISSING-registered userfaultfd for [@start, @start + @len), or -1. */
static inline int uffd_open_register(void *start, unsigned long len)
{
	struct uffdio_register reg = {};
	struct uffdio_api api = {};
	int uffd;

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | UFFD_USER_MODE_ONLY);
	if (uffd < 0)
		return -1;
	api.api = UFFD_API;
	reg.range.start = (uintptr_t)start;
	reg.range.len = len;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_API, &api) ||
	    ioctl(uffd, UFFDIO_REGISTER, &reg)) {
		close(uffd);
		return -1;
	}
	return uffd;
}

/*
 * UFFDIO_COPY @len bytes.  Returns the bytes installed: a partial fill
 * returns -1/EAGAIN from the ioctl but still reports .copy, which is what
 * the tests care about.  Returns -errno when nothing was copied.
 */
static inline long uffd_copy(int uffd, void *dst, void *src, unsigned long len)
{
	struct uffdio_copy copy = {};
	int ret;

	copy.dst = (uintptr_t)dst;
	copy.src = (uintptr_t)src;
	copy.len = len;
	ret = ioctl(uffd, UFFDIO_COPY, &copy);
	if (copy.copy > 0)
		return (long)copy.copy;
	return ret ? -errno : 0;
}

#endif /* PPPS_TUPLE_TEST_H */
