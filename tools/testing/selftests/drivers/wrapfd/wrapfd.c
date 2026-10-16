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
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/align.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/wrapfd.h>
#include "../../kselftest_harness.h"
#include "load_gate.h"

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

/* Returns @ret, or -errno if @ret is negative. */
static int ret_or_errno(int ret)
{
	return ret < 0 ? -errno : ret;
}

/* Returns the WRAPFD_CONTENT_* state of @wrapfd, or -errno. */
static int state_of(int wrapfd)
{
	unsigned int state;

	return wrapfd_get_state(wrapfd, &state) ? -errno : (int)state;
}

/* Returns lseek(@fd, 0, SEEK_END), or -errno. */
static off_t seek_end(int fd)
{
	off_t ret = lseek(fd, 0, SEEK_END);

	return ret < 0 ? -errno : ret;
}

/* Maps @fd read-only and unmaps it again. Returns 0, or the -errno of mmap(). */
static int try_map(int fd, size_t len)
{
	void *ptr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);

	if (ptr == MAP_FAILED)
		return -errno;
	munmap(ptr, len);
	return 0;
}

/*
 * Returns 0 if a new read-only mapping of @wrapfd shows the content, 1 if not, or the -errno of
 * mmap(). It asserts nothing, so forked workers can use it.
 */
static int map_and_compare(FIXTURE_DATA(wrapfd_tests) *self, int wrapfd)
{
	void *ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, wrapfd, 0);
	int ret;

	if (ptr == MAP_FAILED)
		return -errno;
	ret = memcmp(ptr, self->content, self->size) ? 1 : 0;
	munmap(ptr, self->size);
	return ret;
}

/*
 * Skips a test that pins behaviour requested in @bug and not implemented yet, unless
 * WRAPFD_KNOWN_BUGS=1 is set in the environment. The fix for the bug removes the
 * SKIP_UNTIL_FIXED().
 */
#define SKIP_UNTIL_FIXED(bug) do { \
	const char *__known_bugs = getenv("WRAPFD_KNOWN_BUGS"); \
	if (!__known_bugs || strcmp(__known_bugs, "1")) \
		SKIP(return, "%s not fixed; set WRAPFD_KNOWN_BUGS=1 to run", bug); \
} while (0)

/*
 * A forked worker process. worker_start() returns 0 in the worker and the worker's pid in the
 * test process, like fork(). Fork workers while the test process holds no mapping of the
 * dma-buf: once a wrap has been mapped, every mapping of its dma-buf counts as a mapping of the
 * wrap, inherited ones included.
 *
 * The two pipes form a lockstep handshake: the worker announces a state with worker_ready() and
 * waits in worker_wait_go(); the test process waits for it with worker_wait_ready() and lets it
 * continue with worker_go(). Workers report failures through EXPECT_*() only, which records them
 * in the shared test metadata: an ASSERT_*() in a worker would run the fixture teardown there.
 * While a worker waits in worker_wait_go(), the test process checks with EXPECT_*() too, so that
 * a failed check still lets the worker go and be joined.
 */
struct worker {
	pid_t pid;
	int ready_fd;	/* worker: write end; test process: read end */
	int go_fd;	/* worker: read end; test process: write end */
};

static pid_t worker_start(struct __test_metadata *_metadata, struct worker *wk)
{
	pid_t parent = getpid();
	int ready[2], go[2];

	/* A write to a dead worker's pipe should fail the check, not kill the test. */
	signal(SIGPIPE, SIG_IGN);
	ASSERT_EQ(0, pipe2(ready, O_CLOEXEC));
	ASSERT_EQ(0, pipe2(go, O_CLOEXEC));
	fflush(NULL);
	wk->pid = fork();
	ASSERT_LE(0, wk->pid);
	if (!wk->pid) {
		/* Die with the test process instead of keeping wraps owned or mapped. */
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (getppid() != parent)
			_exit(1);
		close(ready[0]);
		close(go[1]);
		wk->ready_fd = ready[1];
		wk->go_fd = go[0];
		return 0;
	}
	close(ready[1]);
	close(go[0]);
	wk->ready_fd = ready[0];
	wk->go_fd = go[1];
	return wk->pid;
}

static void worker_ready(struct worker *wk)
{
	char c = 0;

	if (write(wk->ready_fd, &c, 1) != 1)
		_exit(1);
}

static void worker_wait_go(struct worker *wk)
{
	char c;

	if (read(wk->go_fd, &c, 1) != 1)
		_exit(1);
}

/*
 * Ends the worker. Called from the handler of a failed EXPECT_*(), it must clear the shared
 * trigger that the handler loop would otherwise reset, or the test process's next ASSERT_*()
 * would abort.
 */
static void __attribute__((noreturn)) worker_exit(struct __test_metadata *_metadata)
{
	_metadata->trigger = 0;
	_exit(0);
}

static void worker_wait_ready(struct __test_metadata *_metadata, struct worker *wk)
{
	char c;

	ASSERT_EQ(1, read(wk->ready_fd, &c, 1))
		TH_LOG("worker %d exited before it was ready", wk->pid);
}

static void worker_go(struct __test_metadata *_metadata, struct worker *wk)
{
	char c = 0;

	ASSERT_EQ(1, write(wk->go_fd, &c, 1));
}

/* Reaps the worker and checks that it exited normally. */
static void worker_join(struct __test_metadata *_metadata, struct worker *wk)
{
	int status;

	close(wk->ready_fd);
	close(wk->go_fd);
	ASSERT_EQ(wk->pid, waitpid(wk->pid, &status, 0));
	ASSERT_TRUE(WIFEXITED(status) && !WEXITSTATUS(status))
		TH_LOG("worker %d ended with status 0x%x", wk->pid, status);
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
		/*
		 * Check the content from the child, through the inherited mapping and through a
		 * mapping of its own: any process can map an unowned wrap. Use EXPECT_*() only, as
		 * an ASSERT_*() would run the fixture teardown in the child.
		 */
		EXPECT_EQ(dmabuf_sync_start(wrapfd, DMA_BUF_SYNC_READ), 0);
		EXPECT_EQ(memcmp(self->content, ptr, self->size), 0);
		EXPECT_EQ(dmabuf_sync_end(wrapfd, DMA_BUF_SYNC_READ), 0);
		EXPECT_EQ(map_and_compare(self, wrapfd), 0);
		_exit(EXIT_SUCCESS);
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
	struct worker wk;
	int wrapfd;
	char *ptr;

	wrapfd = wrapfd_wrap(self->dev_fd, fd, PROT_READ);
	ASSERT_TRUE(wrapfd >= 0);

	/* Try taking ownership while another process maps the wrapfd */
	if (!worker_start(_metadata, &wk)) {
		ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, wrapfd, 0);
		EXPECT_NE(ptr, MAP_FAILED)
			worker_exit(_metadata);
		worker_ready(&wk);
		worker_wait_go(&wk);
		munmap(ptr, self->size);
		worker_exit(_metadata);
	}
	worker_wait_ready(_metadata, &wk);
	EXPECT_TRUE(wrapfd_acquire_ownership(wrapfd) && errno == EINVAL);
	worker_go(_metadata, &wk);
	worker_join(_metadata, &wk);

	/* The EINVAL came from the other process's mapping */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

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
	struct worker wk;
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

	/* The rewrapped buffer keeps its size. REWRAP clears the bytes, so they are not checked. */
	ASSERT_EQ(lseek(wrapfd2, 0, SEEK_END), lseek(fd, 0, SEEK_END));

	/* The empty original wrap file stays owned by this process */
	if (!worker_start(_metadata, &wk)) {
		EXPECT_TRUE(wrapfd_acquire_ownership(wrapfd) && errno == EBUSY);
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

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
	struct worker wk;
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

	/* The wrap file stays open as a handle to the empty wrap */
	ASSERT_NE(fcntl(wrapfd, F_GETFD), -1);

	/* Try emptying the empty buffer */
	ASSERT_TRUE(wrapfd_empty(wrapfd) < 0 && errno == ENOENT);

	/* Try mapping the empty wrap file */
	ptr = mmap(NULL, self->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   wrapfd, 0);
	ASSERT_TRUE(ptr == MAP_FAILED && errno == ENOENT);

	/* Try loading into an empty wrap file. */
	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == ENOENT);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	/* Ownership survived EMPTY, and is gone now */
	ASSERT_TRUE(wrapfd_release_ownership(wrapfd) < 0 && errno == EBUSY);

	/* Once unowned, the empty wrap file refuses ACQUIRE, LOAD and mmap in any process */
	ASSERT_TRUE(wrapfd_acquire_ownership(wrapfd) < 0 && errno == ENOENT);
	ASSERT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 && errno == ENOENT);
	ASSERT_EQ(try_map(wrapfd, self->size), -ENOENT);
	if (!worker_start(_metadata, &wk)) {
		EXPECT_TRUE(wrapfd_acquire_ownership(wrapfd) < 0 && errno == ENOENT);
		EXPECT_TRUE(wrapfd_load(wrapfd, self->fd, 0, 0, self->size) < 0 &&
			    errno == ENOENT);
		EXPECT_EQ(try_map(wrapfd, self->size), -ENOENT);
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

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
	struct worker wk;
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

	/* Another process may map the owned buffer now */
	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(try_map(wrapfd, self->size), 0);
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	/* Release buffer ownership */
	ASSERT_EQ(wrapfd_release_ownership(wrapfd), 0);

	/* RELEASE prohibited guests again, so the next owner starts with guests prohibited */
	ASSERT_EQ(wrapfd_acquire_ownership(wrapfd), 0);
	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(try_map(wrapfd, self->size), -EBUSY);
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);
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
}

TEST_F(wrapfd_tests, wrapfd_test_dmabuf_system_heap)
{
	int dmabuf_fd;
	int rdonly_dmabuf_fd;
	int heap_fd;

	heap_fd = open("/dev/dma_heap/system", O_RDONLY);
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

/*
 * Behaviour tests. Each pins one behaviour of the wrapfd uapi that userspace buffer management
 * relies on: ownership across processes and threads, mappings held by other processes, dead
 * owners, and the state transitions of EMPTY, REWRAP and LOAD. Each runs on a wrap and a dma-buf
 * of its own, so that every behaviour has a result of its own.
 *
 * Tests that pin behaviour requested in a bug but not implemented yet are skipped unless
 * WRAPFD_KNOWN_BUGS=1 is set in the environment (SKIP_UNTIL_FIXED()).
 */

/*
 * Wraps a new system-heap dma-buf read-write and returns the wrap. If @buf is not NULL, it
 * receives the dma-buf's fd; otherwise that fd is closed and the wrap keeps the dma-buf. The
 * dma-buf is never mapped through its own fd: once a wrap has been mapped, every mapping of its
 * dma-buf counts as a mapping of the wrap.
 */
static int new_wrap(struct __test_metadata *_metadata, FIXTURE_DATA(wrapfd_tests) *self,
		    int *buf)
{
	int heap_fd, dmabuf_fd, wrapfd;

	heap_fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
	ASSERT_LE(0, heap_fd);
	dmabuf_fd = dmabuf_heap_alloc(heap_fd, self->size, O_RDWR);
	close(heap_fd);
	ASSERT_LE(0, dmabuf_fd);
	wrapfd = wrapfd_wrap(self->dev_fd, dmabuf_fd, PROT_READ | PROT_WRITE);
	ASSERT_LE(0, wrapfd);
	if (buf)
		*buf = dmabuf_fd;
	else
		close(dmabuf_fd);
	return wrapfd;
}

/* Loads the whole content file into @w. Returns 0, or -errno. */
static int load_all(FIXTURE_DATA(wrapfd_tests) *self, int w)
{
	return ret_or_errno(wrapfd_load(w, self->fd, 0, 0, self->size));
}

/*
 * Threads hand their results back to the test thread, which checks them: the harness is not
 * thread-safe.
 */
struct thread_call {
	int (*fn)(void *arg);
	void *arg;
	int ret;
};

static void *thread_call_main(void *p)
{
	struct thread_call *call = p;

	call->ret = call->fn(call->arg);
	return NULL;
}

/* Runs @fn(@arg) on another thread of this process and returns its result. */
static int run_on_thread(struct __test_metadata *_metadata, int (*fn)(void *), void *arg)
{
	struct thread_call call = { .fn = fn, .arg = arg };
	pthread_t thread;

	ASSERT_EQ(0, pthread_create(&thread, NULL, thread_call_main, &call));
	ASSERT_EQ(0, pthread_join(thread, NULL));
	return call.ret;
}

static int thread_release(void *w)
{
	return ret_or_errno(wrapfd_release_ownership(*(int *)w));
}

static int thread_empty(void *w)
{
	return ret_or_errno(wrapfd_empty(*(int *)w));
}

struct load_args {
	FIXTURE_DATA(wrapfd_tests) *self;
	int w;
};

static int thread_load(void *p)
{
	struct load_args *args = p;

	return load_all(args->self, args->w);
}

struct rewrap_args {
	int w;
	int new_fd;	/* out: the result of REWRAP */
	int new_state;	/* out: the state of the new wrap */
};

static int thread_rewrap(void *p)
{
	struct rewrap_args *args = p;

	args->new_fd = ret_or_errno(wrapfd_rewrap(args->w, PROT_READ));
	if (args->new_fd < 0)
		return args->new_fd;
	args->new_state = state_of(args->new_fd);
	return 0;
}

/*
 * The wrap that REWRAP returns starts unowned, so another process can acquire it while the old
 * wrap is still owned. Closing the old wrap leaves the new wrap's state intact, and the new wrap
 * can still be acquired. REWRAP clears the buffer, so the bytes are not checked.
 */
TEST_F(wrapfd_tests, rewrapped_wrap_is_independent_of_old_wrap)
{
	struct worker wk;
	int w, n;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	n = ret_or_errno(wrapfd_rewrap(w, PROT_READ));
	ASSERT_LE(0, n);

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(n)));
		EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(n)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, close(w));
	ASSERT_EQ(WRAPFD_CONTENT_RDONLY, state_of(n));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(n)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(n)));
}

/*
 * Ownership belongs to the process: another thread of the owner can LOAD into and EMPTY the
 * wrap.
 */
TEST_F(wrapfd_tests, other_thread_of_owner_can_load_and_empty)
{
	struct load_args load;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	load = (struct load_args){ .self = self, .w = w };
	ASSERT_EQ(0, run_on_thread(_metadata, thread_load, &load));
	ASSERT_EQ(0, map_and_compare(self, w));

	ASSERT_EQ(0, run_on_thread(_metadata, thread_empty, &w));
	ASSERT_EQ(WRAPFD_CONTENT_EMPTY, state_of(w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * RELEASE from another thread of the owner ends the process's ownership: EMPTY from the acquiring
 * thread then fails with EBUSY, and another process can acquire the wrap.
 */
TEST_F(wrapfd_tests, release_by_other_thread_of_owner_ends_ownership)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	ASSERT_EQ(0, run_on_thread(_metadata, thread_release, &w));
	ASSERT_EQ(-EBUSY, ret_or_errno(wrapfd_empty(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	/* The EBUSY came from the lost ownership. */
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_empty(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * While another process owns the wrap, ACQUIRE fails with EBUSY, also after the owner emptied it:
 * the owner check comes before the content check. ENOENT appears only once the owner released the
 * empty wrap.
 */
TEST_F(wrapfd_tests, acquire_fails_ebusy_while_other_process_owns)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		worker_ready(&wk);
		worker_wait_go(&wk);
		EXPECT_EQ(0, ret_or_errno(wrapfd_empty(w)));
		worker_ready(&wk);
		worker_wait_go(&wk);
		EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
		worker_exit(_metadata);
	}
	worker_wait_ready(_metadata, &wk);
	EXPECT_EQ(-EBUSY, ret_or_errno(wrapfd_acquire_ownership(w)));	/* owned, with content */
	worker_go(_metadata, &wk);
	worker_wait_ready(_metadata, &wk);
	EXPECT_EQ(-EBUSY, ret_or_errno(wrapfd_acquire_ownership(w)));	/* owned, empty */
	worker_go(_metadata, &wk);
	worker_join(_metadata, &wk);
	ASSERT_EQ(-ENOENT, ret_or_errno(wrapfd_acquire_ownership(w)));	/* unowned, empty */
}

/* A second ACQUIRE by the owner succeeds, and ownership is not counted: one RELEASE ends it. */
TEST_F(wrapfd_tests, owner_reacquire_succeeds_and_one_release_ends_ownership)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);
}

/*
 * RELEASE of an unowned wrap, or by a process that does not own the wrap, fails with EBUSY and
 * leaves the wrap and its ownership as they were.
 */
TEST_F(wrapfd_tests, release_by_non_owner_fails_ebusy)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(-EBUSY, ret_or_errno(wrapfd_release_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(-EBUSY, ret_or_errno(wrapfd_release_ownership(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * The owner can RELEASE while a guest process maps the wrap; ownership ends and the mapping stays
 * valid. Requested in b/568458393: RELEASE fails with EINVAL while the wrap is mapped, which
 * test_owner() asserts.
 */
TEST_F(wrapfd_tests, release_succeeds_while_mapped)
{
	struct worker wk;
	void *ptr;
	int w;

	SKIP_UNTIL_FIXED("b/568458393");

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_allow_guests(w)));

	if (!worker_start(_metadata, &wk)) {
		ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, w, 0);
		EXPECT_NE(MAP_FAILED, ptr)
			worker_exit(_metadata);
		worker_ready(&wk);
		worker_wait_go(&wk);
		munmap(ptr, self->size);
		worker_exit(_metadata);
	}
	worker_wait_ready(_metadata, &wk);
	EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
	worker_go(_metadata, &wk);
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * While a guest process maps the wrap, the owner's EMPTY and REWRAP fail with EINVAL and leave the
 * content in place. Once the guest has unmapped, EMPTY succeeds.
 */
TEST_F(wrapfd_tests, empty_and_rewrap_fail_einval_when_mapped_by_other_process)
{
	struct worker wk;
	void *ptr;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_allow_guests(w)));

	if (!worker_start(_metadata, &wk)) {
		ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, w, 0);
		EXPECT_NE(MAP_FAILED, ptr)
			worker_exit(_metadata);
		worker_ready(&wk);
		worker_wait_go(&wk);
		munmap(ptr, self->size);
		worker_exit(_metadata);
	}
	worker_wait_ready(_metadata, &wk);
	EXPECT_EQ(-EINVAL, ret_or_errno(wrapfd_empty(w)));
	EXPECT_EQ(-EINVAL, ret_or_errno(wrapfd_rewrap(w, PROT_READ)));
	EXPECT_EQ(WRAPFD_CONTENT_RDWR, state_of(w));
	worker_go(_metadata, &wk);
	worker_join(_metadata, &wk);

	/* The EINVALs came from the mapping. */
	ASSERT_EQ(0, ret_or_errno(wrapfd_empty(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * After another thread of the owner moved the content out with REWRAP, EMPTY on the old wrap fails
 * with ENOENT: REWRAP is atomic, so EMPTY never sees a half-moved wrap.
 */
TEST_F(wrapfd_tests, empty_after_rewrap_by_other_thread_fails_enoent)
{
	struct rewrap_args rewrap;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	rewrap = (struct rewrap_args){ .w = w };
	ASSERT_EQ(0, run_on_thread(_metadata, thread_rewrap, &rewrap));
	ASSERT_EQ(WRAPFD_CONTENT_RDONLY, rewrap.new_state);

	ASSERT_EQ(-ENOENT, ret_or_errno(wrapfd_empty(w)));
}

/*
 * A REWRAP that fails after its checks passed (here no fd can be allocated, EMFILE) leaves the
 * content in the old wrap with its state and bytes unchanged.
 */
TEST_F(wrapfd_tests, rewrap_failure_leaves_content_in_place)
{
	const struct rlimit no_files = { 0, 0 };
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		EXPECT_EQ(0, setrlimit(RLIMIT_NOFILE, &no_files));
		EXPECT_EQ(-EMFILE, ret_or_errno(wrapfd_rewrap(w, PROT_READ | PROT_WRITE)));
		EXPECT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(WRAPFD_CONTENT_RDWR, state_of(w));
	ASSERT_EQ(0, map_and_compare(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/* LOAD into an unowned wrap needs no ACQUIRE, from a process that never owned the wrap. */
TEST_F(wrapfd_tests, load_needs_no_ownership_on_unowned_wrap)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, load_all(self, w));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, map_and_compare(self, w));
}

/*
 * While another process owns the wrap and guests are prohibited, LOAD and lseek fail with EBUSY,
 * as mmap does. Requested in b/567193983: neither checks the owner.
 */
TEST_F(wrapfd_tests, load_and_lseek_by_non_owner_process_fail_ebusy_while_owned)
{
	struct worker wk;
	int w;

	SKIP_UNTIL_FIXED("b/567193983");

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(-EBUSY, load_all(self, w));
		EXPECT_EQ(-EBUSY, seek_end(w));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * LOAD fails with EINVAL when the range runs past the end of the source file or of the buffer,
 * and the wrap stays usable.
 */
TEST_F(wrapfd_tests, load_rejects_out_of_range_einval)
{
	off_t bufsize;
	int w;

	w = new_wrap(_metadata, self, NULL);
	/* The buffer is page-rounded, so it ends after the content file. */
	bufsize = seek_end(w);
	ASSERT_EQ((off_t)ALIGN(self->size, self->page_size), bufsize);
	ASSERT_EQ(-EINVAL, ret_or_errno(wrapfd_load(w, self->fd, 1, 0, self->size)));
	ASSERT_EQ(-EINVAL, ret_or_errno(wrapfd_load(w, self->fd, 0, bufsize, 1)));
	ASSERT_EQ(0, load_all(self, w));
}

/*
 * While a process owns the wrap and guests are prohibited, mmap and forwarded dma-buf ioctls
 * (DMA_BUF_IOCTL_SYNC) from another process fail with EBUSY. After RELEASE they succeed.
 */
TEST_F(wrapfd_tests, mmap_and_dmabuf_ioctl_by_other_process_fail_ebusy_while_owned)
{
	struct worker wk;
	void *ptr;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(-EBUSY, try_map(w, self->size));
		EXPECT_EQ(-EBUSY, ret_or_errno(dmabuf_sync_start(w, DMA_BUF_SYNC_READ)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));

	/* The EBUSYs came from the ownership. */
	if (!worker_start(_metadata, &wk)) {
		ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, w, 0);
		EXPECT_NE(MAP_FAILED, ptr)
			worker_exit(_metadata);
		EXPECT_EQ(0, ret_or_errno(dmabuf_sync_start(w, DMA_BUF_SYNC_READ)));
		EXPECT_EQ(0, ret_or_errno(dmabuf_sync_end(w, DMA_BUF_SYNC_READ)));
		munmap(ptr, self->size);
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);
}

/* GET_STATE has no owner check: it works from a process that does not own the wrap. */
TEST_F(wrapfd_tests, get_state_by_non_owner_process_succeeds_while_owned)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(WRAPFD_CONTENT_RDWR, state_of(w));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * A failed lseek on an empty wrap has no side effect: ACQUIRE afterwards still fails with ENOENT
 * (empty), not EINVAL. The C driver (android16-6.12) returns EINVAL here, b/567187927.
 */
TEST_F(wrapfd_tests, wrap_lseek_end_on_empty_wrap_has_no_side_effect)
{
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_empty(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));

	ASSERT_EQ(-ENOENT, seek_end(w));
	ASSERT_EQ(-ENOENT, ret_or_errno(wrapfd_acquire_ownership(w)));
}

/* When the owner exits without releasing, another process can acquire the wrap. */
TEST_F(wrapfd_tests, dead_owner_reset_allows_acquire_by_another_process)
{
	struct worker wk;
	int w;

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * ALLOW_GUESTS granted by an owner that died does not carry over to the next owner. Requested in
 * b/567279947: the dead-owner reset clears the owner only.
 */
TEST_F(wrapfd_tests, allow_guests_cleared_when_owner_dies)
{
	struct worker wk;
	int w;

	SKIP_UNTIL_FIXED("b/567279947");

	w = new_wrap(_metadata, self, NULL);
	ASSERT_EQ(0, load_all(self, w));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
		EXPECT_EQ(0, ret_or_errno(wrapfd_allow_guests(w)));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));

	if (!worker_start(_metadata, &wk)) {
		EXPECT_EQ(-EBUSY, try_map(w, self->size));
		worker_exit(_metadata);
	}
	worker_join(_metadata, &wk);

	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * LOAD succeeds while another process maps the unowned wrap, and that existing mapping shows the
 * loaded bytes once LOAD has returned.
 */
TEST_F(wrapfd_tests, mapping_held_across_load_shows_loaded_bytes)
{
	struct worker wk;
	void *ptr;
	int w;

	w = new_wrap(_metadata, self, NULL);

	if (!worker_start(_metadata, &wk)) {
		ptr = mmap(NULL, self->size, PROT_READ, MAP_SHARED, w, 0);
		EXPECT_NE(MAP_FAILED, ptr)
			worker_exit(_metadata);
		worker_ready(&wk);
		worker_wait_go(&wk);
		EXPECT_EQ(0, memcmp(ptr, self->content, self->size));
		munmap(ptr, self->size);
		worker_exit(_metadata);
	}
	worker_wait_ready(_metadata, &wk);
	EXPECT_EQ(0, load_all(self, w));
	worker_go(_metadata, &wk);
	worker_join(_metadata, &wk);
}

/*
 * Behaviour while a LOAD is in flight. The LOAD is held inside the driver (load_gate.h), and the
 * call under test runs on another thread as a probe, which either returns while the LOAD is held
 * or waits for it.
 */

/*
 * Holds a LOAD of @len bytes at offset @offs of the content file into @w, at the same offset in
 * the buffer. @buf is @w's dma-buf. Returns false if the test was skipped because the kernel
 * cannot hold a LOAD.
 */
static bool hold_load(struct __test_metadata *_metadata, FIXTURE_DATA(wrapfd_tests) *self,
		      struct load_gate *gate, int buf, int w, size_t offs, size_t len)
{
	int ret = load_gate_hold(gate, buf, w, self->fd, offs, offs, len);

	if (ret == -ENODEV)
		SKIP(return false, "cannot hold a LOAD: the kernel lacks sw_sync");
	ASSERT_EQ(0, ret);
	ASSERT_EQ(0, load_gate_wait_held(gate));
	return true;
}

static int probe_acquire(struct probe *probe)
{
	return ret_or_errno(wrapfd_acquire_ownership(probe->wrapfd));
}

static int probe_release(struct probe *probe)
{
	return ret_or_errno(wrapfd_release_ownership(probe->wrapfd));
}

static int probe_empty(struct probe *probe)
{
	return ret_or_errno(wrapfd_empty(probe->wrapfd));
}

/* Returns the new wrap's fd, or -errno. */
static int probe_rewrap(struct probe *probe)
{
	return ret_or_errno(wrapfd_rewrap(probe->wrapfd, PROT_READ | PROT_WRITE));
}

static int probe_get_state(struct probe *probe)
{
	return state_of(probe->wrapfd);
}

struct load_range {
	int fd;
	size_t offs;	/* both in the file and in the buffer */
	size_t len;
};

static int probe_load(struct probe *probe)
{
	struct load_range *range = probe->arg;

	return ret_or_errno(wrapfd_load(probe->wrapfd, range->fd, range->offs, range->offs,
					range->len));
}

/*
 * While a LOAD is in flight on an unowned wrap, ACQUIRE fails with EINVAL at once instead of
 * waiting for the LOAD.
 */
TEST_F(wrapfd_tests, acquire_fails_einval_during_load)
{
	enum probe_outcome outcome;
	struct load_gate gate;
	struct probe probe;
	int buf, w, ret;

	w = new_wrap(_metadata, self, &buf);
	if (!hold_load(_metadata, self, &gate, buf, w, 0, self->size))
		return;
	ASSERT_EQ(0, probe_start(&probe, probe_acquire, w, NULL));
	outcome = probe_settle(&probe);
	ASSERT_EQ(0, load_gate_release(&gate));
	ret = probe_join(&probe);
	/* Undo an ACQUIRE that waited for the LOAD and succeeded. */
	if (!ret)
		wrapfd_release_ownership(w);

	ASSERT_EQ(PROBE_RETURNED, outcome)
		TH_LOG("ACQUIRE waited for the LOAD");
	ASSERT_EQ(-EINVAL, ret);
}

/*
 * While a LOAD is in flight, the owner's EMPTY and REWRAP fail with EINVAL at once, and the loaded
 * content stays.
 */
TEST_F(wrapfd_tests, empty_and_rewrap_fail_einval_during_load)
{
	enum probe_outcome empty_outcome, rewrap_outcome;
	int buf, w, empty_ret, rewrap_ret;
	struct probe empty, rewrap;
	struct load_gate gate;

	w = new_wrap(_metadata, self, &buf);
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	/* The loader is another thread of the owner. */
	if (!hold_load(_metadata, self, &gate, buf, w, 0, self->size))
		return;
	ASSERT_EQ(0, probe_start(&empty, probe_empty, w, NULL));
	ASSERT_EQ(0, probe_start(&rewrap, probe_rewrap, w, NULL));
	empty_outcome = probe_settle(&empty);
	rewrap_outcome = probe_settle(&rewrap);
	ASSERT_EQ(0, load_gate_release(&gate));
	empty_ret = probe_join(&empty);
	rewrap_ret = probe_join(&rewrap);
	if (rewrap_ret >= 0)
		close(rewrap_ret);

	ASSERT_EQ(PROBE_RETURNED, empty_outcome)
		TH_LOG("EMPTY waited for the LOAD");
	ASSERT_EQ(PROBE_RETURNED, rewrap_outcome)
		TH_LOG("REWRAP waited for the LOAD");
	ASSERT_EQ(-EINVAL, empty_ret);
	ASSERT_EQ(-EINVAL, rewrap_ret);
	ASSERT_EQ(WRAPFD_CONTENT_RDWR, state_of(w));
	ASSERT_EQ(0, ret_or_errno(wrapfd_release_ownership(w)));
}

/*
 * While a LOAD is in flight, the owner's RELEASE succeeds. Whether it waits for the LOAD is
 * release_returns_without_waiting_during_load.
 */
TEST_F(wrapfd_tests, release_succeeds_during_load)
{
	enum probe_outcome outcome;
	struct load_gate gate;
	struct probe probe;
	int buf, w, ret;

	w = new_wrap(_metadata, self, &buf);
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	if (!hold_load(_metadata, self, &gate, buf, w, 0, self->size))
		return;
	ASSERT_EQ(0, probe_start(&probe, probe_release, w, NULL));
	outcome = probe_settle(&probe);
	ASSERT_EQ(0, load_gate_release(&gate));
	ret = probe_join(&probe);
	/* Undo a RELEASE that failed. */
	if (ret)
		wrapfd_release_ownership(w);

	ASSERT_NE(PROBE_RUNNING, outcome)
		TH_LOG("RELEASE did not reach the driver while the LOAD was held");
	ASSERT_EQ(0, ret);
}

/* While a LOAD is in flight, the owner's RELEASE returns without waiting for the LOAD. */
TEST_F(wrapfd_tests, release_returns_without_waiting_during_load)
{
	enum probe_outcome outcome;
	struct load_gate gate;
	struct probe probe;
	int buf, w;

	w = new_wrap(_metadata, self, &buf);
	ASSERT_EQ(0, ret_or_errno(wrapfd_acquire_ownership(w)));
	if (!hold_load(_metadata, self, &gate, buf, w, 0, self->size))
		return;
	ASSERT_EQ(0, probe_start(&probe, probe_release, w, NULL));
	outcome = probe_settle(&probe);
	ASSERT_EQ(0, load_gate_release(&gate));
	/* Undo a RELEASE that failed. */
	if (probe_join(&probe))
		wrapfd_release_ownership(w);

	ASSERT_EQ(PROBE_RETURNED, outcome)
		TH_LOG("RELEASE waited for the LOAD");
}

/* GET_STATE returns the current state at once while a LOAD is in flight. */
TEST_F(wrapfd_tests, get_state_returns_without_waiting_during_load)
{
	enum probe_outcome outcome;
	struct load_gate gate;
	struct probe probe;
	int buf, w;

	w = new_wrap(_metadata, self, &buf);
	if (!hold_load(_metadata, self, &gate, buf, w, 0, self->size))
		return;
	ASSERT_EQ(0, probe_start(&probe, probe_get_state, w, NULL));
	outcome = probe_settle(&probe);
	ASSERT_EQ(0, load_gate_release(&gate));
	ASSERT_EQ(WRAPFD_CONTENT_RDWR, probe_join(&probe));

	ASSERT_EQ(PROBE_RETURNED, outcome)
		TH_LOG("GET_STATE waited for the LOAD");
}

/*
 * Two LOADs into disjoint halves of one unowned wrap, the second started while the first is held
 * in flight, both succeed, and the buffer then holds both halves.
 */
TEST_F(wrapfd_tests, concurrent_loads_into_same_wrap_succeed)
{
	size_t half = self->size / 2;
	enum probe_outcome outcome;
	struct load_range second;
	struct load_gate gate;
	struct probe probe;
	int buf, w;

	w = new_wrap(_metadata, self, &buf);
	if (!hold_load(_metadata, self, &gate, buf, w, 0, half))
		return;
	second = (struct load_range){ .fd = self->fd, .offs = half, .len = self->size - half };
	ASSERT_EQ(0, probe_start(&probe, probe_load, w, &second));
	/*
	 * The second LOAD blocks inside the driver: on the same fence or, with a driver that holds
	 * the wrap's lock across a LOAD, on that lock. Which of the two is not asserted.
	 */
	outcome = probe_settle(&probe);
	ASSERT_EQ(0, load_gate_release(&gate));
	ASSERT_EQ(0, probe_join(&probe));

	ASSERT_NE(PROBE_RUNNING, outcome)
		TH_LOG("the second LOAD did not reach the driver while the first was held");
	ASSERT_EQ(0, map_and_compare(self, w));
}

TEST_HARNESS_MAIN
