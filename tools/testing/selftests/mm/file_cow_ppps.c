// SPDX-License-Identifier: GPL-2.0
/*
 * Private file COW in a 4K compat process packs the four slices of a native
 * page into one anonymous folio and keeps that tuple intact across fork,
 * mprotect, mremap, pageout, truncate, ptrace writes and PTE-table edges.
 */
#define _GNU_SOURCE

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ppps_tuple_test.h"

#define PTE_SPAN (512UL * PROCESS_PAGE_SIZE)

static int pattern_file(size_t size)
{
	unsigned char *buffer;
	unsigned long offset;
	int fd;

	fd = memfd_create("ppps-file-cow", MFD_CLOEXEC);
	if (fd < 0)
		return -1;
	buffer = malloc(size);
	if (!buffer) {
		close(fd);
		return -1;
	}
	for (offset = 0; offset < size; offset++)
		buffer[offset] = 0x20 + (offset / PROCESS_PAGE_SIZE) % 0x40;
	if (ftruncate(fd, size) ||
	    pwrite(fd, buffer, size, 0) != (ssize_t)size) {
		free(buffer);
		close(fd);
		return -1;
	}
	free(buffer);
	return fd;
}

static bool read_vmstat(const char *name, unsigned long long *value)
{
	char key[128];
	unsigned long long current;
	FILE *file;
	bool found = false;

	file = fopen("/proc/vmstat", "re");
	if (!file)
		return false;
	while (fscanf(file, "%127s %llu", key, &current) == 2) {
		if (!strcmp(key, name)) {
			*value = current;
			found = true;
			break;
		}
	}
	fclose(file);
	return found;
}

static unsigned char *fixed_offset_mapping(int fd, unsigned char **reservation,
					   unsigned char **tuple,
					   bool cross_pte_boundary)
{
	const size_t reservation_size = 2 * PTE_SPAN;
	unsigned char *mapping;
	uintptr_t boundary;

	*reservation = mmap(NULL, reservation_size, PROT_NONE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (*reservation == MAP_FAILED)
		return MAP_FAILED;
	boundary = ((uintptr_t)*reservation + 2 * NATIVE_PAGE_SIZE +
		    PTE_SPAN - 1) & ~(PTE_SPAN - 1);
	if (cross_pte_boundary)
		*tuple = (unsigned char *)(boundary - 2 * PROCESS_PAGE_SIZE);
	else
		*tuple = (unsigned char *)(boundary + NATIVE_PAGE_SIZE);
	mapping = *tuple - 3 * PROCESS_PAGE_SIZE;
	if (mmap(mapping, 2 * NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_FIXED, fd, PROCESS_PAGE_SIZE) == MAP_FAILED) {
		munmap(*reservation, reservation_size);
		*reservation = MAP_FAILED;
		return MAP_FAILED;
	}
	return mapping;
}

static void print_vma_flags(const void *address)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end, offset;
	unsigned long vma_start = 0, vma_offset = 0;
	unsigned int slice;
	char perms[8];
	char *line = NULL;
	size_t capacity = 0;
	bool in_target = false;
	FILE *smaps;

	smaps = fopen("/proc/self/smaps", "re");
	if (!smaps)
		return;
	while (getline(&line, &capacity, smaps) >= 0) {
		if (sscanf(line, "%lx-%lx %7s %lx", &start, &end, perms,
			   &offset) == 4) {
			in_target = target >= start && target < end;
			if (in_target) {
				vma_start = start;
				vma_offset = offset;
				ksft_print_msg("boundary VMA %s", line);
			}
			continue;
		}
		if (in_target && !strncmp(line, "VmFlags:", 8)) {
			ksft_print_msg("boundary %s", line);
			break;
		}
	}
	free(line);
	fclose(smaps);
	slice = ((target - vma_start + vma_offset) / PROCESS_PAGE_SIZE) % PPPS_SLICES;
	ksft_print_msg("boundary calculated slice=%u base=%#lx\n", slice,
		       target - slice * PROCESS_PAGE_SIZE);
}

static bool write_all_slices(unsigned char *map, unsigned char first)
{
	unsigned int i;

	for (i = 0; i < PPPS_SLICES; i++)
		map[i * PROCESS_PAGE_SIZE] = first + i;
	for (i = 0; i < PPPS_SLICES; i++)
		if (map[i * PROCESS_PAGE_SIZE] != first + i)
			return false;
	return true;
}

static bool verify_pattern(const unsigned char *map,
			   const unsigned char first[PPPS_SLICES],
			   unsigned int written_mask)
{
	unsigned long offset;

	for (offset = 0; offset < NATIVE_PAGE_SIZE; offset++) {
		unsigned int slice = offset / PROCESS_PAGE_SIZE;
		unsigned char expected = 0x20 + slice;

		if (offset % PROCESS_PAGE_SIZE == 0 && (written_mask & (1U << slice)))
			expected = first[slice];
		if (map[offset] != expected) {
			ksft_print_msg("content mismatch offset=%lu got=%#x expected=%#x\n",
				       offset, map[offset], expected);
			return false;
		}
	}
	return true;
}

static bool full_tuple(void)
{
	const unsigned char written[PPPS_SLICES] = { 0x71, 0x72, 0x73, 0x74 };
	const unsigned int order[PPPS_SLICES] = { 3, 1, 0, 2 };
	unsigned long anonymous;
	uint64_t pfn[PPPS_SLICES];
	unsigned char file_byte;
	unsigned char *map;
	unsigned int written_mask = 0;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	passed = true;
	for (unsigned int i = 0; i < PPPS_SLICES && passed; i++) {
		unsigned int slice = order[i];

		map[slice * PROCESS_PAGE_SIZE] = written[slice];
		written_mask |= 1U << slice;
		passed = verify_pattern(map, written, written_mask);
	}
	passed = passed && read_pfns(map, pfn) &&
			same_pfn(pfn) &&
		ppps_smaps_bytes(map, NATIVE_PAGE_SIZE, "Anonymous", &anonymous) &&
		anonymous == NATIVE_PAGE_SIZE &&
		pread(fd, &file_byte, 1, 0) == 1 && file_byte == 0x20;
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool mixed_file_and_anon(void)
{
	const unsigned char original = 0x20;
	uint64_t pfn[PPPS_SLICES] = {};
	unsigned long anonymous = 0;
	unsigned char *map;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	map[PROCESS_PAGE_SIZE] = 0x61;
	passed = map[0] == original;
	map[0] = 0x60;
	/* Keep slices 2 and 3 as resident page-cache PTEs. */
	passed &= map[2 * PROCESS_PAGE_SIZE] == 0x22 &&
		map[3 * PROCESS_PAGE_SIZE] == 0x23;
	passed &= read_pfns(map, pfn) && pfn[0] && pfn[0] == pfn[1] &&
			ppps_smaps_bytes(map, NATIVE_PAGE_SIZE, "Anonymous", &anonymous) &&
			anonymous == 2 * PROCESS_PAGE_SIZE && map[PROCESS_PAGE_SIZE] == 0x61;
	if (!passed)
		ksft_print_msg("mixed PFNs=%llu,%llu,%llu,%llu Anonymous=%lu\n",
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3], anonymous);
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool fork_cow(void)
{
	unsigned char *map;
	int status;
	pid_t child;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	passed = write_all_slices(map, 0x40);
	child = fork();
	if (!child) {
		uint64_t pfn[PPPS_SLICES];

		map[2 * PROCESS_PAGE_SIZE] = 0x7a;
		_exit(read_pfns(map, pfn) && same_pfn(pfn) &&
		      map[0] == 0x40 && map[2 * PROCESS_PAGE_SIZE] == 0x7a ? 0 : 1);
	}
	passed &= child > 0 && waitpid(child, &status, 0) == child &&
		WIFEXITED(status) && !WEXITSTATUS(status) &&
		map[2 * PROCESS_PAGE_SIZE] == 0x42;
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool offset_tuple(void)
{
	uint64_t pfn[PPPS_SLICES] = {};
	unsigned char *map, *reservation, *tuple;
	bool passed, resident, shared;
	int fd = pattern_file(3 * NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = fixed_offset_mapping(fd, &reservation, &tuple, false);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	passed = write_all_slices(tuple, 0x50);
	resident = read_pfns(tuple, pfn);
	shared = resident && same_pfn(pfn);
	passed &= shared;
	if (!passed) {
		ksft_print_msg("offset tuple map=%p tuple=%p resident=%d shared=%d\n",
			       map, tuple, resident, shared);
		ksft_print_msg("PFNs=%llu,%llu,%llu,%llu\n",
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
	}
	munmap(reservation, 2 * PTE_SPAN);
	close(fd);
	return passed;
}

static bool pte_boundary_fallback(void)
{
	unsigned long long alloc_before, alloc_after = 0;
	unsigned long long fill_before, fill_after = 0;
	unsigned long long before, after = 0;
	unsigned long anon_before = 0, anon_after = 0;
	uint64_t pfn[PPPS_SLICES] = {};
	unsigned char file_byte;
	unsigned char *map, *reservation, *tuple;
	bool passed = true;
	int fd = pattern_file(3 * NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = fixed_offset_mapping(fd, &reservation, &tuple, true);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	/* Install file PTEs first so this specifically exercises do_wp_page(). */
	for (unsigned int i = 0; i < PPPS_SLICES; i++)
		passed &= tuple[i * PROCESS_PAGE_SIZE] == 0x24 + i;
	passed &= ppps_smaps_bytes(tuple, NATIVE_PAGE_SIZE, "Anonymous",
				   &anon_before) &&
		read_vmstat("ppps_file_cow_alloc", &alloc_before) &&
		read_vmstat("ppps_file_cow_fill", &fill_before) &&
		read_vmstat("ppps_file_cow_multi_folio", &before) &&
		write_all_slices(tuple, 0x68) && read_pfns(tuple, pfn) &&
		!same_pfn(pfn) &&
		read_vmstat("ppps_file_cow_alloc", &alloc_after) &&
		read_vmstat("ppps_file_cow_fill", &fill_after) &&
		read_vmstat("ppps_file_cow_multi_folio", &after) &&
		ppps_smaps_bytes(tuple, NATIVE_PAGE_SIZE, "Anonymous", &anon_after) &&
		after > before;
	for (unsigned int i = 0; i < PPPS_SLICES; i++)
		passed &= pread(fd, &file_byte, 1,
				4 * PROCESS_PAGE_SIZE + i * PROCESS_PAGE_SIZE) == 1 &&
			file_byte == 0x24 + i;
	if (!passed)
		print_vma_flags(tuple);
	if (!passed) {
		ksft_print_msg("boundary tuple=%p PFNs=%llu,%llu,%llu,%llu\n",
			       tuple,
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
		ksft_print_msg("alloc=%llu->%llu fill=%llu->%llu\n",
			       alloc_before, alloc_after, fill_before, fill_after);
		ksft_print_msg("multi=%llu->%llu anon=%lu->%lu\n", before, after,
			       anon_before, anon_after);
	}
	munmap(reservation, 2 * PTE_SPAN);
	close(fd);
	return passed;
}

static bool split_vma(void)
{
	uint64_t pfn[PPPS_SLICES];
	unsigned char *map;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	map[0] = 0x51;
	passed = !mprotect(map + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE, PROT_READ) &&
		!mprotect(map + PROCESS_PAGE_SIZE, PROCESS_PAGE_SIZE,
			  PROT_READ | PROT_WRITE) &&
		write_all_slices(map, 0x51) && read_pfns(map, pfn) &&
		same_pfn(pfn);
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool partial_mremap_once(bool cross_pte_boundary)
{
	size_t reservation_size = cross_pte_boundary ? 2 * PTE_SPAN :
		2 * NATIVE_PAGE_SIZE;
	unsigned char *destination, *map, *moved, *reservation;
	uintptr_t boundary;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	reservation = mmap(NULL, reservation_size, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED || reservation == MAP_FAILED) {
		if (map != MAP_FAILED)
			munmap(map, NATIVE_PAGE_SIZE);
		if (reservation != MAP_FAILED)
			munmap(reservation, reservation_size);
		close(fd);
		return false;
	}
	if (cross_pte_boundary) {
		boundary = ((uintptr_t)reservation + PROCESS_PAGE_SIZE +
			    PTE_SPAN - 1) &
			~(PTE_SPAN - 1);
		destination = (unsigned char *)boundary - PROCESS_PAGE_SIZE;
	} else {
		destination = reservation;
	}
	passed = write_all_slices(map, 0x40);
	moved = mremap(map + PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
		       2 * PROCESS_PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
		       destination);
	passed &= moved == destination && map[0] == 0x40 &&
		map[3 * PROCESS_PAGE_SIZE] == 0x43 && destination[0] == 0x41 &&
		destination[PROCESS_PAGE_SIZE] == 0x42;
	if (passed) {
		map[0] = 0x60;
		map[3 * PROCESS_PAGE_SIZE] = 0x63;
		destination[0] = 0x61;
		destination[PROCESS_PAGE_SIZE] = 0x62;
		passed = map[0] == 0x60 && map[3 * PROCESS_PAGE_SIZE] == 0x63 &&
			destination[0] == 0x61 &&
			destination[PROCESS_PAGE_SIZE] == 0x62;
	}
	munmap(map, NATIVE_PAGE_SIZE);
	munmap(reservation, reservation_size);
	close(fd);
	return passed;
}

static bool partial_mremap(void)
{
	return partial_mremap_once(false) && partial_mremap_once(true);
}

static bool pageout_tuple(void)
{
	uint64_t pfn[PPPS_SLICES];
	unsigned char *map;
	bool wrote, resident, shared;
	int advice;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	wrote = write_all_slices(map, 0x30);
	passed = wrote;
	advice = madvise(map, NATIVE_PAGE_SIZE, MADV_PAGEOUT);
	passed &= !advice;
	for (unsigned int i = 0; i < PPPS_SLICES; i++)
		passed &= map[i * PROCESS_PAGE_SIZE] == 0x30 + i;
	resident = read_pfns(map, pfn);
	shared = resident && same_pfn(pfn);
	passed &= shared;
	if (!passed) {
		ksft_print_msg("pageout wrote=%d advice=%d errno=%d resident=%d shared=%d\n",
			       wrote, advice, errno, resident, shared);
		ksft_print_msg("bytes=%#x,%#x,%#x,%#x\n", map[0],
			       map[PROCESS_PAGE_SIZE], map[2 * PROCESS_PAGE_SIZE],
			       map[3 * PROCESS_PAGE_SIZE]);
		ksft_print_msg("PFNs=%llu,%llu,%llu,%llu\n",
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
	}
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static unsigned char force_read_byte(const unsigned char *address)
{
	return *(const volatile unsigned char *)address;
}

static void expected_sigbus(int signal)
{
	_exit(signal == SIGBUS ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool read_sigbus(const unsigned char *address)
{
	int status;
	pid_t child = fork();

	if (!child) {
		struct sigaction action = {
			.sa_handler = expected_sigbus,
		};

		sigemptyset(&action.sa_mask);
		if (sigaction(SIGBUS, &action, NULL))
			_exit(2);
		(void)force_read_byte(address);
		_exit(1);
	}
	return child > 0 && waitpid(child, &status, 0) == child &&
		WIFEXITED(status) && !WEXITSTATUS(status);
}

static bool truncate_mixed(void)
{
	unsigned char *map;
	bool passed;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	map[PROCESS_PAGE_SIZE] = 0x6a;
	/* Hole punching preserves private COW data (even_cows=false). */
	passed = !fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			   0, NATIVE_PAGE_SIZE);
	passed &= force_read_byte(map) == 0 &&
		map[PROCESS_PAGE_SIZE] == 0x6a;
	/* Truncation invalidates both file and COW PTEs past EOF. */
	passed &= !ftruncate(fd, 0);
	passed &= read_sigbus(map) && read_sigbus(map + PROCESS_PAGE_SIZE);
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool independent_mmaps(void)
{
	const size_t reservation_size = 2 * PTE_SPAN;
	unsigned long long multi_before = 0, multi_after = 0;
	uint64_t pfn[PPPS_SLICES] = {};
	unsigned char *base, *reservation;
	char path[64];
	bool passed;
	int fd2 = -1;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
	fd2 = open(path, O_RDWR | O_CLOEXEC);
	reservation = mmap(NULL, reservation_size, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (fd2 < 0 || reservation == MAP_FAILED)
		goto fail;
	base = (unsigned char *)(((uintptr_t)reservation + PTE_SPAN - 1) &
				 ~(PTE_SPAN - 1)) + NATIVE_PAGE_SIZE;
	if (mmap(base, 2 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_FIXED, fd, 0) == MAP_FAILED ||
	    mmap(base + 2 * PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
		 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd2,
		 2 * PROCESS_PAGE_SIZE) == MAP_FAILED)
		goto fail;
	passed = read_vmstat("ppps_file_cow_multi_folio", &multi_before) &&
		write_all_slices(base, 0x40) && read_pfns(base, pfn) &&
		pfn[0] && pfn[0] == pfn[1] && pfn[2] && pfn[2] == pfn[3] &&
		pfn[0] != pfn[2] &&
		read_vmstat("ppps_file_cow_multi_folio", &multi_after) &&
		multi_after > multi_before;
	if (!passed) {
		print_vma_flags(base);
		print_vma_flags(base + 2 * PROCESS_PAGE_SIZE);
		ksft_print_msg("independent PFNs=%llu,%llu,%llu,%llu\n",
			       (unsigned long long)pfn[0],
			       (unsigned long long)pfn[1],
			       (unsigned long long)pfn[2],
			       (unsigned long long)pfn[3]);
		ksft_print_msg("multi=%llu->%llu\n", multi_before, multi_after);
	}
	munmap(reservation, reservation_size);
	close(fd2);
	close(fd);
	return passed;

fail:
	if (reservation != MAP_FAILED)
		munmap(reservation, reservation_size);
	if (fd2 >= 0)
		close(fd2);
	close(fd);
	return false;
}

static bool large_pagecache_source(void)
{
	static const char * const candidates[] = {
		"/system/bin/linker64", "/system/bin/sh", "/bin/sh",
		"/proc/self/exe",
	};
	unsigned char *expected = NULL, *map = MAP_FAILED;
	struct stat stat;
	off_t offset;
	bool passed = false;
	int fd = -1;

	/*
	 * Executable readahead commonly puts this native page in a large file
	 * folio.  The COW fill path must copy from vmf->page, not the folio head.
	 */
	for (unsigned int i = 0; i < ARRAY_SIZE(candidates); i++) {
		fd = open(candidates[i], O_RDONLY | O_CLOEXEC);
		if (fd >= 0 && !fstat(fd, &stat) &&
		    stat.st_size >= (off_t)(2 * NATIVE_PAGE_SIZE))
			break;
		if (fd >= 0)
			close(fd);
		fd = -1;
	}
	if (fd < 0)
		goto out;
	offset = NATIVE_PAGE_SIZE;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, offset);
	if (map == MAP_FAILED)
		goto out;
	expected = malloc(NATIVE_PAGE_SIZE);
	if (!expected)
		goto out;
	memcpy(expected, map, NATIVE_PAGE_SIZE);
	passed = true;
	for (unsigned int i = 0; i < PPPS_SLICES && passed; i++) {
		unsigned long pos = i * PROCESS_PAGE_SIZE;

		expected[pos] ^= 0x5a;
		map[pos] = expected[pos];
		if (memcmp(map, expected, NATIVE_PAGE_SIZE)) {
			unsigned long mismatch;

			for (mismatch = 0; mismatch < NATIVE_PAGE_SIZE; mismatch++)
				if (map[mismatch] != expected[mismatch])
					break;
			ksft_print_msg("large-folio mismatch after slice %u\n", i);
			ksft_print_msg("offset=%lu got=%#x expected=%#x\n",
				       mismatch, map[mismatch], expected[mismatch]);
			passed = false;
		}
	}
out:
	free(expected);
	if (map != MAP_FAILED)
		munmap(map, NATIVE_PAGE_SIZE);
	if (fd >= 0)
		close(fd);
	return passed;
}

static bool ptrace_force_cow(void)
{
	const unsigned long first_word = 0x5151515151515151UL;
	const unsigned long second_word = 0x6262626262626262UL;
	unsigned char file_byte;
	unsigned char *map;
	int status = 0;
	pid_t child;
	bool passed, reaped = false;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		close(fd);
		return false;
	}
	/* Install read-only file PTEs before fork and forced remote writes. */
	passed = map[0] == 0x20 && map[PROCESS_PAGE_SIZE] == 0x21 &&
		map[2 * PROCESS_PAGE_SIZE] == 0x22 &&
		map[3 * PROCESS_PAGE_SIZE] == 0x23;
	child = fork();
	if (!child) {
		unsigned long anonymous = 0;
		uint64_t pfn[PPPS_SLICES] = {};
		unsigned long got_first, got_second;

		if (ptrace(PTRACE_TRACEME, 0, NULL, NULL))
			_exit(2);
		raise(SIGSTOP);
		memcpy(&got_first, map, sizeof(got_first));
		memcpy(&got_second, map + PROCESS_PAGE_SIZE, sizeof(got_second));
		_exit(got_first == first_word && got_second == second_word &&
		      map[2 * PROCESS_PAGE_SIZE] == 0x22 &&
		      map[3 * PROCESS_PAGE_SIZE] == 0x23 && read_pfns(map, pfn) &&
		      pfn[0] && pfn[0] == pfn[1] && pfn[0] != pfn[2] &&
		      ppps_smaps_bytes(map, NATIVE_PAGE_SIZE, "Anonymous",
				       &anonymous) &&
		      anonymous == 2 * PROCESS_PAGE_SIZE ? 0 : 1);
	}
	if (child <= 0 || waitpid(child, &status, WUNTRACED) != child ||
	    !WIFSTOPPED(status) ||
	    ptrace(PTRACE_POKEDATA, child, map,
		   (void *)(uintptr_t)first_word) ||
	    ptrace(PTRACE_POKEDATA, child, map + PROCESS_PAGE_SIZE,
		   (void *)(uintptr_t)second_word) ||
	    ptrace(PTRACE_CONT, child, NULL, NULL) ||
	    waitpid(child, &status, 0) != child) {
		passed = false;
		goto out;
	}
	reaped = true;
	passed &= WIFEXITED(status) && !WEXITSTATUS(status) &&
		pread(fd, &file_byte, 1, 0) == 1 && file_byte == 0x20 &&
		pread(fd, &file_byte, 1, PROCESS_PAGE_SIZE) == 1 &&
		file_byte == 0x21;
out:
	if (child > 0 && !reaped) {
		ptrace(PTRACE_KILL, child, NULL, NULL);
		kill(child, SIGKILL);
		waitpid(child, &status, 0);
	}
	if (!passed)
		ksft_print_msg("ptrace child status=%#x errno=%d\n", status,
			       errno);
	munmap(map, NATIVE_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool clean_file_mremap_fixed(void)
{
	const size_t length = 7 * PROCESS_PAGE_SIZE;
	unsigned char *map, *destination, *moved;
	bool passed = true;
	unsigned long offset;
	int saved_errno = 0;
	int fd = pattern_file(length);

	if (fd < 0)
		return false;
	map = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	destination = mmap(NULL, length, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED || destination == MAP_FAILED) {
		if (map != MAP_FAILED)
			munmap(map, length);
		if (destination != MAP_FAILED)
			munmap(destination, length);
		close(fd);
		return false;
	}

	/* Populate clean page-cache PTEs without triggering private COW. */
	for (offset = 0; offset < length; offset += PROCESS_PAGE_SIZE)
		passed &= map[offset] == 0x20 + offset / PROCESS_PAGE_SIZE;
	moved = mremap(map, length, length, MREMAP_MAYMOVE | MREMAP_FIXED,
		       destination);
	if (moved == MAP_FAILED) {
		saved_errno = errno;
		passed = false;
	} else {
		for (offset = 0; offset < length; offset += PROCESS_PAGE_SIZE)
			passed &= moved[offset] == 0x20 + offset / PROCESS_PAGE_SIZE;
	}
	if (!passed)
		ksft_print_msg("clean file mremap moved=%p errno=%d\n", moved,
			       saved_errno);
	if (moved != MAP_FAILED)
		munmap(moved, length);
	else
		munmap(map, length);
	munmap(destination, length);
	close(fd);
	return passed;
}

static bool mixed_file_mremap_fixed(void)
{
	unsigned char *map, *destination, *moved;
	bool passed;
	int saved_errno = 0;
	int fd = pattern_file(NATIVE_PAGE_SIZE);

	if (fd < 0)
		return false;
	map = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		   fd, 0);
	destination = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED || destination == MAP_FAILED) {
		if (map != MAP_FAILED)
			munmap(map, NATIVE_PAGE_SIZE);
		if (destination != MAP_FAILED)
			munmap(destination, 2 * PROCESS_PAGE_SIZE);
		close(fd);
		return false;
	}

	/* Leave slices 2 and 3 as clean file PTEs beside a packed COW pair. */
	passed = map[0] == 0x20 && map[PROCESS_PAGE_SIZE] == 0x21 &&
		map[2 * PROCESS_PAGE_SIZE] == 0x22 &&
		map[3 * PROCESS_PAGE_SIZE] == 0x23;
	map[0] = 0x70;
	map[PROCESS_PAGE_SIZE] = 0x71;
	moved = mremap(map + PROCESS_PAGE_SIZE, 2 * PROCESS_PAGE_SIZE,
		       2 * PROCESS_PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
		       destination);
	if (moved == MAP_FAILED) {
		saved_errno = errno;
		passed = false;
	} else {
		passed &= map[0] == 0x70 && moved[0] == 0x71 &&
			moved[PROCESS_PAGE_SIZE] == 0x22 &&
			map[3 * PROCESS_PAGE_SIZE] == 0x23;
	}
	if (!passed)
		ksft_print_msg("mixed file mremap moved=%p errno=%d\n", moved,
			       saved_errno);
	if (moved != MAP_FAILED)
		munmap(moved, 2 * PROCESS_PAGE_SIZE);
	munmap(map, NATIVE_PAGE_SIZE);
	munmap(destination, 2 * PROCESS_PAGE_SIZE);
	close(fd);
	return passed;
}

static bool run_one_test(unsigned int test)
{
	switch (test) {
	case 1:
		return full_tuple();
	case 2:
		return mixed_file_and_anon();
	case 3:
		return fork_cow();
	case 4:
		return offset_tuple();
	case 5:
		return split_vma();
	case 6:
		return partial_mremap();
	case 7:
		return pageout_tuple();
	case 8:
		return truncate_mixed();
	case 9:
		return independent_mmaps();
	case 10:
		return large_pagecache_source();
	case 11:
		return pte_boundary_fallback();
	case 12:
		return ptrace_force_cow();
	case 13:
		return clean_file_mremap_fixed();
	case 14:
		return mixed_file_mremap_fixed();
	default:
		return false;
	}
}

static int run_test(void)
{
	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(14);

	ksft_test_result(full_tuple(),
			 "four file COW slices share one anonymous folio\n");
	ksft_test_result(mixed_file_and_anon(),
			 "file and anonymous slices coexist in one tuple\n");
	ksft_test_result(fork_cow(), "fork COW preserves the file tuple\n");
	ksft_test_result(offset_tuple(), "offset-4K file tuple shares correctly\n");
	ksft_test_result(split_vma(), "mprotect split preserves tuple sharing\n");
	ksft_test_result(partial_mremap(),
			 "partial mremap preserves file COW data\n");
	ksft_test_result(pageout_tuple(), "file COW tuple survives pageout\n");
	ksft_test_result(truncate_mixed(),
			 "hole punch preserves COW; truncate invalidates all slices\n");
	ksft_test_result(independent_mmaps(),
			 "independent mappings keep separate anonymous tuples\n");
	ksft_test_result(large_pagecache_source(),
			 "file COW fill uses the exact page of a large folio\n");
	ksft_test_result(pte_boundary_fallback(),
			 "file tuple crossing a PTE table safely falls back\n");
	ksft_test_result(ptrace_force_cow(),
			 "ptrace forced write packs private read-only file COW\n");
	ksft_test_result(clean_file_mremap_fixed(),
			 "fixed mremap preserves clean private file PTEs\n");
	ksft_test_result(mixed_file_mremap_fixed(),
			 "fixed mremap preserves mixed file and COW PTEs\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	if (argc == 2 && !strcmp(mode, "--boundary-exec"))
		exec_compat(argv[0], "--boundary", NULL);
	if ((argc == 3 || argc == 4) && !strcmp(mode, "--case-exec"))
		exec_compat(argv[0], "--case", argv[2],
			    argc == 4 ? argv[3] : "1", NULL);
	if (argc == 4 && !strcmp(mode, "--case")) {
		unsigned long test = strtoul(argv[2], NULL, 10);
		unsigned long repeat = strtoul(argv[3], NULL, 10);

		if (!ppps_is_compat_process() || test < 1 || test > 14 ||
		    !repeat)
			return EXIT_FAILURE;
		for (unsigned long i = 0; i < repeat; i++)
			if (!run_one_test(test))
				return EXIT_FAILURE;
		return EXIT_SUCCESS;
	}
	if (argc == 2 && !strcmp(mode, "--boundary"))
		return pte_boundary_fallback() ? EXIT_SUCCESS : EXIT_FAILURE;
	if (argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return run_test();
	return EXIT_FAILURE;
}
