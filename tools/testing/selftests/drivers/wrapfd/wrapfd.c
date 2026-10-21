// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 * Author: Suren Baghdasaryan <surenb@google.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * WrapFD API tests
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/align.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/wrapfd.h>
#include "../../kselftest_harness.h"

#define offset_in_page(addr, page_size) ((addr) & (page_size - 1))

/* ioctl wrappers */
static inline int wrapfd_wrap(int dev_fd, int fd, int prot)
{
	struct wrapfd_wrap wrap = {
		.fd = fd,
		.prot = prot,
	};

	return ioctl(dev_fd, WRAPFD_DEV_IOC_WRAP, &wrap);
}

static inline int wrapfd_get_state(int wrapfd, unsigned int *state)
{
	struct wrapfd_get_state wrap_state = { 0 };
	int ret;

	ret = ioctl(wrapfd, WRAPFD_DEV_IOC_GET_STATE, &wrap_state);
	if (!ret && state)
		*state = wrap_state.state;

	return ret;
}

static inline int wrapfd_acquire_ownership(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP, NULL);
}

static inline int wrapfd_release_ownership(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_RELEASE_OWNERSHIP, NULL);
}

static inline int wrapfd_load(int wrapfd, int fd, unsigned long file_offs,
			      unsigned long buf_offs, unsigned long len)
{
	struct wrapfd_load load = {
		.fd = fd,
		.file_offs = file_offs,
		.buf_offs = buf_offs,
		.len = len,
	};

	return ioctl(wrapfd, WRAPFD_DEV_IOC_LOAD, &load);
}

static inline int wrapfd_rewrap(int wrapfd, int prot)
{
	struct wrapfd_rewrap rewrap = {
		.prot = prot,
	};

	return ioctl(wrapfd, WRAPFD_DEV_IOC_REWRAP, &rewrap);
}

static inline int wrapfd_empty(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_EMPTY, NULL);
}

static inline int wrapfd_allow_guests(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_ALLOW_GUESTS, NULL);
}

static inline int wrapfd_prohibit_guests(int wrapfd)
{
	return ioctl(wrapfd, WRAPFD_DEV_IOC_PROHIBIT_GUESTS, NULL);
}

static inline int wrapfd_add_monitors(int dev_fd, struct wrapfd_monitor *arr,
				      __u64 *results, __u64 len)
{
	struct wrapfd_add_monitors add_monitors = {
		.monitors = {
			.len = len,
			.arr = arr,
			.results = results,
		},
	};

	return ioctl(dev_fd, WRAPFD_DEV_IOC_ADD_MONITOR, &add_monitors);
}

static inline int wrapfd_del_monitors(int dev_fd, struct wrapfd_monitor *arr,
				      __u64 *results, __u64 len)
{
	struct wrapfd_del_monitors del_monitors = {
		.monitors = {
			.len = len,
			.arr = arr,
			.results = results,
		},
	};

	return ioctl(dev_fd, WRAPFD_DEV_IOC_DEL_MONITOR, &del_monitors);
}

static inline int wrapfd_query_events(int dev_fd, struct wrapfd_event *arr,
				      __u64 len, __u64 *count)
{
	struct wrapfd_events events = {
		.len = len,
		.arr = arr,
	};
	int ret;

	ret = ioctl(dev_fd, WRAPFD_DEV_IOC_QUERY_EVENTS, &events);
	if (!ret && count)
		*count = events.count;

	return ret;
}

/* test utility functions */
static int dmabuf_heap_alloc(int heap_fd, size_t len, int fd_flags)
{
	struct dma_heap_allocation_data data = {
		.len = len,
		.fd = 0,
		.fd_flags = fd_flags | O_CLOEXEC,
		.heap_flags = 0,
	};
	int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data);

	return ret < 0 ? ret : data.fd;
}

static int dmabuf_sync(int dmabuf_fd, bool start, int access_type)
{
	struct dma_buf_sync sync = {
		.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | access_type,
	};

	return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

static int dmabuf_sync_start(int dmabuf_fd, int access_type)
{
	return dmabuf_sync(dmabuf_fd, true, access_type);
}

static int dmabuf_sync_end(int dmabuf_fd, int access_type)
{
	return dmabuf_sync(dmabuf_fd, false, access_type);
}

static void generate_file_content(char *content, size_t size)
{
	srand(time(NULL));
	/* Generate only printable characters in the range of [32, 126] */
	for (size_t i = 0; i < size; i++)
		content[i] = 32 + rand() % 95;
}

FIXTURE(wrapfd_tests)
{
	size_t page_size;
	char *content;
	size_t size;
	int dev_fd;
	int fd;
};

static size_t get_file_size(size_t page_size)
{
	size_t min_req_size = 100 * page_size - page_size / 2;
	FILE *min_req_size_fp;

	min_req_size_fp = fopen("/sys/module/wrapfd/parameters/min_bytes_per_req", "r");
	if (!min_req_size_fp)
		return min_req_size;

	if (fscanf(min_req_size_fp, "%zu\n", &min_req_size) != 1)
		goto out;

	/*
	 * Intentionally make the file size large enough so that the driver divides loads into
	 * multiple requests, and make the file size page unaligned if necessary.
	 */
	min_req_size *= 3;
	if (IS_ALIGNED(min_req_size, page_size))
		min_req_size -= page_size / 2;

out:
	fclose(min_req_size_fp);
	return min_req_size;
}

FIXTURE_SETUP(wrapfd_tests)
{
	ssize_t total_wr;
	FILE *ftmp;

	EXPECT_EQ(getuid(), 0)
		SKIP(return, "Skipping all tests as non-root");

	EXPECT_EQ(access("/dev/wrapfd", F_OK), 0)
		SKIP(return, "Skipping all tests; kernel lacks wrapfd support");

	self->page_size = (size_t)sysconf(_SC_PAGESIZE);
	self->size = get_file_size(self->page_size);

	self->dev_fd = open("/dev/wrapfd", O_RDONLY);
	ASSERT_TRUE(self->dev_fd >= 0);

	/* Prepare random content buffer */
	self->content = malloc(self->size);
	generate_file_content(self->content, self->size);

	/* Prepare temporary file with the same content */
	ftmp = tmpfile();
	ASSERT_NE(ftmp, NULL);
	self->fd = dup(fileno(ftmp));
	fclose(ftmp);

	total_wr = 0;
	do {
		ssize_t wr = write(self->fd, self->content + total_wr,
				   self->size - total_wr);
		ASSERT_TRUE(wr >= 0);
		total_wr += wr;
	} while (total_wr < self->size);
}

FIXTURE_TEARDOWN(wrapfd_tests)
{
	close(self->fd);
	free(self->content);
	close(self->dev_fd);
}

static int cmp_content(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int wrapfd, unsigned long file_offs,
		       unsigned long buf_offs, unsigned long len)
{
	/* mmap() operates on page aligned offsets and lengths, so handle that here. */
	unsigned long aligned_buf_offs = ALIGN_DOWN(buf_offs, self->page_size);
	unsigned long buf_pg_offs = offset_in_page(buf_offs, self->page_size);
	unsigned long aligned_len = ALIGN(buf_offs + len, self->page_size) - aligned_buf_offs;
	char *ptr;
	int ret;

	ptr = mmap(NULL, aligned_len, PROT_READ, MAP_SHARED, wrapfd, aligned_buf_offs);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
	ret = memcmp(self->content + file_offs, ptr + buf_pg_offs, len);
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
	ASSERT_EQ(munmap(ptr, aligned_len), 0);

	return ret;
}

static void clear_content(struct __test_metadata *_metadata,
			  FIXTURE_DATA(wrapfd_tests) *self, int wrapfd)
{
	char *ptr;

	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_WRITE), 0);
	memset(ptr, 0, self->size);
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_WRITE), 0);
	ASSERT_EQ(munmap(ptr, self->size), 0);
}

static bool is_content_clear(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int wrapfd,
		       unsigned long buf_offs, unsigned long len)
{
	/* mmap() operates on page aligned offsets and lengths, so handle that here. */
	unsigned long aligned_buf_offs = ALIGN_DOWN(buf_offs, self->page_size);
	unsigned long buf_pg_offs = offset_in_page(buf_offs, self->page_size);
	unsigned long aligned_len = ALIGN(buf_offs + len, self->page_size) - aligned_buf_offs;
	unsigned long i = 0;
	char *ptr;
	int ret;

	ptr = mmap(NULL, aligned_len, PROT_READ, MAP_SHARED, wrapfd, aligned_buf_offs);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
	while (i < len && ptr[buf_pg_offs + i] == 0)
		i++;
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
	ASSERT_EQ(munmap(ptr, aligned_len), 0);

	return i == len;
}

static void test_wrap(struct __test_metadata *_metadata,
		      FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	struct stat sb;

	/* Get state of a non-wrapped fd */
	ASSERT_TRUE(wrapfd_get_state(fd, NULL) &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_get_state(self->dev_fd, NULL) &&
		    errno == ENOTTY);

	/* Wrap and get state of a wrapped fd */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_get_state(wrapfd, NULL), 0);

	/* Ensure that the size of the wrapfd matches the size of the underlying buffer. */
	ASSERT_EQ(fstat(wrapfd, &sb), 0);
	ASSERT_EQ(sb.st_size, ALIGN(self->size, self->page_size));

	close(wrapfd);
}

static void test_wrap_rdonly_file(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	unsigned int state;

	/* Wrapping a read-only file in a read/write wrap should fail. */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_NE(wrapfd, 0);

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_get_state(wrapfd, &state), 0);
	ASSERT_EQ(state, WRAPFD_CONTENT_RDONLY);

	close(wrapfd);
}

static const void *memchr_inv(const void *start, int c, size_t bytes)
{
	const char *src = start;
	char val = c;
	int i;

	for (i = 0; i < bytes; i++)
		if (src[i] != val)
			return &src[i];

	return NULL;
}

static int poison_region(int wrapfd, void *start, int poison, size_t bytes)
{
	int ret = dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_WRITE);

	if (ret)
		return ret;

	memset(start, poison, bytes);

	return dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_WRITE);
}

static int verify_region_poison(int wrapfd, void *start, int poison, size_t bytes)
{
	int ret = dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ);
	bool poisoned;

	if (ret)
		return ret;

	poisoned = memchr_inv(start, poison, bytes) == NULL;

	ret = dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ);
	if (ret)
		return ret;

	return poisoned ? 0 : -1;
}

static int load_and_cmp(struct __test_metadata *_metadata, FIXTURE_DATA(wrapfd_tests) *self,
			int wrapfd, unsigned long file_offs, unsigned long buf_offs,
			unsigned long len)
{
	const char poison = 0xaa;
	int ret;
	char *head_ptr = NULL;
	unsigned long tail_len, tail_aligned_offset, tail_map_len;
	char *tail_page_ptr = NULL;
	char *tail_ptr = NULL;

	clear_content(_metadata, self, wrapfd);

	ret = cmp_content(_metadata, self, wrapfd, 0, 0, self->size);
	EXPECT_NE(ret, 0)
		return ret;

	if (buf_offs) {
		head_ptr = mmap(NULL, buf_offs, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
		EXPECT_NE(head_ptr, MAP_FAILED)
			return -1;

		ret = poison_region(wrapfd, head_ptr, poison, buf_offs);
		EXPECT_EQ(ret, 0)
			goto out_unmap_head;
	}

	if ((buf_offs + len) < self->size) {
		tail_len = self->size - buf_offs - len;
		tail_aligned_offset = ALIGN_DOWN(buf_offs + len, self->page_size);
		tail_map_len = self->size - tail_aligned_offset;
		tail_page_ptr = mmap(NULL, tail_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd,
				     tail_aligned_offset);
		EXPECT_NE(tail_page_ptr, MAP_FAILED) {
			ret = -1;
			goto out_unmap_head;
		}

		tail_ptr = tail_page_ptr + offset_in_page(buf_offs + len, self->page_size);
		ret = poison_region(wrapfd, tail_ptr, poison, tail_len);
		EXPECT_EQ(ret, 0)
			goto out_unmap_tail;
	}

	ret = wrapfd_load(wrapfd, self->fd, file_offs, buf_offs, len);
	EXPECT_EQ(ret, 0)
		goto out_unmap_tail;

	if (head_ptr) {
		ret = verify_region_poison(wrapfd, head_ptr, poison, buf_offs);
		EXPECT_EQ(ret, 0)
			goto out_unmap_tail;
	}

	ret = cmp_content(_metadata, self, wrapfd, file_offs, buf_offs, len);
	EXPECT_EQ(ret, 0)
		goto out_unmap_tail;

	if (tail_ptr) {
		ret = verify_region_poison(wrapfd, tail_ptr, poison, tail_len);
		EXPECT_EQ(ret, 0);
	}

out_unmap_tail:
	if (tail_page_ptr)
		EXPECT_EQ(munmap(tail_page_ptr, tail_map_len), 0);
out_unmap_head:
	if (head_ptr)
		EXPECT_EQ(munmap(head_ptr, buf_offs), 0);
	return ret;
}

static int __test_loads(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int wrapfd)
{
	int ret;

	/* Test a sub-page load and ensure only the requested part was written to. */
	ret = load_and_cmp(_metadata, self, wrapfd, 0, 0, self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, 0, self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Test tiny read at random offsets. */
	ret = load_and_cmp(_metadata, self, wrapfd, 100, 0, 50);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Cross a page boundary in the file and then in the buffer. */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size - 1, 0, self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, 0, self->page_size - 1, self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	/*
	 * Start bounce buffer, large O_DIRECT read and memmove() logic along with end bounce
	 * buffer.
	 */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, self->page_size / 4,
			   self->page_size * 3);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Test logic for fitting everything within the first bounce page. */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, self->page_size / 2,
			   self->page_size / 2);
	EXPECT_EQ(ret, 0)
		return ret;

	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size, self->page_size,
			   self->size - self->page_size);
	EXPECT_EQ(ret, 0)
		return ret;

	/*
	 * Test logic to use a bounce buffer at the start and end with a large enough request
	 * that gets split into multiple sub-requests.
	 */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size / 2, self->page_size / 2,
			   self->size - (self->page_size / 2));
	EXPECT_EQ(ret, 0)
		return ret;

	/*
	 * Test logic to use a bounce buffer at the start and end with 2 bounce buffers at the end.
	 *
	 * 2 bounce buffers get allocated because after accounting for the start buffer, the
	 * remaining length to be copied at the proper offset crosses a page boundary.
	 */
	ret = load_and_cmp(_metadata, self, wrapfd, self->page_size - 1, 0, self->page_size + 4);
	EXPECT_EQ(ret, 0)
		return ret;

	/* Save this one for last since subsequent tests run comparisons on the entire buffer. */
	ret = load_and_cmp(_metadata, self, wrapfd, 0, 0, self->size);
	EXPECT_EQ(ret, 0);

	return ret;
}

static void test_load_mapped(struct __test_metadata *_metadata,
			     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Loading should still work while a buffer is mapped. */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	ASSERT_EQ(__test_loads(_metadata, self, wrapfd), 0);

	ASSERT_EQ(munmap(ptr, self->size), 0);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_load_unmapped(struct __test_metadata *_metadata,
			       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	ASSERT_EQ(__test_loads(_metadata, self, wrapfd), 0);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_load_rdonly_file(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == EACCES);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_wrap_rdonly(struct __test_metadata *_metadata,
			     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Try mapping as writable */
	ASSERT_EQ(mmap(NULL, self->size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, wrapfd, 0), MAP_FAILED);
	ASSERT_EQ(errno, EACCES);

	close(wrapfd);
}

static void test_wrap_rdwr(struct __test_metadata *_metadata,
			   FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_RW), 0);
	ptr[0]++;

	/* Check content of the buffer after modification */
	ASSERT_NE(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Restore buffer content */
	ptr[0]--;
	ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_RW), 0);


	/* Confirm the final content */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

/* Bionic's implementation of libc does not have a wrapper for remap_file_pages(). */
#ifdef __ANDROID__
static int remap_file_pages(void *addr, size_t size, int prot, size_t pgoff, int flags)
{
	return syscall(__NR_remap_file_pages, addr, size, prot, pgoff, flags);
}
#endif

static void test_remap_file_pages(struct __test_metadata *_metadata,
				  FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	/* remap_file_pages() on the content should succeed */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   fd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(remap_file_pages(ptr, self->page_size, 0, 1, 0), 0);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* remap_file_pages() on the wrapfd should fail with EINVAL error */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(remap_file_pages(ptr, self->page_size, 0, 1, 0), -1);
	ASSERT_EQ(errno, EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

static void test_wrap_remap(struct __test_metadata *_metadata,
			    FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	char *ptr, *new_ptr;
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Remap to a new address */
	new_ptr = mremap(ptr, self->size, self->size, MREMAP_MAYMOVE);
	ASSERT_NE(new_ptr, MAP_FAILED);
	ASSERT_EQ(memcmp(self->content, new_ptr, self->size), 0);
	ptr = new_ptr;

	/* Resize the mapping */
	new_ptr = mremap(ptr, self->size, self->size / 2, MREMAP_MAYMOVE);
	ASSERT_NE(new_ptr, MAP_FAILED);
	ASSERT_EQ(memcmp(self->content, new_ptr, self->size / 2), 0);
	ptr = new_ptr;
	ASSERT_EQ(munmap(ptr, self->size / 2), 0);

	close(wrapfd);
}

static void test_wrap_fork(struct __test_metadata *_metadata,
			   FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, status;
	char *ptr;
	pid_t pid;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Check content of the buffer before modification */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);

	/* Modify buffer content */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	pid = fork();
	ASSERT_FALSE(pid < 0);
	if (pid == 0) {
		/* Check the content from the child */
		ASSERT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
		ASSERT_EQ(memcmp(self->content, ptr, self->size), 0);
		ASSERT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
		exit(EXIT_SUCCESS);
	} else {
		ASSERT_NE(wait(&status), -1);
		ASSERT_TRUE(WIFEXITED(status));
		ASSERT_EQ(WEXITSTATUS(status), 0);
	}

	ASSERT_EQ(munmap(ptr, self->size), 0);

	close(wrapfd);
}

static void test_dup(struct __test_metadata *_metadata,
		     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, wrapfd2;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	wrapfd2 = dup(wrapfd);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Check content of the buffer using both fds */
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd, 0, 0, self->size), 0);
	ASSERT_EQ(cmp_content(_metadata, self, wrapfd2, 0, 0, self->size), 0);

	close(wrapfd2);
	close(wrapfd);
}

static void test_owner(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try taking ownership of a mapped wrapfd */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_acquire_ownership(wrapfd) && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Take ownership of an unmapped wrapfd */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Map owned wrapfd */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);

	/* Try releasing ownership while still mapped */
	ASSERT_TRUE(wrapfd_release_ownership(wrapfd) && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Release ownership of an unmapped wrapfd */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);
	close(wrapfd);
}

static void test_rewrap(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd, wrapfd2, wrapfd3;
	unsigned int state;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDONLY);

	/* Try rewrapping unowned buffer */
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try rewrapping a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED | MAP_POPULATE,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Rewrap the buffer to writable */
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Check the states of the new and original buffers */
	ASSERT_TRUE(wrapfd_get_state(wrapfd2, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDWR);
	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/*
	 * Check rewrapped content which should be cleared
	 * by the wrapfd driver
	 */
	ASSERT_EQ(is_content_clear(_metadata, self, wrapfd2,
				   0, self->size), true);

	/* Take ownership of the rewrapped buffer */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd2), 0);

	/* Convert back to read-only */
	wrapfd3 = wrapfd_rewrap(wrapfd2, PROT_READ);
	ASSERT_TRUE(wrapfd3 >= 0);
	ASSERT_TRUE(wrapfd_get_state(wrapfd3, &state) == 0 &&
		    state == WRAPFD_CONTENT_RDONLY);
	ASSERT_TRUE(wrapfd_get_state(wrapfd2, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/* Try mapping as writable */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd3, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == EACCES);

	/*
	 * Check rewrapped content which should be cleared
	 * by the wrapfd driver
	 */
	ASSERT_EQ(is_content_clear(_metadata, self, wrapfd3,
				   0, self->size), true);

	/* Try mapping the original empty wrap file */
	ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == ENOENT);

	/* Release ownership of the buffers */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd2), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd3);
	close(wrapfd2);
	close(wrapfd);
}

static void test_rewrap_rdonly_file(struct __test_metadata *_metadata,
				    FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	unsigned int state;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	ASSERT_EQ(wrapfd_get_state(wrapfd, &state), 0);
	ASSERT_EQ(state, WRAPFD_CONTENT_RDONLY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Rewrapping the buffer as writable should fail. */
	ASSERT_TRUE(wrapfd_rewrap(wrapfd, PROT_WRITE) < 0 && errno == EACCES);

	/* Mapping the buffer as writable should also fail. */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == EACCES);

	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

static void test_empty(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	unsigned int state;
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try emptying unowned buffer */
	ASSERT_TRUE(wrapfd_empty(wrapfd) < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try emptying a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_empty(wrapfd) < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Empty the buffer */
	ASSERT_EQ(wrapfd_empty(wrapfd), 0);
	ASSERT_TRUE(wrapfd_get_state(wrapfd, &state) == 0 &&
		    state == WRAPFD_CONTENT_EMPTY);

	/* Try mapping the empty wrap file */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == ENOENT);

	/* Try loading into an empty wrap file. */
	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == ENOENT);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

static int is_close_on_exec(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return -1;

	return (flags & FD_CLOEXEC) ? 1 : 0;
}

static int set_close_on_exec(int fd, bool set) {
	int flags;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		return -1;

	if (set)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	return fcntl(fd, F_SETFD, flags);
}

static void __test_close_on_exec(struct __test_metadata *_metadata,
				 FIXTURE_DATA(wrapfd_tests) *self,
				 int fd, int close_on_exec)
{
	int wrapfd, wrapfd2;

	/* Wrap's attribute should match its content */
	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(close_on_exec, is_close_on_exec(wrapfd));

	/* Rewrapping should preserve the attribute */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);
	wrapfd2 = wrapfd_rewrap(wrapfd, PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);
	ASSERT_EQ(close_on_exec, is_close_on_exec(wrapfd2));

	close(wrapfd2);
	close(wrapfd);
}

static void test_close_on_exec(struct __test_metadata *_metadata,
			       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int close_on_exec;

	close_on_exec = is_close_on_exec(fd);
	ASSERT_NE(close_on_exec, -1);

	/* Test FD_CLOEXEC inheritance */
	__test_close_on_exec(_metadata, self, fd, close_on_exec);

	/* Test FD_CLOEXEC inheritance after toggling the attribute */
	set_close_on_exec(fd, !close_on_exec);
	__test_close_on_exec(_metadata, self, fd, !close_on_exec);

	/* Reset attribute to the original value */
	set_close_on_exec(fd, close_on_exec);
}

static void test_guests(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try allowing guests for unowned buffer */
	ASSERT_TRUE(wrapfd_allow_guests(wrapfd) < 0 && errno == EBUSY);

	/* Take buffer ownership */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);

	/* Try allowing guests for a mapped buffer */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_TRUE(wrapfd_allow_guests(wrapfd) < 0 && errno == EINVAL);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Allow guests */
	ASSERT_EQ(wrapfd_allow_guests(wrapfd), 0);
	ASSERT_EQ(wrapfd_prohibit_guests(wrapfd), 0);
	ASSERT_EQ(wrapfd_allow_guests(wrapfd), 0);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	close(wrapfd);
}

#define FDINFO_BUF_SIZE	4096

static void test_ioctl(struct __test_metadata *_metadata,
			FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	const char *buf_name = "test_dmabuf";
	char str[FDINFO_BUF_SIZE];
	char fdinfo_name[64];
	int fdinfo_fd;
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);
	ASSERT_EQ(ioctl(wrapfd, DMA_BUF_SET_NAME, buf_name), 0);

	/* Verify the dmabuf name is changed */
	sprintf(fdinfo_name, "/proc/%d/fdinfo/%d", getpid(), fd);
	fdinfo_fd = open(fdinfo_name, O_RDONLY);
	ASSERT_TRUE(fdinfo_fd >= 0);
	ASSERT_TRUE(read(fdinfo_fd, str, FDINFO_BUF_SIZE) > 0);
	ASSERT_NE(strstr(str, buf_name), NULL);
	close(fdinfo_fd);

	/* Verify the dmabuf name is changed in wrapfd fdinfo too */
	sprintf(fdinfo_name, "/proc/%d/fdinfo/%d", getpid(), wrapfd);
	fdinfo_fd = open(fdinfo_name, O_RDONLY);
	ASSERT_TRUE(fdinfo_fd >= 0);
	ASSERT_TRUE(read(fdinfo_fd, str, FDINFO_BUF_SIZE) > 0);
	ASSERT_NE(strstr(str, buf_name), NULL);
	close(fdinfo_fd);

	close(wrapfd);
}

static void test_lseek(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	int wrapfd;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd >= 0);

	/* Verify the dmabuf size is what we expect; dmabuf sizes are page aligned. */
	ASSERT_EQ(lseek(wrapfd, 0, SEEK_END), ALIGN(self->size, self->page_size));
	ASSERT_EQ(lseek(wrapfd, 0, SEEK_SET), 0);

	close(wrapfd);
}

#define POLL_TIMEOUT_MS	5000

enum test_state {
	POLL_STATE_INIT,
	POLL_STATE_READY,
	POLL_STATE_START,
	POLL_STATE_DONE,
	POLL_STATE_RESET,
};

struct poll_thread_ctl {
	pthread_mutex_t sync_lock;
	pthread_cond_t sync_cond;
	enum test_state curr_state;
	int dev_fd;
	int poll_ret;
	short revents;
	bool exit;
};

/* Thread synchronization routines */
static void poll_thread_wait_for_state(struct poll_thread_ctl *poll_ctl, enum test_state state)
{
	pthread_mutex_lock(&poll_ctl->sync_lock);
	while (poll_ctl->curr_state != state)
		pthread_cond_wait(&poll_ctl->sync_cond, &poll_ctl->sync_lock);
	pthread_mutex_unlock(&poll_ctl->sync_lock);
}

static void poll_thread_signal_state(struct poll_thread_ctl *poll_ctl, enum test_state state)
{
	pthread_mutex_lock(&poll_ctl->sync_lock);
	poll_ctl->curr_state = state;
	pthread_cond_signal(&poll_ctl->sync_cond);
	pthread_mutex_unlock(&poll_ctl->sync_lock);
}

static void poll_thread_stop(struct poll_thread_ctl *poll_ctl)
{
	poll_thread_wait_for_state(poll_ctl, POLL_STATE_READY);
	poll_ctl->exit = true;
	poll_thread_signal_state(poll_ctl, POLL_STATE_START);
}

static void *poll_thread_fn(void *arg)
{
	struct poll_thread_ctl *poll_ctl = arg;
	struct pollfd pollfd = {
		.fd = poll_ctl->dev_fd,
		.events = POLLIN,
	};

	while (true) {
		poll_thread_signal_state(poll_ctl, POLL_STATE_READY);
		poll_thread_wait_for_state(poll_ctl, POLL_STATE_START);
		if (poll_ctl->exit)
			break;

		pollfd.revents = 0;
		poll_ctl->poll_ret = poll(&pollfd, 1, POLL_TIMEOUT_MS);
		poll_ctl->revents = pollfd.revents;

		poll_thread_signal_state(poll_ctl, POLL_STATE_DONE);
		poll_thread_wait_for_state(poll_ctl, POLL_STATE_RESET);
	}

	return NULL;
}

static void poll_thread_start_poll(struct poll_thread_ctl *poll_ctl)
{
	poll_thread_wait_for_state(poll_ctl, POLL_STATE_READY);
	poll_thread_signal_state(poll_ctl, POLL_STATE_START);
}

static void poll_thread_finish_poll(struct __test_metadata *_metadata,
				    struct poll_thread_ctl *poll_ctl)
{
	poll_thread_wait_for_state(poll_ctl, POLL_STATE_DONE);
	ASSERT_EQ(poll_ctl->poll_ret, 1);
	ASSERT_TRUE(poll_ctl->revents & POLLIN);
	poll_thread_signal_state(poll_ctl, POLL_STATE_RESET);
}

static void verify_single_event(struct __test_metadata *_metadata,
				FIXTURE_DATA(wrapfd_tests) *self,
				struct poll_thread_ctl *poll_ctl,
				__u32 expected_fd, __u32 expected_events,
				__u64 *last_ts)
{
	struct wrapfd_event events[2] = {};
	__u64 count = 0;

	poll_thread_finish_poll(_metadata, poll_ctl);

	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 2, &count), 0);
	ASSERT_EQ(count, 1);
	ASSERT_EQ(events[0].fd, expected_fd);
	ASSERT_EQ(events[0].events, expected_events);
	ASSERT_TRUE(events[0].timestamp >= *last_ts);
	*last_ts = events[0].timestamp;

	/* Subsequent query without new events should return 0 events */
	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 2, &count), 0);
	ASSERT_EQ(count, 0);
}

static void test_poll_ioctls(struct __test_metadata *_metadata,
			     FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	struct wrapfd_add_monitors add_monitors = {};
	struct wrapfd_del_monitors del_monitors = {};
	struct wrapfd_monitor invalid_monitors[6];
	struct wrapfd_monitor monitors[2];
	struct wrapfd_event events[2] = {};
	__u64 invalid_results[6] = {};
	__u64 results[2] = {};
	int wrapfd1, wrapfd2;
	struct pollfd pollfd;
	int wrapfd1_dup;
	__u64 count = 0;
	char *ptr;

	wrapfd1 = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd1 >= 0);
	wrapfd2 = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Test ADD_MONITOR, DEL_MONITOR, and QUERY_EVENTS ioctls with various parameters */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENT_MAP;

	/* Monitor/event ioctls on non-device fds should fail with ENOTTY */
	ASSERT_TRUE(wrapfd_add_monitors(fd, monitors, results, 1) < 0 &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_add_monitors(wrapfd1, monitors, results, 1) < 0 &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_del_monitors(fd, monitors, results, 1) < 0 &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_del_monitors(wrapfd1, monitors, results, 1) < 0 &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_query_events(fd, events, 1, &count) < 0 &&
		    errno == ENOTTY);
	ASSERT_TRUE(wrapfd_query_events(wrapfd1, events, 1, &count) < 0 &&
		    errno == ENOTTY);

	/* Invalid parameters for WRAPFD_DEV_IOC_ADD_MONITOR */
	ASSERT_TRUE(ioctl(self->dev_fd, WRAPFD_DEV_IOC_ADD_MONITOR, NULL) < 0 &&
		    errno == EFAULT);
	add_monitors.monitors.len = 1;
	add_monitors.monitors.arr = monitors;
	add_monitors.monitors.results = results;
	add_monitors.reserved = 1;
	ASSERT_TRUE(ioctl(self->dev_fd, WRAPFD_DEV_IOC_ADD_MONITOR, &add_monitors) < 0 &&
		    errno == EINVAL);
	ASSERT_TRUE(wrapfd_add_monitors(self->dev_fd, monitors, results, 0) < 0 &&
		    errno == EINVAL);
	ASSERT_TRUE(wrapfd_add_monitors(self->dev_fd, NULL, results, 1) < 0 &&
		    errno == EFAULT);
	ASSERT_TRUE(wrapfd_add_monitors(self->dev_fd, monitors, NULL, 1) < 0 &&
		    errno == EFAULT);

	/* Invalid parameters for WRAPFD_DEV_IOC_DEL_MONITOR */
	ASSERT_TRUE(ioctl(self->dev_fd, WRAPFD_DEV_IOC_DEL_MONITOR, NULL) < 0 &&
		    errno == EFAULT);
	del_monitors.monitors.len = 1;
	del_monitors.monitors.arr = monitors;
	del_monitors.monitors.results = results;
	del_monitors.reserved = 1;
	ASSERT_TRUE(ioctl(self->dev_fd, WRAPFD_DEV_IOC_DEL_MONITOR, &del_monitors) < 0 &&
		    errno == EINVAL);
	ASSERT_TRUE(wrapfd_del_monitors(self->dev_fd, monitors, results, 0) < 0 &&
		    errno == EINVAL);
	ASSERT_TRUE(wrapfd_del_monitors(self->dev_fd, NULL, results, 1) < 0 &&
		    errno == EFAULT);
	ASSERT_TRUE(wrapfd_del_monitors(self->dev_fd, monitors, NULL, 1) < 0 &&
		    errno == EFAULT);

	/* Invalid parameters for WRAPFD_DEV_IOC_QUERY_EVENTS */
	ASSERT_TRUE(ioctl(self->dev_fd, WRAPFD_DEV_IOC_QUERY_EVENTS, NULL) < 0 &&
		    errno == EFAULT);
	ASSERT_TRUE(wrapfd_query_events(self->dev_fd, events, 0, &count) < 0 &&
		    errno == EINVAL);

	/* Per-monitor parameter validation for ADD_MONITOR and DEL_MONITOR */
	invalid_monitors[0].fd = -1;
	invalid_monitors[0].events = WRAPFD_EVENT_MAP;
	invalid_monitors[1].fd = fd;
	invalid_monitors[1].events = WRAPFD_EVENT_MAP;
	invalid_monitors[2].fd = self->dev_fd;
	invalid_monitors[2].events = WRAPFD_EVENT_MAP;
	invalid_monitors[3].fd = wrapfd1;
	invalid_monitors[3].events = 0;
	invalid_monitors[4].fd = wrapfd1;
	invalid_monitors[4].events = ~WRAPFD_EVENTS_MASK;
	invalid_monitors[5].fd = wrapfd1;
	invalid_monitors[5].events = WRAPFD_EVENTS_MASK | (WRAPFD_EVENT_LOAD_DONE << 1);

	ASSERT_EQ(wrapfd_add_monitors(self->dev_fd, invalid_monitors, invalid_results, 6), 0);
	ASSERT_EQ((__s64)invalid_results[0], -EBADF);
	ASSERT_EQ((__s64)invalid_results[1], -EBADF);
	ASSERT_EQ((__s64)invalid_results[2], -EBADF);
	ASSERT_EQ((__s64)invalid_results[3], -EINVAL);
	ASSERT_EQ((__s64)invalid_results[4], -EINVAL);
	ASSERT_EQ((__s64)invalid_results[5], -EINVAL);

	memset(invalid_results, 0, sizeof(invalid_results));
	ASSERT_EQ(wrapfd_del_monitors(self->dev_fd, invalid_monitors, invalid_results, 6), 0);
	ASSERT_EQ((__s64)invalid_results[0], -EBADF);
	ASSERT_EQ((__s64)invalid_results[1], -EBADF);
	ASSERT_EQ((__s64)invalid_results[2], -EBADF);
	ASSERT_EQ((__s64)invalid_results[3], -EINVAL);
	ASSERT_EQ((__s64)invalid_results[4], -EINVAL);
	ASSERT_EQ((__s64)invalid_results[5], -EINVAL);

	/* Add monitor on wrapfd1, then test duplicate fd rejection (-EBUSY) */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP;
	results[0] = 0;
	ASSERT_EQ(wrapfd_add_monitors(self->dev_fd, monitors, results, 1), 0);
	ASSERT_EQ(results[0], 0);

	wrapfd1_dup = dup(wrapfd1);
	ASSERT_TRUE(wrapfd1_dup >= 0);
	monitors[0].fd = wrapfd1_dup;
	monitors[0].events = WRAPFD_EVENT_MAP;
	results[0] = 0;
	ASSERT_EQ(wrapfd_add_monitors(self->dev_fd, monitors, results, 1), 0);
	ASSERT_EQ((__s64)results[0], -EBUSY);
	close(wrapfd1_dup);

	/* Expand monitored events on wrapfd1 and add monitor on wrapfd2 */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN;
	monitors[1].fd = wrapfd2;
	monitors[1].events = WRAPFD_EVENTS_MASK;
	memset(results, 0, sizeof(results));
	ASSERT_EQ(wrapfd_add_monitors(self->dev_fd, monitors, results, 2), 0);
	ASSERT_EQ(results[0], 0);
	ASSERT_EQ(results[1], 0);

	/* Querying when no events have triggered should return count == 0 */
	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 2, &count), 0);
	ASSERT_EQ(count, 0);

	/* Trigger events on both wrapfd1 and wrapfd2 to test QUERY_EVENTS parameters */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd1), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd1), 0);
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd2, 0);
	ASSERT_NE(ptr, MAP_FAILED);
	ASSERT_EQ(munmap(ptr, self->size), 0);

	/* Consume the pending poll wakeup */
	pollfd.fd = self->dev_fd;
	pollfd.events = POLLIN;
	pollfd.revents = 0;
	ASSERT_EQ(poll(&pollfd, 1, 0), 1);
	ASSERT_TRUE(pollfd.revents & POLLIN);

	/* Querying with len == 1 when 2 wrapfds have events should return 1 event at a time */
	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 1, &count), 0);
	ASSERT_EQ(count, 1);
	ASSERT_TRUE(events[0].fd == wrapfd1 || events[0].fd == wrapfd2);

	/* Querying remaining pending event with NULL array should fail with EINVAL */
	ASSERT_TRUE(wrapfd_query_events(self->dev_fd, NULL, 1, &count) < 0 &&
		    errno == EINVAL);

	/* Querying remaining pending event with 0 len should fail with EINVAL */
	ASSERT_TRUE(wrapfd_query_events(self->dev_fd, events, 0, &count) < 0 &&
		    errno == EINVAL);

	/* Trigger again if the failed query cleared the second event, or query the remaining one */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd1), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd1), 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd2), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd2), 0);
	poll(&pollfd, 1, 0);

	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 1, &count), 0);
	ASSERT_EQ(count, 1);
	ASSERT_EQ(wrapfd_query_events(self->dev_fd, &events[1], 1, &count), 0);
	ASSERT_EQ(count, 1);
	ASSERT_NE(events[0].fd, events[1].fd);
	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 2, &count), 0);
	ASSERT_EQ(count, 0);

	/* Delete all monitors from wrapfd1 and wrapfd2 */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENTS_MASK;
	monitors[1].fd = wrapfd2;
	monitors[1].events = WRAPFD_EVENTS_MASK;
	memset(results, 0, sizeof(results));
	ASSERT_EQ(wrapfd_del_monitors(self->dev_fd, monitors, results, 2), 0);
	ASSERT_EQ(results[0], 0);
	ASSERT_EQ(results[1], 0);

	close(wrapfd2);
	close(wrapfd1);
}

static void test_poll(struct __test_metadata *_metadata,
		       FIXTURE_DATA(wrapfd_tests) *self, int fd)
{
	struct poll_thread_ctl poll_ctl = {};
	struct wrapfd_monitor monitors[2];
	struct wrapfd_event events[2] = {};
	pthread_mutexattr_t mutex_attr;
	pthread_condattr_t cond_attr;
	int wrapfd1, wrapfd2;
	pthread_t poll_thread;
	__u64 results[2] = {};
	__u64 last_ts = 0;
	__u64 count = 0;
	char *ptr1, *ptr2;

	wrapfd1 = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd1 >= 0);
	wrapfd2 = wrapfd_wrap(self->dev_fd, fd, PROT_READ | PROT_WRITE);
	ASSERT_TRUE(wrapfd2 >= 0);

	/* Monitor both wrapfd1 and wrapfd2 for MAP, UNMAP, OWN, and UNOWN events */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP |
			     WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN;
	monitors[1].fd = wrapfd2;
	monitors[1].events = WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP |
			     WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN;
	ASSERT_EQ(wrapfd_add_monitors(self->dev_fd, monitors, results, 2), 0);
	ASSERT_EQ(results[0], 0);
	ASSERT_EQ(results[1], 0);

	/* Initialize poll thread synchronization primitives and start poll thread */
	pthread_mutexattr_init(&mutex_attr);
	ASSERT_EQ(pthread_mutex_init(&poll_ctl.sync_lock, &mutex_attr), 0);
	pthread_condattr_init(&cond_attr);
	ASSERT_EQ(pthread_cond_init(&poll_ctl.sync_cond, &cond_attr), 0);
	poll_ctl.curr_state = POLL_STATE_INIT;
	poll_ctl.dev_fd = self->dev_fd;
	poll_ctl.exit = false;

	ASSERT_EQ(pthread_create(&poll_thread, NULL, poll_thread_fn, &poll_ctl), 0);

	/* Test WRAPFD_EVENT_MAP on wrapfd1 */
	poll_thread_start_poll(&poll_ctl);
	ptr1 = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd1, 0);
	ASSERT_NE(ptr1, MAP_FAILED);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd1, WRAPFD_EVENT_MAP, &last_ts);

	/* Test WRAPFD_EVENT_MAP on wrapfd2 */
	poll_thread_start_poll(&poll_ctl);
	ptr2 = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd2, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd2, WRAPFD_EVENT_MAP, &last_ts);

	/* Test WRAPFD_EVENT_UNMAP on wrapfd1 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(munmap(ptr1, self->size), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd1, WRAPFD_EVENT_UNMAP, &last_ts);

	/* Test WRAPFD_EVENT_UNMAP on wrapfd2 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(munmap(ptr2, self->size), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd2, WRAPFD_EVENT_UNMAP, &last_ts);

	/* Test WRAPFD_EVENT_OWN on wrapfd1 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd1), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd1, WRAPFD_EVENT_OWN, &last_ts);

	/* Test WRAPFD_EVENT_OWN on wrapfd2 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd2), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd2, WRAPFD_EVENT_OWN, &last_ts);

	/* Test WRAPFD_EVENT_UNOWN on wrapfd1 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd1), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd1, WRAPFD_EVENT_UNOWN, &last_ts);

	/* Test WRAPFD_EVENT_UNOWN on wrapfd2 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd2), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd2, WRAPFD_EVENT_UNOWN, &last_ts);

	/*
	 * Test polling while generating multiple events (OWN, MAP, UNMAP, UNOWN)
	 * across both wrapfd1 and wrapfd2.
	 */
	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd1), 0);
	ptr1 = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd1, 0);
	ASSERT_NE(ptr1, MAP_FAILED);
	ASSERT_EQ(munmap(ptr1, self->size), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd1), 0);

	ptr2 = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd2, 0);
	ASSERT_NE(ptr2, MAP_FAILED);
	ASSERT_EQ(munmap(ptr2, self->size), 0);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd2), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd2), 0);

	poll_thread_finish_poll(_metadata, &poll_ctl);

	ASSERT_EQ(wrapfd_query_events(self->dev_fd, events, 2, &count), 0);
	ASSERT_EQ(count, 2);
	ASSERT_TRUE((events[0].fd == wrapfd1 && events[1].fd == wrapfd2) ||
		    (events[0].fd == wrapfd2 && events[1].fd == wrapfd1));
	ASSERT_EQ(events[0].events, WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP |
				    WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN);
	ASSERT_EQ(events[1].events, WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP |
				    WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN);
	ASSERT_TRUE(events[0].timestamp >= last_ts);
	ASSERT_TRUE(events[1].timestamp >= last_ts);
	last_ts = events[0].timestamp > events[1].timestamp ?
		  events[0].timestamp : events[1].timestamp;

	/*
	 * Remove OWN and UNOWN monitoring from wrapfd1 and verify only
	 * monitored events (MAP and UNMAP) are reported.
	 */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENT_OWN | WRAPFD_EVENT_UNOWN;
	ASSERT_EQ(wrapfd_del_monitors(self->dev_fd, monitors, results, 1), 0);
	ASSERT_EQ(results[0], 0);

	poll_thread_start_poll(&poll_ctl);
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd1), 0);
	ptr1 = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED, wrapfd1, 0);
	ASSERT_NE(ptr1, MAP_FAILED);
	ASSERT_EQ(munmap(ptr1, self->size), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd1), 0);
	verify_single_event(_metadata, self, &poll_ctl, wrapfd1,
			    WRAPFD_EVENT_MAP | WRAPFD_EVENT_UNMAP, &last_ts);

	/* Stop poll thread and clean up */
	poll_thread_stop(&poll_ctl);
	ASSERT_EQ(pthread_join(poll_thread, NULL), 0);
	pthread_cond_destroy(&poll_ctl.sync_cond);
	pthread_condattr_destroy(&cond_attr);
	pthread_mutex_destroy(&poll_ctl.sync_lock);
	pthread_mutexattr_destroy(&mutex_attr);

	/* Remove remaining monitors before closing wrapfd files */
	monitors[0].fd = wrapfd1;
	monitors[0].events = WRAPFD_EVENTS_MASK;
	monitors[1].fd = wrapfd2;
	monitors[1].events = WRAPFD_EVENTS_MASK;
	ASSERT_EQ(wrapfd_del_monitors(self->dev_fd, monitors, results, 2), 0);
	ASSERT_EQ(results[0], 0);
	ASSERT_EQ(results[1], 0);

	close(wrapfd2);
	close(wrapfd1);
}

static void run_tests(struct __test_metadata *_metadata,
		      FIXTURE_DATA(wrapfd_tests) *self, int fd, int rdonly_fd)
{
	test_wrap(_metadata, self, fd);
	test_wrap_rdonly_file(_metadata, self, rdonly_fd);
	test_load_mapped(_metadata, self, fd);
	test_load_unmapped(_metadata, self, fd);
	test_load_rdonly_file(_metadata, self, rdonly_fd);
	test_wrap_rdonly(_metadata, self, fd);
	test_wrap_rdwr(_metadata, self, fd);
	test_remap_file_pages(_metadata, self, fd);
	test_wrap_remap(_metadata, self, fd);
	test_wrap_fork(_metadata, self, fd);
	test_dup(_metadata, self, fd);
	test_owner(_metadata, self, fd);
	test_rewrap(_metadata, self, fd);
	test_rewrap_rdonly_file(_metadata, self, rdonly_fd);
	test_empty(_metadata, self, fd);
	test_close_on_exec(_metadata, self, fd);
	test_guests(_metadata, self, fd);
	test_ioctl(_metadata, self, fd);
	test_lseek(_metadata, self, fd);
	test_poll_ioctls(_metadata, self, fd);
	test_poll(_metadata, self, fd);
}

TEST_F(wrapfd_tests, wrapfd_test_dmabuf_system_heap)
{
	int dmabuf_fd;
	int rdonly_dmabuf_fd;
	int heap_fd;

	heap_fd = open("/dev/dma_heap/system", O_RDONLY);
	if (heap_fd < 0 && errno == ENOENT)
		SKIP(return, "Skipping system heap tests as the system heap is missing");
	ASSERT_TRUE(heap_fd >= 0);
	dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDWR);
	ASSERT_TRUE(dmabuf_fd >= 0);
	rdonly_dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDONLY);
	ASSERT_TRUE(rdonly_dmabuf_fd >= 0);
	close(heap_fd);
	run_tests(_metadata, self, dmabuf_fd, rdonly_dmabuf_fd);
	close(rdonly_dmabuf_fd);
	close(dmabuf_fd);
}

TEST_F(wrapfd_tests, wrapfd_test_dmabuf_cma_heap)
{
	int dmabuf_fd;
	int rdonly_dmabuf_fd;
	int heap_fd;

	heap_fd = open("/dev/dma_heap/default_cma_region", O_RDONLY);
	if (heap_fd < 0 && errno == ENOENT)
		SKIP(return, "Skipping CMA tests as the CMA heap is missing");
	ASSERT_TRUE(heap_fd >= 0);
	dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDWR);
	ASSERT_TRUE(dmabuf_fd >= 0);
	rdonly_dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDONLY);
	ASSERT_TRUE(rdonly_dmabuf_fd >= 0);
	close(heap_fd);
	run_tests(_metadata, self, dmabuf_fd, rdonly_dmabuf_fd);
	close(rdonly_dmabuf_fd);
	close(dmabuf_fd);
}

TEST_HARNESS_MAIN
