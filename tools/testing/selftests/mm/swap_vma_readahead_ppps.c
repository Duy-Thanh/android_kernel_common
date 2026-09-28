// SPDX-License-Identifier: GPL-2.0
/*
 * Swapped-out 4K pages of a compat process fault back in with intact
 * contents through swap I/O, and a fault in the native page adjacent to the
 * previous one starts VMA-based swap readahead (swap_ra grows).  Swap
 * entries and the readahead window are per native page, so the test walks
 * the mapping in native-page steps.
 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "kselftest_ppps.h"

#define ADDRESS_ALIGN	(64 * 1024UL)
#define TEST_PAGES	64
#define TEST_SIZE	(TEST_PAGES * PROCESS_PAGE_SIZE)
#define RESERVE_SIZE	(TEST_SIZE + 2 * ADDRESS_ALIGN)
#define POLL_ATTEMPTS	300

static const char *page_cluster_path = "/proc/sys/vm/page-cluster";
static const char *vma_ra_path = "/sys/kernel/mm/swap/vma_ra_enabled";
static unsigned long saved_page_cluster;
static char saved_vma_ra[16];
static bool restore_page_cluster;
static bool restore_vma_ra;

static bool read_named_value(const char *path, const char *name,
			     unsigned long *value)
{
	char key[64];
	unsigned long current;
	char *line = NULL;
	size_t capacity = 0;
	FILE *file;

	file = fopen(path, "re");
	if (!file)
		return false;
	while (getline(&line, &capacity, file) >= 0) {
		if (sscanf(line, "%63s %lu", key, &current) == 2 &&
		    !strcmp(key, name)) {
			*value = current;
			free(line);
			fclose(file);
			return true;
		}
	}
	free(line);
	fclose(file);
	return false;
}

static bool read_ulong_file(const char *path, unsigned long *value)
{
	FILE *file = fopen(path, "re");
	bool success = file && fscanf(file, "%lu", value) == 1;

	if (file)
		fclose(file);
	return success;
}

static bool read_word_file(const char *path, char *word, size_t size)
{
	FILE *file = fopen(path, "re");
	bool success = file && fgets(word, size, file);

	if (file)
		fclose(file);
	if (success)
		word[strcspn(word, "\n")] = '\0';
	return success && word[0];
}

/* A sole loop device prevents an existing zram per-CPU cluster being reused. */
static bool verified_swap_fixture(void)
{
	char line[512], device[256], type[64], path[320];
	unsigned long rotational;
	const char *base;
	FILE *swaps;
	int count = 0;
	bool valid = false;

	swaps = fopen("/proc/swaps", "re");
	if (!swaps)
		return false;
	if (!fgets(line, sizeof(line), swaps))
		goto out;
	while (fgets(line, sizeof(line), swaps)) {
		if (sscanf(line, "%255s %63s", device, type) != 2)
			goto out;
		count++;
		if (count != 1 || strcmp(type, "partition"))
			goto out;
		base = strrchr(device, '/');
		base = base ? base + 1 : device;
		if (strncmp(base, "loop", 4) || !base[4])
			goto out;
		for (const char *digit = base + 4; *digit; digit++) {
			if (*digit < '0' || *digit > '9')
				goto out;
		}
		if (snprintf(path, sizeof(path),
			     "/sys/class/block/%s/queue/rotational", base) >=
		    (int)sizeof(path) ||
		    !read_ulong_file(path, &rotational) || rotational)
			goto out;
	}
	valid = count == 1;
out:
	fclose(swaps);
	return valid;
}

static bool write_text_file(const char *path, const char *text)
{
	FILE *file = fopen(path, "we");
	bool success;

	if (!file)
		return false;
	success = fputs(text, file) >= 0;
	if (fclose(file))
		success = false;
	return success;
}

static void restore_readahead_config(void)
{
	char value[32];

	if (restore_page_cluster) {
		snprintf(value, sizeof(value), "%lu\n", saved_page_cluster);
		write_text_file(page_cluster_path, value);
	}
	if (restore_vma_ra)
		write_text_file(vma_ra_path, saved_vma_ra);
}

static bool mapping_usage(const void *address, unsigned long *rss_bytes,
			  unsigned long *swap_bytes)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end, value_kb;
	char *line = NULL;
	size_t capacity = 0;
	bool found_rss = false;
	bool found_swap = false;
	bool in_target = false;
	FILE *smaps;

	smaps = fopen("/proc/self/smaps", "re");
	if (!smaps)
		return false;
	while (getline(&line, &capacity, smaps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			if (in_target)
				break;
			in_target = target >= start && target < end;
			continue;
		}
		if (!in_target)
			continue;
		if (sscanf(line, "Rss: %lu kB", &value_kb) == 1) {
			*rss_bytes = value_kb * 1024;
			found_rss = true;
		} else if (sscanf(line, "Swap: %lu kB", &value_kb) == 1) {
			*swap_bytes = value_kb * 1024;
			found_swap = true;
		}
	}
	free(line);
	fclose(smaps);
	return found_rss && found_swap;
}

static bool mapping_span(const void *address, unsigned long *span)
{
	unsigned long target = (unsigned long)address;
	unsigned long start, end;
	char *line = NULL;
	size_t capacity = 0;
	bool found = false;
	FILE *maps;

	maps = fopen("/proc/self/maps", "re");
	if (!maps)
		return false;
	while (getline(&line, &capacity, maps) >= 0) {
		if (sscanf(line, "%lx-%lx", &start, &end) != 2)
			continue;
		if (target >= start && target < end) {
			*span = end - start;
			found = true;
			break;
		}
	}
	free(line);
	fclose(maps);
	return found;
}

static bool page_out_mapping(void *mapping, unsigned long *rss_bytes,
			     unsigned long *swap_bytes)
{
	unsigned int attempt;

	if (madvise(mapping, TEST_SIZE, MADV_PAGEOUT))
		return false;
	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!mapping_usage(mapping, rss_bytes, swap_bytes))
			return false;
		if (*swap_bytes >= TEST_SIZE / 2 && *rss_bytes <= TEST_SIZE / 2)
			return true;
		usleep(10000);
	}
	return false;
}

/* mincore reports up-to-date swap-cache folios for swapped anonymous PTEs. */
static bool cache_absent(unsigned char *mapping, unsigned int page)
{
	unsigned char resident[PPPS_SLICES];
	unsigned int i;

	if (mincore(mapping + page * NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE,
		    resident))
		return false;
	for (i = 0; i < PPPS_SLICES; i++) {
		if (resident[i] & 1)
			return false;
	}
	return true;
}

static bool cache_present(unsigned char *mapping, unsigned int page)
{
	unsigned char resident[PPPS_SLICES];
	unsigned int i;

	if (mincore(mapping + page * NATIVE_PAGE_SIZE, NATIVE_PAGE_SIZE,
		    resident))
		return false;
	for (i = 0; i < PPPS_SLICES; i++) {
		if (!(resident[i] & 1))
			return false;
	}
	return true;
}

static bool test_pages_cold(unsigned char *mapping)
{
	return cache_absent(mapping, 0) && cache_absent(mapping, 8) &&
	       cache_absent(mapping, 9) && cache_absent(mapping, 10);
}

/* Apply bounded pressure and stop as soon as the test pages are evicted. */
static bool evict_swap_cache(unsigned char *mapping)
{
	unsigned long available_kb = 0;
	struct timespec started, now;
	size_t budget, offset;
	unsigned char *pressure;
	bool cold;

	if (test_pages_cold(mapping))
		return true;
	if (!read_named_value("/proc/meminfo", "MemAvailable:",
			      &available_kb) || available_kb < 256 * 1024)
		return false;
	budget = available_kb * 1024 / 2;
	if (budget > 768UL * 1024 * 1024)
		budget = 768UL * 1024 * 1024;
	if (clock_gettime(CLOCK_MONOTONIC, &started))
		return false;
	pressure = mmap(NULL, budget, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pressure == MAP_FAILED)
		return false;
	madvise(pressure, budget, MADV_NOHUGEPAGE);
	cold = false;
	for (offset = 0; offset < budget; offset += PROCESS_PAGE_SIZE) {
		pressure[offset] = offset / PROCESS_PAGE_SIZE;
		if ((offset & ((32UL * 1024 * 1024) - 1)) == 0) {
			if (test_pages_cold(mapping)) {
				cold = true;
				break;
			}
			if (clock_gettime(CLOCK_MONOTONIC, &now) ||
			    now.tv_sec - started.tv_sec >= 10)
				break;
		}
	}
	cold = cold || test_pages_cold(mapping);
	munmap(pressure, budget);
	return cold;
}

static void skip_remaining(unsigned int count, const char *reason)
{
	while (count--)
		ksft_test_result_skip("%s\n", reason);
	ksft_finished();
}

static long major_faults(void)
{
	struct rusage usage;

	if (getrusage(RUSAGE_SELF, &usage))
		return -1;
	return usage.ru_majflt;
}

static bool fault_and_check(unsigned char *mapping, unsigned int page,
			    long *major_delta)
{
	long before = major_faults();
	unsigned char value = mapping[page * NATIVE_PAGE_SIZE];
	long after = major_faults();

	if (before < 0 || after < 0)
		return false;
	*major_delta = after - before;
	return value == (unsigned char)(0x40 + page);
}


static int run_test(void)
{
	unsigned long total_swap_kb = 0;
	unsigned long rss_bytes = 0;
	unsigned long swap_bytes = 0;
	unsigned long span = 0;
	unsigned long swap_ra_before = 0;
	unsigned long swap_ra_after = 0;
	unsigned char *reservation;
	unsigned char *mapping;
	uintptr_t aligned;
	long major_delta[3] = {};
	bool configured;
	bool mapped;
	bool paged_out;
	bool preserved[3];
	unsigned int i;

	if (!verified_swap_fixture())
		ksft_exit_skip("requires sole non-rotational loop swap fixture\n");
	ksft_print_header();
	ksft_set_plan(12);
	if (atexit(restore_readahead_config))
		ksft_exit_fail_msg("could not register configuration cleanup\n");

	if (!read_named_value("/proc/meminfo", "SwapTotal:",
			      &total_swap_kb) || !total_swap_kb)
		ksft_exit_skip("active swap is required\n");
	ksft_test_result_pass("swap is active\n");

	restore_page_cluster = read_ulong_file(page_cluster_path,
					       &saved_page_cluster);
	restore_vma_ra = read_word_file(vma_ra_path, saved_vma_ra,
					sizeof(saved_vma_ra));
	configured = restore_page_cluster && restore_vma_ra &&
		write_text_file(page_cluster_path, "1\n") &&
		write_text_file(vma_ra_path, "true\n");
	ksft_test_result(configured,
			 "enable a two-page VMA readahead window\n");
	if (!configured)
		ksft_exit_fail_msg("could not configure swap readahead\n");

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	aligned = reservation == MAP_FAILED ? 0 :
		((uintptr_t)reservation + 2 * ADDRESS_ALIGN - 1) &
		~(uintptr_t)(ADDRESS_ALIGN - 1);
	mapping = aligned ? mmap((void *)aligned, TEST_SIZE,
				 PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
				 -1, 0) : MAP_FAILED;
	mapped = mapping != MAP_FAILED && !(aligned & (ADDRESS_ALIGN - 1)) &&
		/* Compat VMAs are non-THP and reject THP advice. */
		(!madvise((void *)mapping, TEST_SIZE, MADV_NOHUGEPAGE) ||
		 errno == EINVAL) &&
		mapping_span((const void *)mapping, &span) && span == TEST_SIZE;
	ksft_test_result(mapped,
			 "map an isolated native-aligned 4K-page VMA\n");
	if (!mapped)
		ksft_exit_fail_msg("could not create test mapping\n");

	for (i = 0; i < TEST_PAGES; i++)
		mapping[i * PROCESS_PAGE_SIZE] = 0x40 + i / PPPS_SLICES;
	paged_out = page_out_mapping((void *)mapping, &rss_bytes, &swap_bytes);
	ksft_test_result(paged_out,
			 "page out the VMA (Swap: %lu, Rss: %lu bytes)\n",
			 swap_bytes, rss_bytes);
	if (!paged_out)
		ksft_exit_fail_msg("could not page out test mapping\n");
	if (!evict_swap_cache(mapping))
		skip_remaining(8, "bounded pressure did not evict target/neighbor");
	ksft_print_msg("test pages absent from swap cache before faults\n");

	preserved[0] = fault_and_check(mapping, 0, &major_delta[0]);
	ksft_test_result(preserved[0], "fault native page 0 with intact contents\n");
	ksft_test_result(major_delta[0] > 0,
			 "page 0 requires swap I/O\n");

	preserved[1] = fault_and_check(mapping, 8, &major_delta[1]);
	ksft_test_result(preserved[1], "fault nonadjacent native page 8 with intact contents\n");
	ksft_test_result(major_delta[1] > 0,
			 "page 8 requires swap I/O and resets history\n");

	if (!cache_absent(mapping, 9) || !cache_absent(mapping, 10))
		skip_remaining(4, "adjacent target/neighbor already cached");
	if (!read_named_value("/proc/vmstat", "swap_ra", &swap_ra_before))
		ksft_exit_fail_msg("could not read swap_ra before fault\n");
	preserved[2] = fault_and_check(mapping, 9, &major_delta[2]);
	if (!read_named_value("/proc/vmstat", "swap_ra", &swap_ra_after))
		ksft_exit_fail_msg("could not read swap_ra after fault\n");
	ksft_test_result(preserved[2], "fault adjacent native page 9 with intact contents\n");
	ksft_test_result(major_delta[2] > 0,
			 "page 9 requires swap I/O\n");
	ksft_test_result(cache_present(mapping, 10),
			 "page 10 populated by adjacent-page readahead\n");
	ksft_test_result(swap_ra_after > swap_ra_before,
			 "adjacent native-page fault starts VMA readahead # before %lu after %lu\n",
			 swap_ra_before, swap_ra_after);

	munmap(reservation, RESERVE_SIZE);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
