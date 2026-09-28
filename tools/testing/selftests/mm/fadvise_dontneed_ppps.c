// SPDX-License-Identifier: GPL-2.0
/*
 * POSIX_FADV_DONTNEED honors a 4K process-page hint boundary. The native
 * page-cache folio may also evict neighboring slices.
 */
#define _GNU_SOURCE

#include <limits.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define FILE_PAGES 4UL
#define TARGET_PAGE 1UL
#define POLL_ATTEMPTS 20

static bool query_cache(int fd, __u64 offset, __u64 length, __u64 *nr_cache)
{
	struct cachestat_range range = {
		.off = offset,
		.len = length,
	};
	struct cachestat stat;

	memset(&stat, 0, sizeof(stat));
	if (syscall(__NR_cachestat, fd, &range, &stat, 0)) {
		ksft_print_msg("cachestat failed: %s\n", strerror(errno));
		return false;
	}
	*nr_cache = stat.nr_cache;
	return true;
}

static bool target_page_evicted(int fd)
{
	unsigned int attempt;
	__u64 nr_cache;

	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!query_cache(fd, TARGET_PAGE * PROCESS_PAGE_SIZE,
				 PROCESS_PAGE_SIZE, &nr_cache))
			return false;
		if (!nr_cache) {
			ksft_print_msg("target cache pages after DONTNEED: 0\n");
			return true;
		}
		usleep(10000);
	}
	ksft_print_msg("target page remained resident\n");
	return false;
}

static int run_test(void)
{
	char path[PATH_MAX];
	const char *tmp = getenv("TMPDIR");
	unsigned char data[PROCESS_PAGE_SIZE];
	__u64 nr_cache = 0;
	int pathlen;
	int error;
	int fd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(7);
	if (!tmp)
		tmp = ".";
	pathlen = snprintf(path, sizeof(path), "%s/fadvise-dontneed-%ld", tmp,
			   (long)getpid());
	if (pathlen < 0 || (size_t)pathlen >= sizeof(path))
		ksft_exit_fail_msg("temporary path is too long\n");

	fd = open(path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	ksft_test_result(fd >= 0, "create the backing file\n");
	if (fd < 0)
		ksft_exit_fail_msg("open failed: %s\n", strerror(errno));

	memset(data, 0x5a, sizeof(data));
	ksft_test_result(!ftruncate(fd, FILE_PAGES * PROCESS_PAGE_SIZE) &&
			 pwrite(fd, data, sizeof(data),
				TARGET_PAGE * PROCESS_PAGE_SIZE) ==
			 (ssize_t)sizeof(data) && !fsync(fd),
			 "populate and clean the target process page\n");

	ksft_test_result(query_cache(fd, TARGET_PAGE * PROCESS_PAGE_SIZE,
				     PROCESS_PAGE_SIZE, &nr_cache) && nr_cache == 1,
			 "target process page starts resident\n");

	error = posix_fadvise(fd, TARGET_PAGE * PROCESS_PAGE_SIZE + 1,
			      PROCESS_PAGE_SIZE - 2, POSIX_FADV_DONTNEED);
	ksft_test_result(!error, "advise away part of a process page\n");
	ksft_test_result(!error &&
			 query_cache(fd, TARGET_PAGE * PROCESS_PAGE_SIZE,
				     PROCESS_PAGE_SIZE, &nr_cache) && nr_cache == 1,
			 "partial process page remains cached\n");

	error = posix_fadvise(fd, TARGET_PAGE * PROCESS_PAGE_SIZE,
			      PROCESS_PAGE_SIZE, POSIX_FADV_DONTNEED);
	ksft_test_result(!error, "advise away one complete process page\n");
	ksft_test_result(!error && target_page_evicted(fd),
			 "DONTNEED evicts the requested process page\n");

	close(fd);
	unlink(path);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
