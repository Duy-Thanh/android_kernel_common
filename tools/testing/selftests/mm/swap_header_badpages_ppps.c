// SPDX-License-Identifier: GPL-2.0
/*
 * swapon(2) of an area formatted with a 4K swap header converts the header
 * to native 16K units before validating it.  The bad-page list lives in the
 * header page, so its length must be checked against the header page before
 * the conversion walks, sorts and rewrites it, and the walk must not index
 * the one-element badpages[] declaration of union swap_header (with
 * -fstrict-flex-arrays=3 and CONFIG_UBSAN_BOUNDS that is an out-of-bounds
 * access the moment a second entry is touched).
 *
 * Two headers are tried, each must be rejected with EINVAL without a kernel
 * report: one with two bad pages (a valid count; regular swap files simply
 * may not have bad pages) and one whose bad-page list runs 16 bytes past the
 * 16K page cache folio holding the header.  The header file is exactly one
 * native page long so that folio cannot be larger; an overrun then lands in
 * a neighbouring physical page, which KASAN (HW tags) reports.  Root is
 * required; set PPPS_SWAP_HEADER_DIR to a directory on a filesystem that
 * supports swap files (default: the current directory).
 */
#define _GNU_SOURCE

#include <limits.h>
#include <sys/klog.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define SWAP_VERSION_OFFSET		1024
#define SWAP_LAST_PAGE_OFFSET		1028
#define SWAP_BADPAGES_OFFSET		1032
#define SWAP_BADPAGE_LIST_OFFSET	1536
#define SWAP_MAGIC			"SWAPSPACE2"
#define SWAP_MAGIC_SIZE			10
/* Entries that fit in a native 16K header page. */
#define NATIVE_BADPAGE_SLOTS \
	((NATIVE_PAGE_SIZE - SWAP_BADPAGE_LIST_OFFSET) / sizeof(uint32_t))

#define SYSLOG_ACTION_READ_ALL		3
#define SYSLOG_ACTION_SIZE_BUFFER	10

static char swap_path[PATH_MAX];

static void cleanup(void)
{
	if (swap_path[0]) {
		syscall(SYS_swapoff, swap_path);
		unlink(swap_path);
	}
}

static void store_u32(unsigned char *buffer, size_t offset, uint32_t value)
{
	memcpy(buffer + offset, &value, sizeof(value));
}

static bool write_header(uint32_t nr_badpages)
{
	unsigned char *header = calloc(1, NATIVE_PAGE_SIZE);
	uint32_t i, slots;
	int fd;
	bool ok = false;

	if (!header)
		return false;
	unlink(swap_path);
	fd = open(swap_path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0)
		goto out;
	store_u32(header, SWAP_VERSION_OFFSET, 1);
	store_u32(header, SWAP_LAST_PAGE_OFFSET, 0x10000);
	store_u32(header, SWAP_BADPAGES_OFFSET, nr_badpages);
	/* Distinct entries so deduplication cannot shrink the list. */
	slots = nr_badpages < NATIVE_BADPAGE_SLOTS ? nr_badpages :
		NATIVE_BADPAGE_SLOTS;
	for (i = 0; i < slots; i++)
		store_u32(header, SWAP_BADPAGE_LIST_OFFSET + i * 4,
			  (i + 1) * 4);
	/* 4K header: the signature sits at the end of the first 4K. */
	memcpy(header + PROCESS_PAGE_SIZE - SWAP_MAGIC_SIZE, SWAP_MAGIC,
	       SWAP_MAGIC_SIZE);
	ok = pwrite(fd, header, NATIVE_PAGE_SIZE, 0) == NATIVE_PAGE_SIZE &&
	     !fsync(fd);
	close(fd);
out:
	free(header);
	return ok;
}

/* Count kernel log lines that look like a memory-safety report. */
static int kernel_reports(void)
{
	int size = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0), len, count = 0;
	char *log, *line, *save = NULL;

	if (size <= 0)
		return -1;
	log = malloc(size + 1);
	if (!log)
		return -1;
	len = klogctl(SYSLOG_ACTION_READ_ALL, log, size);
	if (len < 0) {
		free(log);
		return -1;
	}
	log[len] = '\0';
	for (line = strtok_r(log, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		if (strstr(line, "BUG: KASAN") || strstr(line, "BUG: unable") ||
		    strstr(line, "Unable to handle kernel") ||
		    strstr(line, "Internal error") || strstr(line, "UBSAN"))
			count++;
	}
	free(log);
	return count;
}

static bool attempt(uint32_t nr_badpages, const char *what)
{
	int before, after, ret, saved_errno;

	if (!write_header(nr_badpages))
		ksft_exit_fail_msg("cannot write %s: %s\n", swap_path,
				   strerror(errno));
	before = kernel_reports();
	errno = 0;
	ret = syscall(SYS_swapon, swap_path, 0);
	saved_errno = errno;
	if (!ret)
		syscall(SYS_swapoff, swap_path);
	after = kernel_reports();
	ksft_print_msg("%s: nr_badpages %u: swapon = %d (%s), kernel reports %d -> %d\n",
		       what, nr_badpages, ret, strerror(saved_errno), before,
		       after);
	return ret == -1 && saved_errno == EINVAL && before >= 0 &&
	       after == before;
}

int main(void)
{
	const char *dir = getenv("PPPS_SWAP_HEADER_DIR");

	ksft_print_header();
	if (geteuid())
		ksft_exit_skip("root is required for swapon(2)\n");
	snprintf(swap_path, sizeof(swap_path), "%s/ppps_badpages_%d.swap",
		 dir ? dir : ".", (int)getpid());
	atexit(cleanup);
	if (kernel_reports() < 0)
		ksft_exit_skip("cannot read the kernel log\n");
	/* KASAN stays quiet after its first report. */
	if (kernel_reports() > 0)
		ksft_exit_skip("the kernel log already holds a KASAN/BUG report\n");
	ksft_set_plan(2);

	ksft_test_result(attempt(2, "two bad pages"),
			 "a 4K header with two bad pages is rejected safely\n");
	/*
	 * KASAN reports the first bad access and stays quiet afterwards, and
	 * the overrun corrupts 16 bytes of a page that belongs to somebody
	 * else, so the overrun is the last attempt.
	 */
	ksft_test_result(attempt(NATIVE_BADPAGE_SLOTS + 4, "16 bytes past the page"),
			 "a 4K header whose bad-page list overruns the header page is rejected safely\n");
	ksft_finished();
}
