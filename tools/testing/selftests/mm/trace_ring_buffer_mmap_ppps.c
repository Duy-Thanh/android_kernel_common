// SPDX-License-Identifier: GPL-2.0
/*
 * A 4K compat process mmaps the tracefs CPU0 ring buffer: the metadata page,
 * a 4K sub-page offset handled according to the native-page layout, the whole
 * data ring in 4K process pages, and the GET_READER ioctl.
 */
#define _GNU_SOURCE

#include <linux/trace_mmap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "kselftest_ppps.h"

#define TRACE_PIPE_RAW	"/sys/kernel/tracing/per_cpu/cpu0/trace_pipe_raw"

static int run_test(void)
{
	struct trace_buffer_meta *meta;
	unsigned long data_len;
	unsigned long meta_len;
	unsigned long offset;
	unsigned char checksum = 0;
	unsigned char *data;
	void *subpage;
	bool layout_ok;
	bool subpage_ok;
	int saved_errno;
	int fd;

	ksft_print_header();
	ksft_set_plan(7);

	fd = ppps_open_fixture_or_skip(TRACE_PIPE_RAW, O_RDONLY | O_NONBLOCK);
	ksft_test_result(true, "open the CPU0 trace ring buffer\n");

	meta = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (meta == MAP_FAILED)
		ksft_exit_fail_msg("metadata mmap failed: %s\n", strerror(errno));
	ksft_test_result(true, "map the ring-buffer metadata page\n");

	meta_len = meta->meta_page_size;
	data_len = (unsigned long)meta->subbuf_size * meta->nr_subbufs;
	layout_ok = meta->meta_struct_len >= sizeof(*meta) &&
		meta->meta_struct_len <= meta_len &&
		meta_len >= PROCESS_PAGE_SIZE && !(meta_len % PROCESS_PAGE_SIZE) &&
		meta->subbuf_size && meta->nr_subbufs && data_len;
	ksft_test_result(layout_ok, "metadata describes a valid native-page ring\n");
	ksft_print_msg("meta=%lu struct=%u subbuf=%u nr_subbufs=%u data=%lu\n",
		       meta_len, meta->meta_struct_len, meta->subbuf_size,
		       meta->nr_subbufs, data_len);
	if (!layout_ok)
		ksft_exit_fail_msg("invalid trace ring-buffer metadata\n");

	errno = 0;
	subpage = mmap(NULL, PROCESS_PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
		       PROCESS_PAGE_SIZE);
	saved_errno = errno;
	subpage_ok = meta_len == PROCESS_PAGE_SIZE ? subpage != MAP_FAILED :
						  subpage == MAP_FAILED;
	ksft_test_result(subpage_ok,
			 "handle a 4K mmap offset according to the native-page layout (errno=%d)\n",
			 saved_errno);
	if (subpage != MAP_FAILED)
		munmap(subpage, PROCESS_PAGE_SIZE);

	errno = 0;
	data = mmap(NULL, data_len, PROT_READ, MAP_SHARED, fd, meta_len);
	saved_errno = errno;
	ksft_test_result(data != MAP_FAILED,
			 "map the complete ring using 4K process pages\n");
	ksft_print_msg("data mmap=%p errno=%d\n", data, saved_errno);
	if (data == MAP_FAILED)
		ksft_exit_fail_msg("full data mmap failed: %s\n",
				   strerror(saved_errno));

	for (offset = 0; offset < data_len; offset += PROCESS_PAGE_SIZE)
		checksum ^= data[offset];
	ksft_test_result(true, "access every mapped 4K slice without SIGBUS\n");
	ksft_print_msg("mapped data checksum=%u\n", checksum);

	errno = 0;
	ksft_test_result(ioctl(fd, TRACE_MMAP_IOCTL_GET_READER) == 0,
			 "swap a reader sub-buffer through the mmap ioctl\n");

	munmap(data, data_len);
	munmap(meta, PROCESS_PAGE_SIZE);
	close(fd);
	ksft_finished();
}

PPPS_COMPAT_MAIN(run_test)
