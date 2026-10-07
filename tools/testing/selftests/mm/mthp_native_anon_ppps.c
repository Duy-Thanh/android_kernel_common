// SPDX-License-Identifier: GPL-2.0
/*
 * A PPPS kernel must keep the native 16K anonymous fault path identical to
 * upstream.  With an mTHP size enabled (sysfs hugepages-<size>kB/enabled),
 * an anonymous write fault in a native process may allocate a large folio;
 * the fault must then map the whole folio, i.e. every 16K page of the
 * aligned block is present right after a single write, backed by contiguous
 * PFNs.  Mapping only the faulting page of a large folio corrupts the folio's
 * mapcount, refcount and rmap index.
 *
 * Root is required to enable the mTHP size; the previous setting is
 * restored.  4K compat processes never get large anonymous folios and are
 * not tested here.
 */
#define _GNU_SOURCE

#include <dirent.h>
#include <limits.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define THP_ROOT	"/sys/kernel/mm/transparent_hugepage"
#define BLOCKS		16

static char enabled_path[PATH_MAX];
static char saved_setting[32];
static char stats_path[PATH_MAX];

static bool read_line(const char *path, char *buf, size_t size)
{
	FILE *file = fopen(path, "re");
	bool ok;

	if (!file)
		return false;
	ok = fgets(buf, size, file) != NULL;
	fclose(file);
	if (ok)
		buf[strcspn(buf, "\n")] = '\0';
	return ok;
}

static bool write_str(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t len = strlen(value), ret;

	if (fd < 0)
		return false;
	ret = write(fd, value, len);
	close(fd);
	return ret == len;
}

/* "always inherit [madvise] never" -> "madvise". */
static void bracketed(const char *line, char *out, size_t size)
{
	const char *open = strchr(line, '['), *close;

	out[0] = '\0';
	if (!open || !(close = strchr(open, ']')))
		return;
	snprintf(out, size, "%.*s", (int)(close - open - 1), open + 1);
}

static void restore(void)
{
	if (saved_setting[0])
		write_str(enabled_path, saved_setting);
}

static long fault_alloc_count(void)
{
	char line[64];

	if (!read_line(stats_path, line, sizeof(line)))
		return -1;
	return atol(line);
}

/* Smallest anonymous mTHP size above the native page the kernel offers. */
#define KPF_COMPOUND_HEAD	(1ULL << 15)
#define KPF_COMPOUND_TAIL	(1ULL << 16)
#define KPF_THP			(1ULL << 22)

static bool kpageflags(uint64_t pfn, uint64_t *flags)
{
	int fd = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
	bool ok;

	if (fd < 0)
		return false;
	ok = pread(fd, flags, sizeof(*flags), pfn * sizeof(*flags)) ==
	     sizeof(*flags);
	close(fd);
	return ok;
}

static unsigned long pick_size(void)
{
	unsigned long best = 0, kb;
	struct dirent *entry;
	DIR *dir = opendir(THP_ROOT);

	if (!dir)
		return 0;
	while ((entry = readdir(dir))) {
		if (sscanf(entry->d_name, "hugepages-%lukB", &kb) != 1)
			continue;
		char path[PATH_MAX];

		if (kb * 1024 <= NATIVE_PAGE_SIZE || kb * 1024 > 64 * NATIVE_PAGE_SIZE)
			continue;
		/* Sizes without an anonymous "enabled" control do not count. */
		snprintf(path, sizeof(path), THP_ROOT "/%s/enabled",
			 entry->d_name);
		if (access(path, W_OK))
			continue;
		if (!best || kb < best)
			best = kb;
	}
	closedir(dir);
	return best * 1024;
}

int main(void)
{
	unsigned long folio_size, pages_per_folio, block, i;
	unsigned char *raw, *area;
	long before, after;
	char line[128];
	unsigned int partial = 0, mapped_whole = 0, checked = 0, unknown = 0;
	bool data_ok = true;

	ksft_print_header();
	if (getpagesize() != (int)NATIVE_PAGE_SIZE) {
		if (ppps_is_compat_process())
			exec_native(NULL, NULL);
		ksft_exit_skip("requires a native 16K process\n");
	}
	if (geteuid())
		ksft_exit_skip("root is required to enable an mTHP size\n");
	folio_size = pick_size();
	if (!folio_size)
		ksft_exit_skip("no anonymous mTHP size is offered\n");
	pages_per_folio = folio_size / NATIVE_PAGE_SIZE;
	snprintf(enabled_path, sizeof(enabled_path),
		 THP_ROOT "/hugepages-%lukB/enabled", folio_size / 1024);
	snprintf(stats_path, sizeof(stats_path),
		 THP_ROOT "/hugepages-%lukB/stats/anon_fault_alloc",
		 folio_size / 1024);
	if (!read_line(enabled_path, line, sizeof(line)))
		ksft_exit_skip("cannot read %s\n", enabled_path);
	bracketed(line, saved_setting, sizeof(saved_setting));
	ksft_print_msg("mTHP size %lu kB, previous setting \"%s\"\n",
		       folio_size / 1024, saved_setting);
	atexit(restore);
	if (!write_str(enabled_path, "always"))
		ksft_exit_skip("cannot enable %s: %s\n", enabled_path,
			       strerror(errno));
	ksft_set_plan(2);

	raw = mmap(NULL, (BLOCKS + 1) * folio_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (raw == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	area = (unsigned char *)(((uintptr_t)raw + folio_size - 1) &
				 ~(folio_size - 1));
	madvise(area, BLOCKS * folio_size, MADV_HUGEPAGE);

	/* Keep the system-wide setting changed for as short as possible. */
	before = fault_alloc_count();
	for (block = 0; block < BLOCKS; block++)
		area[block * folio_size] = 0x40 + block;
	after = fault_alloc_count();
	restore();
	ksft_print_msg("anon_fault_alloc %ld -> %ld\n", before, after);
	if (before < 0 || after <= before) {
		munmap(raw, (BLOCKS + 1) * folio_size);
		ksft_exit_skip("no %lu kB folio was allocated by the faults\n",
			       folio_size / 1024);
	}

	for (block = 0; block < BLOCKS; block++) {
		unsigned char *p = area + block * folio_size;
		uint64_t first = 0, pfn, flags;
		unsigned int present = 0;
		bool contiguous = true;

		for (i = 0; i < pages_per_folio; i++) {
			if (!ppps_pfn(p + i * NATIVE_PAGE_SIZE, &pfn))
				continue;
			if (!present)
				first = pfn - i;
			else if (pfn != first + i)
				contiguous = false;
			present++;
		}
		checked++;
		if (present == pages_per_folio && contiguous &&
		    !(first % pages_per_folio)) {
			mapped_whole++;
			continue;
		}
		/* A lone PTE must map an order-0 folio. */
		if (!ppps_pfn(p, &pfn) || !kpageflags(pfn, &flags)) {
			unknown++;
			continue;
		}
		if (flags & (KPF_COMPOUND_HEAD | KPF_COMPOUND_TAIL | KPF_THP)) {
			ksft_print_msg("block %lu: %u of %lu pages mapped, pfn %#llx is part of a large folio (kpageflags %#llx)\n",
				       block, present, pages_per_folio,
				       (unsigned long long)pfn,
				       (unsigned long long)flags);
			partial++;
		}
	}
	ksft_print_msg("%u blocks: %u fully mapped, %u large folios mapped partially, %u unknown\n",
		       checked, mapped_whole, partial, unknown);
	ksft_test_result(!partial && !unknown && mapped_whole,
			 "every %lu kB folio is mapped in full by its fault\n",
			 folio_size / 1024);

	for (block = 0; block < BLOCKS; block++)
		for (i = 1; i < pages_per_folio; i++)
			area[block * folio_size + i * NATIVE_PAGE_SIZE] = 0x80 + i;
	for (block = 0; block < BLOCKS && data_ok; block++) {
		unsigned char *p = area + block * folio_size;

		if (p[0] != 0x40 + block)
			data_ok = false;
		for (i = 1; i < pages_per_folio; i++)
			if (p[i * NATIVE_PAGE_SIZE] != 0x80 + i)
				data_ok = false;
	}
	ksft_test_result(data_ok, "data written to every page reads back\n");
	munmap(raw, (BLOCKS + 1) * folio_size);
	ksft_finished();
}
