// SPDX-License-Identifier: GPL-2.0
/* Exercise 4K swap headers on a native 16K PPPS kernel. */
#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <sys/syscall.h>

#include "kselftest_ppps.h"

#define TEST_FILE_SIZE	(64UL * 1024 * 1024)
#define SWAP_VERSION_OFFSET	1024
#define SWAP_LAST_PAGE_OFFSET	1028
#define SWAP_BADPAGES_OFFSET	1032
#define SWAP_BADPAGE_LIST_OFFSET	1536
#define SWAP_MAGIC	"SWAPSPACE2"
#define SWAP_MAGIC_SIZE	10

static const char *swap_path;
static bool swap_active;

static void store_u32(unsigned char *buffer, size_t offset, uint32_t value)
{
	memcpy(buffer + offset, &value, sizeof(value));
}

static void cleanup(void)
{
	if (swap_active)
		syscall(SYS_swapoff, swap_path);
	if (swap_path)
		unlink(swap_path);
}

static bool write_swap_header(size_t header_page_size, uint32_t last_page,
			      const uint32_t *badpages, uint32_t nr_badpages)
{
	unsigned char *header;
	uint32_t i;
	int error;
	int fd;
	bool ok = false;

	header = calloc(1, NATIVE_PAGE_SIZE);
	if (!header)
		return false;
	fd = open(swap_path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0)
		goto out_free;
	error = posix_fallocate(fd, 0, TEST_FILE_SIZE);
	if (error) {
		errno = error;
		goto out_close;
	}
	store_u32(header, SWAP_VERSION_OFFSET, 1);
	store_u32(header, SWAP_LAST_PAGE_OFFSET, last_page);
	store_u32(header, SWAP_BADPAGES_OFFSET, nr_badpages);
	for (i = 0; i < nr_badpages; i++)
		store_u32(header, SWAP_BADPAGE_LIST_OFFSET + i * sizeof(uint32_t),
			  badpages[i]);
	memcpy(header + header_page_size - SWAP_MAGIC_SIZE,
	       SWAP_MAGIC, SWAP_MAGIC_SIZE);
	if (pwrite(fd, header, header_page_size, 0) !=
	    (ssize_t)header_page_size ||
	    fsync(fd))
		goto out_close;
	ok = !close(fd);
	fd = -1;
out_close:
	if (fd >= 0)
		close(fd);
out_free:
	free(header);
	return ok;
}

static bool swap_entry(unsigned long *size_kb)
{
	char device[PATH_MAX];
	char type[32];
	char *line = NULL;
	size_t capacity = 0;
	unsigned long used_kb;
	bool found = false;
	int priority;
	FILE *file;

	file = fopen("/proc/swaps", "re");
	if (!file)
		return false;
	while (getline(&line, &capacity, file) >= 0) {
		if (sscanf(line, "%4095s %31s %lu %lu %d", device, type,
			   size_kb, &used_kb, &priority) == 5 &&
		    !strcmp(device, swap_path)) {
			found = true;
			break;
		}
	}
	free(line);
	fclose(file);
	return found;
}

static bool native_page_capacity(unsigned long size_kb)
{
	return size_kb && size_kb < TEST_FILE_SIZE / 1024 &&
	       !(size_kb % (NATIVE_PAGE_SIZE / 1024));
}

static bool current_header_is_rejected(void)
{
	int ret;

	errno = 0;
	ret = syscall(SYS_swapon, swap_path, 0);
	if (!ret) {
		swap_active = true;
		syscall(SYS_swapoff, swap_path);
		swap_active = false;
	}
	return ret == -1 && errno == EINVAL;
}

static int run_test(void)
{
	static const uint32_t badpages[] = { 0, 4, 5, 8, 9, 0xffffffff };
	const uint32_t compat_last_page =
		TEST_FILE_SIZE / PROCESS_PAGE_SIZE - 1;
	const uint32_t native_last_page =
		TEST_FILE_SIZE / NATIVE_PAGE_SIZE - 1;
	unsigned long compat_size_kb = 0;
	unsigned long native_size_kb = 0;
	int ret;

	ksft_print_header();
	swap_path = getenv("PPPS_SWAP_HEADER_FILE");
	if (!swap_path)
		ksft_exit_skip("PPPS_SWAP_HEADER_FILE is not set\n");
	if (geteuid())
		ksft_exit_skip("root is required for swapon(2)\n");
	ksft_set_plan(19);
	if (atexit(cleanup))
		ksft_exit_fail_msg("could not register cleanup\n");

	ksft_test_result(sysconf(_SC_PAGESIZE) == PROCESS_PAGE_SIZE,
			 "test process uses 4K pages\n");
	ksft_test_result(write_swap_header(NATIVE_PAGE_SIZE, native_last_page,
					   NULL, 0),
			 "create a swap file with a native header\n");
	ret = syscall(SYS_swapon, swap_path, 0);
	ksft_test_result(!ret, "accept the native swap header\n");
	if (ret)
		ksft_exit_fail_msg("native swapon failed: %s\n", strerror(errno));
	swap_active = true;
	ksft_test_result(swap_entry(&native_size_kb),
			 "list the native swap area\n");
	ksft_test_result(native_page_capacity(native_size_kb),
			 "report native-page capacity (%lu kB)\n",
			 native_size_kb);
	ret = syscall(SYS_swapoff, swap_path);
	ksft_test_result(!ret, "disable the native swap area\n");
	if (ret)
		ksft_exit_fail_msg("native swapoff failed: %s\n", strerror(errno));
	swap_active = false;

	ksft_test_result(write_swap_header(PROCESS_PAGE_SIZE, compat_last_page,
					   NULL, 0),
			 "create a physically allocated swap file with a 4K header\n");
	ret = syscall(SYS_swapon, swap_path, 0);
	ksft_test_result(!ret, "accept the 4K swap header\n");
	if (ret)
		ksft_exit_fail_msg("swapon failed: %s\n", strerror(errno));
	swap_active = true;
	ksft_test_result(swap_entry(&compat_size_kb),
			 "list the converted swap area\n");
	ksft_test_result(native_page_capacity(compat_size_kb),
			 "report converted native-page capacity (%lu kB)\n",
			 compat_size_kb);
	ret = syscall(SYS_swapoff, swap_path);
	ksft_test_result(!ret, "disable the converted swap area\n");
	if (ret)
		ksft_exit_fail_msg("swapoff failed: %s\n", strerror(errno));
	swap_active = false;

	ksft_test_result(write_swap_header(PROCESS_PAGE_SIZE, compat_last_page,
					   badpages, ARRAY_SIZE(badpages)),
			 "create a 4K header with duplicate and invalid bad pages\n");
	errno = 0;
	ret = syscall(SYS_swapon, swap_path, 0);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject bad pages in a regular swap file after conversion\n");

	ksft_test_result(write_swap_header(PROCESS_PAGE_SIZE, 2, NULL, 0),
			 "create a sub-native-page 4K swap header\n");
	errno = 0;
	ret = syscall(SYS_swapon, swap_path, 0);
	ksft_test_result(ret == -1 && errno == EINVAL,
			 "reject an empty area after 4K-to-native conversion\n");

	ksft_test_result(write_swap_header(PROCESS_PAGE_SIZE, compat_last_page,
					   (uint32_t[]){ 0 }, 1),
			 "create a 4K header containing badpage zero\n");
	ksft_test_result(current_header_is_rejected(),
			 "reject badpage zero before conversion\n");
	ksft_test_result(write_swap_header(PROCESS_PAGE_SIZE, compat_last_page,
					   (uint32_t[]){ UINT32_MAX }, 1),
			 "create a 4K header containing an out-of-range badpage\n");
	ksft_test_result(current_header_is_rejected(),
			 "reject an out-of-range badpage before conversion\n");

	ksft_finished();
}

int main(int argc, char **argv)
{
	return ppps_compat_main(argc, argv, run_test);
}
