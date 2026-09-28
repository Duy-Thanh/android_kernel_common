// SPDX-License-Identifier: GPL-2.0
/*
 * videobuf2 mmap of a 6K plane through the vb2_mmap_ppps fixture: a 4K compat
 * process maps it rounded to two 4K pages, oversized or wrong-cookie mappings
 * fail with EINVAL, and the exported DMA-BUF retains its native-page allocation.
 * The --native role checks the same device in native page units.
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "kselftest_ppps.h"

#define TEST_PLANE_SIZE (6 * 1024UL)
#define VALID_MAP_SIZE (2 * PROCESS_PAGE_SIZE)
#define OVERSIZED_MAP_SIZE (3 * PROCESS_PAGE_SIZE)
#define VB2_MMAP_PPPS_EXPBUF _IO('v', 0x70)

static int run_native_test(void)
{
	long page_size = sysconf(_SC_PAGESIZE);
	size_t valid_map_size;
	void *mapping;
	struct stat st = {};
	int saved_errno;
	int dmabuf_fd;
	int fd;

	ksft_print_header();
	ksft_set_plan(6);
	ksft_test_result(page_size == PROCESS_PAGE_SIZE ||
			 page_size == NATIVE_PAGE_SIZE,
			 "native process uses a supported page size (%ld)\n",
			 page_size);
	if (page_size != PROCESS_PAGE_SIZE && page_size != NATIVE_PAGE_SIZE)
		ksft_exit_fail_msg("unsupported native page size: %ld\n",
				   page_size);
	valid_map_size = (TEST_PLANE_SIZE + page_size - 1) & ~(page_size - 1);

	fd = ppps_open_fixture_or_skip("/dev/vb2_mmap_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open the videobuf2 mmap test device\n");
	dmabuf_fd = ioctl(fd, VB2_MMAP_PPPS_EXPBUF);
	ksft_test_result(dmabuf_fd >= 0, "export the plane as a DMA-BUF\n");
	if (dmabuf_fd < 0)
		ksft_exit_fail_msg("export DMA-BUF failed: %s\n",
				   strerror(errno));
	ksft_test_result(fstat(dmabuf_fd, &st) == 0 &&
			 st.st_size >= (off_t)TEST_PLANE_SIZE &&
			 st.st_size <= (off_t)valid_map_size,
			 "DMA-BUF fits its native process-page range (%lld)\n",
			 (long long)st.st_size);
	mapping = mmap(NULL, valid_map_size, PROT_READ, MAP_SHARED,
		       dmabuf_fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map the DMA-BUF through its final native process page\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, valid_map_size);

	errno = 0;
	mapping = mmap(NULL, valid_map_size + page_size, PROT_READ, MAP_SHARED,
		       dmabuf_fd, 0);
	saved_errno = errno;
	ksft_test_result(mapping == MAP_FAILED && saved_errno == EINVAL,
			 "reject a mapping one native process page too long (errno=%d)\n",
			 saved_errno);
	if (mapping != MAP_FAILED)
		munmap(mapping, valid_map_size + page_size);
	close(dmabuf_fd);
	close(fd);
	ksft_finished();
}

static int run_test(void)
{
	void *mapping;
	struct stat st = {};
	int saved_errno;
	int dmabuf_fd;
	int fd;

	ppps_require_compat();
	ksft_print_header();
	ksft_set_plan(8);
	fd = ppps_open_fixture_or_skip("/dev/vb2_mmap_ppps", O_RDWR);
	ksft_test_result(fd >= 0, "open the videobuf2 mmap test device\n");

	mapping = mmap(NULL, VALID_MAP_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map the 6K plane rounded to two process pages\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, VALID_MAP_SIZE);

	errno = 0;
	mapping = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
		       PROCESS_PAGE_SIZE);
	saved_errno = errno;
	ksft_test_result(mapping == MAP_FAILED && saved_errno == EINVAL,
			 "reject a cookie selecting a nonexistent plane (errno=%d)\n",
			 saved_errno);
	if (mapping != MAP_FAILED)
		munmap(mapping, PROCESS_PAGE_SIZE);

	errno = 0;
	mapping = mmap(NULL, OVERSIZED_MAP_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	saved_errno = errno;
	ksft_test_result(mapping == MAP_FAILED && saved_errno == EINVAL,
			 "reject a 12K mapping of the 6K plane (errno=%d)\n",
			 saved_errno);
	if (mapping != MAP_FAILED)
		munmap(mapping, OVERSIZED_MAP_SIZE);

	dmabuf_fd = ioctl(fd, VB2_MMAP_PPPS_EXPBUF);
	ksft_test_result(dmabuf_fd >= 0, "export the plane as a DMA-BUF\n");
	if (dmabuf_fd < 0)
		ksft_exit_fail_msg("export DMA-BUF failed: %s\n",
				   strerror(errno));
	ksft_test_result(fstat(dmabuf_fd, &st) == 0 &&
			 st.st_size == (off_t)NATIVE_PAGE_SIZE,
			 "DMA-BUF retains its native-page-aligned allocation (%lld)\n",
			 (long long)st.st_size);
	mapping = mmap(NULL, VALID_MAP_SIZE, PROT_READ, MAP_SHARED,
		       dmabuf_fd, 0);
	ksft_test_result(mapping != MAP_FAILED,
			 "map the DMA-BUF through its final process page\n");
	if (mapping != MAP_FAILED)
		munmap(mapping, VALID_MAP_SIZE);

	errno = 0;
	mapping = mmap(NULL, (NATIVE_PAGE_SIZE + PROCESS_PAGE_SIZE), PROT_READ, MAP_SHARED,
		       dmabuf_fd, 0);
	saved_errno = errno;
	ksft_test_result(mapping == MAP_FAILED && saved_errno == EINVAL,
			 "reject a DMA-BUF mapping beyond its allocation (errno=%d)\n",
			 saved_errno);
	if (mapping != MAP_FAILED)
		munmap(mapping, (NATIVE_PAGE_SIZE + PROCESS_PAGE_SIZE));
	close(dmabuf_fd);
	close(fd);
	ksft_finished();
}

int main(int argc, char **argv)
{
	const char *mode = ppps_run_mode(argc, argv, NULL);

	if (!mode)
		exec_compat(argv[0], "--run", NULL);
	if (argc == 2 && !strcmp(mode, "--run"))
		return run_test();
	if (argc == 2 && !strcmp(mode, "--native"))
		exec_native(argv[0], "--native-run", NULL);
	if (argc == 2 && !strcmp(mode, "--native-run"))
		return run_native_test();
	return EXIT_FAILURE;
}
