// SPDX-License-Identifier: GPL-2.0
/*
 * Exercise the real /dev/ashmem driver from both sides of a PPPS kernel.
 *
 * A compat process gets 4K ashmem size/range validation, while automatically
 * selected mappings retain the native 16K alignment used throughout PPPS.
 * Explicit, available 4K-only-aligned addresses are still honored.  Since an
 * ashmem fd can cross a process boundary, the native child also verifies that
 * the same driver continues to expose its 16K ABI to native processes.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../../drivers/staging/android/uapi/ashmem.h"
#include "kselftest_ppps.h"

#define ALIGNMENT_TRIALS 32
#define RESERVE_SIZE (4 * NATIVE_PAGE_SIZE)

static int new_region(size_t size)
{
	int fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);

	if (fd < 0)
		return -1;
	if (ioctl(fd, ASHMEM_SET_SIZE, size)) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool default_mmaps_are_native_aligned(int fd)
{
	void *mappings[ALIGNMENT_TRIALS];
	unsigned int mapped;
	bool passed = true;

	for (mapped = 0; mapped < ALIGNMENT_TRIALS; mapped++) {
		mappings[mapped] = mmap(NULL, PROCESS_PAGE_SIZE,
					PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (mappings[mapped] == MAP_FAILED) {
			passed = false;
			break;
		}
		if ((uintptr_t)mappings[mapped] & (NATIVE_PAGE_SIZE - 1))
			passed = false;
	}
	while (mapped)
		munmap(mappings[--mapped], PROCESS_PAGE_SIZE);
	return passed;
}

static void *free_4k_only_aligned_range(void)
{
	unsigned char *reservation;
	void *hint;

	reservation = mmap(NULL, RESERVE_SIZE, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (reservation == MAP_FAILED)
		return MAP_FAILED;
	hint = reservation + PROCESS_PAGE_SIZE;
	if (munmap(reservation, RESERVE_SIZE))
		return MAP_FAILED;
	return hint;
}

static bool available_hint_is_honored(int fd, bool fixed)
{
	void *hint = free_4k_only_aligned_range();
	void *mapping;
	int flags = MAP_SHARED;

	if (hint == MAP_FAILED)
		return false;
	if (fixed)
		flags |= MAP_FIXED_NOREPLACE;
	mapping = mmap(hint, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       flags, fd, 0);
	if (mapping == MAP_FAILED)
		return false;
	munmap(mapping, PROCESS_PAGE_SIZE);
	return mapping == hint &&
	       ((uintptr_t)mapping & (NATIVE_PAGE_SIZE - 1)) != 0;
}

static int native_checks(void)
{
	struct ashmem_pin pin_4k = { .offset = 0, .len = PROCESS_PAGE_SIZE };
	struct ashmem_pin pin_16k = { .offset = 0, .len = NATIVE_PAGE_SIZE };
	void *mapping;
	int fd;
	int ret;

	if (getpagesize() != NATIVE_PAGE_SIZE) {
		fprintf(stderr, "native ashmem child has %d-byte pages\n",
			getpagesize());
		return EXIT_FAILURE;
	}
	fd = new_region(2 * NATIVE_PAGE_SIZE);
	if (fd < 0) {
		fprintf(stderr, "native ashmem open/setup failed: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED ||
	    ((uintptr_t)mapping & (NATIVE_PAGE_SIZE - 1))) {
		fprintf(stderr, "native ashmem default mmap failed or was unaligned\n");
		goto fail;
	}
	munmap(mapping, NATIVE_PAGE_SIZE);

	errno = 0;
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, PROCESS_PAGE_SIZE);
	if (mapping != MAP_FAILED || errno != EINVAL) {
		fprintf(stderr, "native ashmem accepted a 4K file offset\n");
		if (mapping != MAP_FAILED)
			munmap(mapping, NATIVE_PAGE_SIZE);
		goto fail;
	}
	errno = 0;
	if (ioctl(fd, ASHMEM_UNPIN, &pin_4k) != -1 || errno != EINVAL) {
		fprintf(stderr, "native ashmem accepted a 4K unpin range\n");
		goto fail;
	}
	if (ioctl(fd, ASHMEM_UNPIN, &pin_16k) ||
	    ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) != ASHMEM_IS_UNPINNED) {
		fprintf(stderr, "native ashmem 16K unpin/status failed\n");
		goto fail;
	}
	ret = ioctl(fd, ASHMEM_PIN, &pin_16k);
	if ((ret != ASHMEM_NOT_PURGED && ret != ASHMEM_WAS_PURGED) ||
	    ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) != ASHMEM_IS_PINNED) {
		fprintf(stderr, "native ashmem 16K pin/status failed\n");
		goto fail;
	}
	close(fd);
	return EXIT_SUCCESS;

fail:
	close(fd);
	return EXIT_FAILURE;
}

static int native_shared_checks(const char *fd_arg)
{
	struct ashmem_pin pin_16k = { .offset = 0, .len = NATIVE_PAGE_SIZE };
	char *end;
	long value;
	int fd;
	int ret;

	errno = 0;
	value = strtol(fd_arg, &end, 10);
	if (errno || *end || value < 0 || value > INT_MAX)
		return EXIT_FAILURE;
	fd = value;
	if (getpagesize() != NATIVE_PAGE_SIZE)
		return EXIT_FAILURE;
	if (ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) != ASHMEM_IS_UNPINNED)
		return EXIT_FAILURE;
	ret = ioctl(fd, ASHMEM_PIN, &pin_16k);
	if ((ret != ASHMEM_NOT_PURGED && ret != ASHMEM_WAS_PURGED) ||
	    ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) != ASHMEM_IS_PINNED)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

static bool native_shared_child_passes(int fd)
{
	char fd_arg[16];
	int fd_flags;
	pid_t pid;
	int status;

	fd_flags = fcntl(fd, F_GETFD);
	if (fd_flags < 0 || fcntl(fd, F_SETFD, fd_flags & ~FD_CLOEXEC))
		return false;
	snprintf(fd_arg, sizeof(fd_arg), "%d", fd);
	pid = fork();
	if (!pid) {
		ppps_execl(false, NULL, "--native-shared", fd_arg, NULL);
		_exit(EXIT_FAILURE);
	}
	if (fcntl(fd, F_SETFD, fd_flags))
		return false;
	if (pid < 0 || waitpid(pid, &status, 0) != pid)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

static bool native_child_passes(void)
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		return false;
	if (!pid) {
		ppps_execl(false, NULL, "--native", NULL);
		_exit(EXIT_FAILURE);
	}
	if (waitpid(pid, &status, 0) != pid)
		return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

static int purge_count_checks(int page_size)
{
	struct ashmem_pin full = { .offset = 0, .len = NATIVE_PAGE_SIZE };
	int expected = NATIVE_PAGE_SIZE / page_size;
	void *mapping;
	int empty;
	int fd;
	int ret;

	if (getpagesize() != page_size)
		return EXIT_FAILURE;
	fd = new_region(NATIVE_PAGE_SIZE);
	if (fd < 0) {
		fprintf(stderr, "ashmem purge setup failed: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "ashmem purge mmap failed: %s\n", strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}
	munmap(mapping, NATIVE_PAGE_SIZE);

	/* The ioctl is global; start from a known empty LRU in the isolated guest. */
	if (ioctl(fd, ASHMEM_PURGE_ALL_CACHES) < 0 ||
	    ioctl(fd, ASHMEM_UNPIN, &full)) {
		fprintf(stderr, "ashmem purge preparation failed: %s\n", strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}
	ret = ioctl(fd, ASHMEM_PURGE_ALL_CACHES);
	empty = ioctl(fd, ASHMEM_PURGE_ALL_CACHES);
	close(fd);
	if (ret != expected || empty != 0) {
		fprintf(stderr,
			"ashmem purge counts %d/%d, expected %d/0 for %d-byte pages\n",
			ret, empty, expected, page_size);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int native_purge_shared_checks(const char *fd_arg)
{
	char *end;
	long value;
	int empty;
	int ret;

	errno = 0;
	value = strtol(fd_arg, &end, 10);
	if (errno || *end || value < 0 || value > INT_MAX ||
	    getpagesize() != NATIVE_PAGE_SIZE)
		return EXIT_FAILURE;
	ret = ioctl((int)value, ASHMEM_PURGE_ALL_CACHES);
	empty = ioctl((int)value, ASHMEM_PURGE_ALL_CACHES);
	return ret == 1 && empty == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int mixed_purge_count_checks(void)
{
	struct ashmem_pin first = { .offset = 0, .len = PROCESS_PAGE_SIZE };
	char fd_arg[16];
	void *mapping;
	int fd_flags;
	int status;
	pid_t pid;
	int fd;

	ppps_require_compat();
	fd = new_region(NATIVE_PAGE_SIZE);
	if (fd < 0)
		return EXIT_FAILURE;
	mapping = mmap(NULL, NATIVE_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED || ioctl(fd, ASHMEM_PURGE_ALL_CACHES) < 0 ||
	    ioctl(fd, ASHMEM_UNPIN, &first))
		goto fail;
	munmap(mapping, NATIVE_PAGE_SIZE);
	mapping = MAP_FAILED;

	fd_flags = fcntl(fd, F_GETFD);
	if (fd_flags < 0 || fcntl(fd, F_SETFD, fd_flags & ~FD_CLOEXEC))
		goto fail;
	snprintf(fd_arg, sizeof(fd_arg), "%d", fd);
	pid = fork();
	if (!pid) {
		ppps_execl(false, NULL, "--purge-native-shared", fd_arg, NULL);
		_exit(EXIT_FAILURE);
	}
	if (fcntl(fd, F_SETFD, fd_flags) || pid < 0 ||
	    waitpid(pid, &status, 0) != pid)
		goto fail;
	close(fd);
	return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS ?
		EXIT_SUCCESS : EXIT_FAILURE;

fail:
	if (mapping != MAP_FAILED)
		munmap(mapping, NATIVE_PAGE_SIZE);
	close(fd);
	return EXIT_FAILURE;
}

static int compat_checks(void)
{
	struct ashmem_pin pin_4k = { .offset = 0, .len = PROCESS_PAGE_SIZE };
	struct ashmem_pin second_4k = {
		.offset = PROCESS_PAGE_SIZE,
		.len = PROCESS_PAGE_SIZE,
	};
	unsigned char *offset_mapping = MAP_FAILED;
	unsigned char *full_mapping = MAP_FAILED;
	void *mapping;
	bool coherent = false;
	int small_fd;
	int fd;
	int ret;

	ppps_require_compat();
	small_fd = new_region(PROCESS_PAGE_SIZE);
	if (small_fd < 0) {
		if (errno == ENOENT || errno == ENODEV)
			ksft_exit_skip("/dev/ashmem is unavailable\n");
		ksft_exit_fail_msg("ashmem open/setup failed: %s\n",
				   strerror(errno));
	}

	ksft_print_header();
	ksft_set_plan(13);
	ksft_test_result(default_mmaps_are_native_aligned(small_fd),
			 "default ashmem mappings are native-page aligned\n");
	ksft_test_result(available_hint_is_honored(small_fd, false),
			 "ashmem honors an available 4K-only-aligned hint\n");
	ksft_test_result(available_hint_is_honored(small_fd, true),
			 "ashmem honors an explicit 4K-only-aligned fixed address\n");

	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, small_fd, 0);
	ksft_test_result(mapping != MAP_FAILED, "map a 4K ashmem region\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);

	errno = 0;
	mapping = mmap(NULL, 2 * PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, small_fd, 0);
	ksft_test_result(mapping == MAP_FAILED && errno == EINVAL,
			 "reject an 8K mapping of a 4K ashmem region\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, 2 * PROCESS_PAGE_SIZE);
	close(small_fd);

	fd = new_region(2 * NATIVE_PAGE_SIZE);
	if (fd < 0)
		ksft_exit_fail_msg("large ashmem setup failed: %s\n",
				   strerror(errno));
	offset_mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ | PROT_WRITE,
			      MAP_SHARED, fd, PROCESS_PAGE_SIZE);
	ksft_test_result(offset_mapping != MAP_FAILED,
			 "map ashmem from a 4K file offset\n");
	if (offset_mapping != MAP_FAILED) {
		full_mapping = mmap(NULL, 2 * PROCESS_PAGE_SIZE,
				    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (full_mapping != MAP_FAILED) {
			offset_mapping[0] = 0x5a;
			coherent = full_mapping[PROCESS_PAGE_SIZE] == 0x5a;
		}
	}
	ksft_test_result(coherent,
			 "4K-offset ashmem mappings share the right slice\n");
	if (full_mapping != MAP_FAILED)
		munmap(full_mapping, 2 * PROCESS_PAGE_SIZE);
	if (offset_mapping != MAP_FAILED)
		munmap(offset_mapping, PROCESS_PAGE_SIZE);

	ksft_test_result(ioctl(fd, ASHMEM_UNPIN, &pin_4k) == 0,
			 "unpin a 4K ashmem range\n");
	ksft_test_result(ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) ==
			 ASHMEM_IS_UNPINNED,
			 "report the ashmem region as unpinned\n");
	ret = ioctl(fd, ASHMEM_PIN, &pin_4k);
	ksft_test_result(ret == ASHMEM_NOT_PURGED || ret == ASHMEM_WAS_PURGED,
			 "pin a 4K ashmem range\n");
	ksft_test_result(ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) ==
			 ASHMEM_IS_PINNED,
			 "report the ashmem region as pinned\n");
	ret = ioctl(fd, ASHMEM_UNPIN, &second_4k);
	ksft_test_result(ret == 0 && native_shared_child_passes(fd) &&
			 ioctl(fd, ASHMEM_GET_PIN_STATUS, &(struct ashmem_pin){ 0, 0 }) ==
			 ASHMEM_IS_PINNED,
			 "share 4K ashmem ranges with a native process\n");
	close(fd);

	ksft_test_result(native_child_passes(),
			 "retain native 16K ashmem mmap and pin semantics\n");
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (mode && argc == 2 && !strcmp(mode, "--native"))
		return native_checks();
	if (mode && argc == 3 && !strcmp(mode, "--native-shared"))
		return native_shared_checks(argv[2]);
	if (mode && argc == 2 && !strcmp(mode, "--purge-native"))
		return purge_count_checks(NATIVE_PAGE_SIZE);
	if (mode && argc == 2 && !strcmp(mode, "--purge-compat"))
		return purge_count_checks(PROCESS_PAGE_SIZE);
	if (mode && argc == 3 && !strcmp(mode, "--purge-native-shared"))
		return native_purge_shared_checks(argv[2]);
	if (mode && argc == 2 && !strcmp(mode, "--purge-mixed"))
		return mixed_purge_count_checks();
	if (mode && argc == 2 && !strcmp(mode, PPPS_RUN_FLAG))
		return compat_checks();
	if (argc != 1)
		return EXIT_FAILURE;
	if (!ppps_is_compat_process())
		exec_compat(argv[0], PPPS_RUN_FLAG, NULL);
	return compat_checks();
}
