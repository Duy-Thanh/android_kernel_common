// SPDX-License-Identifier: GPL-2.0-only
/* binder_alloc_selftest.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2017 Google, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm_types.h>
#include <linux/err.h>
#include "binder_internal.h"

#define BUFFER_NUM 5
#define BUFFER_MIN_SIZE (PAGE_SIZE / 8)

static bool binder_selftest_run = true;
static int binder_selftest_failures;
static DEFINE_MUTEX(binder_selftest_lock);

/**
 * enum buf_end_align_type - Page alignment of a buffer
 * end with regard to the end of the previous buffer.
 *
 * In the pictures below, buf2 refers to the buffer we
 * are aligning. buf1 refers to previous buffer by addr.
 * Symbol [ means the start of a buffer, ] means the end
 * of a buffer, and | means page boundaries.
 */
enum buf_end_align_type {
	/**
	 * @SAME_PAGE_UNALIGNED: The end of this buffer is on
	 * the same page as the end of the previous buffer and
	 * is not page aligned. Examples:
	 * buf1 ][ buf2 ][ ...
	 * buf1 ]|[ buf2 ][ ...
	 */
	SAME_PAGE_UNALIGNED = 0,
	/**
	 * @SAME_PAGE_ALIGNED: When the end of the previous buffer
	 * is not page aligned, the end of this buffer is on the
	 * same page as the end of the previous buffer and is page
	 * aligned. When the previous buffer is page aligned, the
	 * end of this buffer is aligned to the next page boundary.
	 * Examples:
	 * buf1 ][ buf2 ]| ...
	 * buf1 ]|[ buf2 ]| ...
	 */
	SAME_PAGE_ALIGNED,
	/**
	 * @NEXT_PAGE_UNALIGNED: The end of this buffer is on
	 * the page next to the end of the previous buffer and
	 * is not page aligned. Examples:
	 * buf1 ][ buf2 | buf2 ][ ...
	 * buf1 ]|[ buf2 | buf2 ][ ...
	 */
	NEXT_PAGE_UNALIGNED,
	/**
	 * @NEXT_PAGE_ALIGNED: The end of this buffer is on
	 * the page next to the end of the previous buffer and
	 * is page aligned. Examples:
	 * buf1 ][ buf2 | buf2 ]| ...
	 * buf1 ]|[ buf2 | buf2 ]| ...
	 */
	NEXT_PAGE_ALIGNED,
	/**
	 * @NEXT_NEXT_UNALIGNED: The end of this buffer is on
	 * the page that follows the page after the end of the
	 * previous buffer and is not page aligned. Examples:
	 * buf1 ][ buf2 | buf2 | buf2 ][ ...
	 * buf1 ]|[ buf2 | buf2 | buf2 ][ ...
	 */
	NEXT_NEXT_UNALIGNED,
	/**
	 * @LOOP_END: The number of enum values in &buf_end_align_type.
	 * It is used for controlling loop termination.
	 */
	LOOP_END,
};

static void pr_err_size_seq(size_t *sizes, int *seq)
{
	int i;

	pr_err("alloc sizes: ");
	for (i = 0; i < BUFFER_NUM; i++)
		pr_cont("[%zu]", sizes[i]);
	pr_cont("\n");
	pr_err("free seq: ");
	for (i = 0; i < BUFFER_NUM; i++)
		pr_cont("[%d]", seq[i]);
	pr_cont("\n");
}

static bool check_buffer_pages_allocated(struct binder_alloc *alloc,
					 struct binder_buffer *buffer,
					 size_t size)
{
	unsigned long page_addr;
	unsigned long end;
	int page_index;

	end = PAGE_ALIGN(buffer->user_data + size);
	page_addr = buffer->user_data;
	for (; page_addr < end; page_addr += PAGE_SIZE) {
		page_index = (page_addr - alloc->buffer) / PAGE_SIZE;
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
		if (!alloc->pages[page_index].page_ptr ||
		    !list_empty(&alloc->pages[page_index].lru)) {
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
		if (!alloc->pages[page_index] ||
		    !list_empty(page_to_lru(alloc->pages[page_index]))) {
=======
		if (!alloc_to_wrap(alloc)->pages[page_index] ||
		    !list_empty(page_to_lru(alloc_to_wrap(alloc)->pages[page_index]))) {
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			pr_err("expect alloc but is %s at page index %d\n",
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
			       alloc->pages[page_index].page_ptr ?
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
			       alloc->pages[page_index] ?
=======
			       alloc_to_wrap(alloc)->pages[page_index] ?
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			       "lru" : "free", page_index);
			return false;
		}
	}
	return true;
}

static void binder_selftest_alloc_buf(struct binder_alloc *alloc,
				      struct binder_buffer *buffers[],
				      size_t *sizes, int *seq)
{
	int i;

	for (i = 0; i < BUFFER_NUM; i++) {
		buffers[i] = binder_alloc_new_buf(alloc, sizes[i], 0, 0, 0);
		if (IS_ERR(buffers[i]) ||
		    !check_buffer_pages_allocated(alloc, buffers[i],
						  sizes[i])) {
			pr_err_size_seq(sizes, seq);
			binder_selftest_failures++;
		}
	}
}

static void binder_selftest_free_buf(struct binder_alloc *alloc,
				     struct binder_buffer *buffers[],
				     size_t *sizes, int *seq, size_t end)
{
	int i;

	for (i = 0; i < BUFFER_NUM; i++)
		binder_alloc_free_buf(alloc, buffers[seq[i]]);

	for (i = 0; i < end / PAGE_SIZE; i++) {
		/**
		 * Error message on a free page can be false positive
		 * if binder shrinker ran during binder_alloc_free_buf
		 * calls above.
		 */
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
		if (list_empty(&alloc->pages[i].lru)) {
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
		if (list_empty(page_to_lru(alloc->pages[i]))) {
=======
		if (list_empty(page_to_lru(alloc_to_wrap(alloc)->pages[i]))) {
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			pr_err_size_seq(sizes, seq);
			pr_err("expect lru but is %s at page index %d\n",
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
			       alloc->pages[i].page_ptr ? "alloc" : "free", i);
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
			       alloc->pages[i] ? "alloc" : "free", i);
=======
			       alloc_to_wrap(alloc)->pages[i] ? "alloc" : "free", i);
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			binder_selftest_failures++;
		}
	}
}

static void binder_selftest_free_page(struct binder_alloc *alloc)
{
	int i;
	unsigned long count;

	while ((count = list_lru_count(&binder_freelist))) {
		list_lru_walk(&binder_freelist, binder_alloc_free_page,
			      NULL, count);
	}

	for (i = 0; i < (alloc->buffer_size / PAGE_SIZE); i++) {
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
		if (alloc->pages[i].page_ptr) {
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
		if (alloc->pages[i]) {
=======
		if (alloc_to_wrap(alloc)->pages[i]) {
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			pr_err("expect free but is %s at page index %d\n",
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
			       list_empty(&alloc->pages[i].lru) ?
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
			       list_empty(page_to_lru(alloc->pages[i])) ?
=======
			       list_empty(page_to_lru(alloc_to_wrap(alloc)->pages[i])) ?
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
			       "alloc" : "lru", i);
			binder_selftest_failures++;
		}
	}
}

static void binder_selftest_alloc_free(struct binder_alloc *alloc,
				       size_t *sizes, int *seq, size_t end)
{
	struct binder_buffer *buffers[BUFFER_NUM];

	binder_selftest_alloc_buf(alloc, buffers, sizes, seq);
	binder_selftest_free_buf(alloc, buffers, sizes, seq, end);

	/* Allocate from lru. */
	binder_selftest_alloc_buf(alloc, buffers, sizes, seq);
	if (list_lru_count(&binder_freelist))
		pr_err("lru list should be empty but is not\n");

	binder_selftest_free_buf(alloc, buffers, sizes, seq, end);
	binder_selftest_free_page(alloc);
}

static bool is_dup(int *seq, int index, int val)
{
	int i;

	for (i = 0; i < index; i++) {
		if (seq[i] == val)
			return true;
	}
	return false;
}

/* Generate BUFFER_NUM factorial free orders. */
static void binder_selftest_free_seq(struct binder_alloc *alloc,
				     size_t *sizes, int *seq,
				     int index, size_t end)
{
	int i;

	if (index == BUFFER_NUM) {
		binder_selftest_alloc_free(alloc, sizes, seq, end);
		return;
	}
	for (i = 0; i < BUFFER_NUM; i++) {
		if (is_dup(seq, index, i))
			continue;
		seq[index] = i;
		binder_selftest_free_seq(alloc, sizes, seq, index + 1, end);
	}
}

static void binder_selftest_alloc_size(struct binder_alloc *alloc,
				       size_t *end_offset)
{
	int i;
	int seq[BUFFER_NUM] = {0};
	size_t front_sizes[BUFFER_NUM];
	size_t back_sizes[BUFFER_NUM];
	size_t last_offset, offset = 0;

	for (i = 0; i < BUFFER_NUM; i++) {
		last_offset = offset;
		offset = end_offset[i];
		front_sizes[i] = offset - last_offset;
		back_sizes[BUFFER_NUM - i - 1] = front_sizes[i];
	}
	/*
	 * Buffers share the first or last few pages.
	 * Only BUFFER_NUM - 1 buffer sizes are adjustable since
	 * we need one giant buffer before getting to the last page.
	 */
	back_sizes[0] += alloc->buffer_size - end_offset[BUFFER_NUM - 1];
	binder_selftest_free_seq(alloc, front_sizes, seq, 0,
				 end_offset[BUFFER_NUM - 1]);
	binder_selftest_free_seq(alloc, back_sizes, seq, 0, alloc->buffer_size);
}

static void binder_selftest_alloc_offset(struct binder_alloc *alloc,
					 size_t *end_offset, int index)
{
	int align;
	size_t end, prev;

	if (index == BUFFER_NUM) {
		binder_selftest_alloc_size(alloc, end_offset);
		return;
	}
	prev = index == 0 ? 0 : end_offset[index - 1];
	end = prev;

	BUILD_BUG_ON(BUFFER_MIN_SIZE * BUFFER_NUM >= PAGE_SIZE);

	for (align = SAME_PAGE_UNALIGNED; align < LOOP_END; align++) {
		if (align % 2)
			end = ALIGN(end, PAGE_SIZE);
		else
			end += BUFFER_MIN_SIZE;
		end_offset[index] = end;
		binder_selftest_alloc_offset(alloc, end_offset, index + 1);
	}
}

/**
 * binder_selftest_alloc() - Test alloc and free of buffer pages.
 * @alloc: Pointer to alloc struct.
 *
 * Allocate BUFFER_NUM buffers to cover all page alignment cases,
 * then free them in all orders possible. Check that pages are
 * correctly allocated, put onto lru when buffers are freed, and
 * are freed when binder_alloc_free_page is called.
 */
void binder_selftest_alloc(struct binder_alloc *alloc)
{
	size_t end_offset[BUFFER_NUM];

	if (!binder_selftest_run)
		return;
	mutex_lock(&binder_selftest_lock);
<<<<<<< HEAD   (dad6868f0c118913906b24f2bc7b56f73fd3dd62 ANDROID: Move SCX_OPS_DISABLING VH inside the scx_fork_rwsem)
	if (!binder_selftest_run || !alloc->vma)
||||||| BASE   (addbef81389450f00b64584be16e1d762cd8fdf1 UPSTREAM: binder: use per-vma lock in page reclaiming)
	if (!binder_selftest_run || !alloc->mapped)
=======
	if (!binder_selftest_run || !alloc_to_wrap(alloc)->mapped)
>>>>>>> CHANGE (c9657998a396b8947624af33f27e0eb13980b709 ANDROID: binder: KMI-break workaround for binder_alloc)
		goto done;
	pr_info("STARTED\n");
	binder_selftest_alloc_offset(alloc, end_offset, 0);
	binder_selftest_run = false;
	if (binder_selftest_failures > 0)
		pr_info("%d tests FAILED\n", binder_selftest_failures);
	else
		pr_info("PASSED\n");

done:
	mutex_unlock(&binder_selftest_lock);
}
