// SPDX-License-Identifier: GPL-2.0
/*
 * MADV_WILLNEED on a two-4K-page file range that straddles two native pages
 * is accepted by a 4K compat process and brings both native pages back into
 * the page cache after eviction.
 */
#define _GNU_SOURCE

#include <sys/mman.h>

#include "kselftest_ppps.h"

#define MAPPING_OFFSET	(3 * PROCESS_PAGE_SIZE)
#define MAPPING_LENGTH	(2 * PROCESS_PAGE_SIZE)
#define FILE_SIZE	(64 * PROCESS_PAGE_SIZE)
#define POLL_ATTEMPTS	100

static bool initialize_file(int fd)
{
	unsigned char *contents;
	ssize_t written;
	size_t i;
	bool passed = false;

	contents = malloc(FILE_SIZE);
	if (!contents)
		return false;
	for (i = 0; i < FILE_SIZE; i++)
		contents[i] = 0x31 + (i / PROCESS_PAGE_SIZE) % 31;
	written = pwrite(fd, contents, FILE_SIZE, 0);
	if (written != (ssize_t)FILE_SIZE) {
		ksft_print_msg("pwrite returned %zd: %s\n", written,
			       strerror(errno));
		goto out;
	}
	if (fsync(fd)) {
		ksft_print_msg("fsync failed: %s\n", strerror(errno));
		goto out;
	}
	passed = true;

out:
	free(contents);
	return passed;
}

static bool get_residency(void *mapping, unsigned char vec[2])
{
	memset(vec, 0xff, 2);
	if (mincore(mapping, MAPPING_LENGTH, vec)) {
		ksft_print_msg("mincore failed: %s\n", strerror(errno));
		return false;
	}
	vec[0] &= 1;
	vec[1] &= 1;
	return true;
}

static bool evict_mapping(int fd, void *mapping)
{
	unsigned char vec[2];
	unsigned int attempt;

	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		int error = posix_fadvise(fd, 0, FILE_SIZE, POSIX_FADV_DONTNEED);

		if (error) {
			ksft_print_msg("POSIX_FADV_DONTNEED failed: %s\n",
				       strerror(error));
			return false;
		}
		if (!get_residency(mapping, vec))
			return false;
		if (!vec[0] && !vec[1])
			return true;
		usleep(10000);
	}
	ksft_print_msg("could not evict mapped range: vec=%u,%u\n",
		       vec[0], vec[1]);
	return false;
}

static bool wait_for_willneed(void *mapping)
{
	unsigned char vec[2];
	unsigned int attempt;

	for (attempt = 0; attempt < POLL_ATTEMPTS; attempt++) {
		if (!get_residency(mapping, vec))
			return false;
		if (vec[0] && vec[1])
			return true;
		usleep(10000);
	}
	ksft_print_msg("post-MADV_WILLNEED residency: vec=%u,%u\n",
		       vec[0], vec[1]);
	return false;
}

static int run_test(const char *path)
{
	void *mapping = MAP_FAILED;
	bool evicted;
	int fd = -1;
	int ret;

	ksft_print_header();

	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
	if (fd < 0)
		ksft_exit_fail_msg("open(%s) failed: %s\n", path,
				   strerror(errno));
	if (!initialize_file(fd))
		ksft_exit_fail_msg("failed to initialize backing file\n");
	mapping = mmap(NULL, MAPPING_LENGTH, PROT_READ, MAP_SHARED, fd,
		       MAPPING_OFFSET);
	if (mapping == MAP_FAILED)
		ksft_exit_fail_msg("mmap failed: %s\n", strerror(errno));

	evicted = evict_mapping(fd, mapping);
	if (!evicted)
		ksft_exit_skip("backing filesystem could not evict test pages\n");
	ksft_set_plan(2);

	errno = 0;
	ret = madvise(mapping, MAPPING_LENGTH, MADV_WILLNEED);
	if (ret)
		ksft_print_msg("MADV_WILLNEED failed: %s\n", strerror(errno));
	ksft_test_result(!ret, "MADV_WILLNEED accepts the 4K range\n");
	ksft_test_result(!ret && wait_for_willneed(mapping),
			 "MADV_WILLNEED reads both native pages in the range\n");

	munmap(mapping, MAPPING_LENGTH);
	close(fd);
	unlink(path);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode) {
		if (argc != 2)
			ksft_exit_fail_msg("usage: %s FILE\n", argv[0]);
		if (!ppps_is_compat_process())
			exec_compat(argv[0], PPPS_RUN_FLAG, argv[1], NULL);
		return run_test(argv[1]);
	}
	if (argc == 3 && !strcmp(mode, PPPS_RUN_FLAG)) {
		ppps_require_compat();
		return run_test(argv[2]);
	}
	return EXIT_FAILURE;
}
