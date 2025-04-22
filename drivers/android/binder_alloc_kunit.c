// SPDX-License-Identifier: GPL-2.0
/*
 * Test cases for binder allocator code
 */

#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h> // struct file_operations
#include <linux/mm_types.h>
#include <linux/mm.h> // for vm_flags_mod and VM flags
#include <linux/mman.h>
#include <linux/sizes.h>

#include "binder_alloc.h"
#include "binder_internal.h"  // for binder_fops. probably can get rid of this
			      // after you pare this down

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

#define BINDER_MMAP_SIZE SZ_1M

#define BUFFER_NUM 5
#define BUFFER_MIN_SIZE (PAGE_SIZE / 8)

static int binder_selftest_failures;

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
		page_index = (page_addr - alloc->vm_start) / PAGE_SIZE;
		if (!alloc->pages[page_index] ||
		    !list_empty(page_to_lru(alloc->pages[page_index]))) {
			pr_err("expect alloc but is %s at page index %d\n",
			       alloc->pages[page_index] ?
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
		if (list_empty(page_to_lru(alloc->pages[i]))) {
			pr_err_size_seq(sizes, seq);
			pr_err("expect lru but is %s at page index %d\n",
			       alloc->pages[i] ? "alloc" : "free", i);
			binder_selftest_failures++;
		}
	}
}

static void binder_selftest_free_page(struct binder_alloc *alloc)
{
	int i;
	unsigned long count;

	while ((count = list_lru_count(__binder_freelist_ptr))) {
		list_lru_walk(__binder_freelist_ptr, binder_alloc_free_page,
			      NULL, count);
	}

	for (i = 0; i < (alloc->buffer_size / PAGE_SIZE); i++) {
		if (alloc->pages[i]) {
			pr_err("expect free but is %s at page index %d\n",
			       list_empty(page_to_lru(alloc->pages[i])) ?
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
	// ynaffitodo: this is no longer true (if we are able to run at the same time as
	// other processes)
	if (list_lru_count(__binder_freelist_ptr))
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
void binder_alloc_test_alloc(struct binder_alloc *alloc)
{
	size_t end_offset[BUFFER_NUM];

	binder_selftest_alloc_offset(alloc, end_offset, 0);
}

static void binder_alloc_test_vma_close(struct vm_area_struct *vma)
{
	struct binder_alloc *alloc = vma->vm_private_data;
	binder_alloc_vma_close(alloc);
}

static const struct vm_operations_struct binder_alloc_test_vm_ops = {
	.close = binder_alloc_test_vma_close,
	.fault = binder_vm_fault,
};

static int binder_alloc_test_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct binder_alloc *alloc = filp->private_data;

	vm_flags_mod(vma, VM_DONTCOPY | VM_MIXEDMAP, VM_MAYWRITE);

	vma->vm_ops = &binder_alloc_test_vm_ops;
	vma->vm_private_data = alloc;

	return binder_alloc_mmap_handler(alloc, vma);
}

static const struct file_operations binder_alloc_test_fops = {
	.mmap = binder_alloc_test_mmap,
};

struct binder_alloc_test {
	struct binder_alloc alloc;
	struct list_lru binder_test_freelist;
	struct kunit_resource freelist_res;
	struct file *filp;
	unsigned long mmap_uaddr;
};

static void binder_alloc_add_test_basic(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1, 1);
}

static void binder_alloc_test_failure(struct kunit *test)
{
	KUNIT_FAIL(test, "This test never passes.");
}

static void binder_alloc_orig(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;

	binder_alloc_test_alloc(&priv->alloc);

	KUNIT_EXPECT_EQ_MSG(test, binder_selftest_failures, 0, "We shouldn't fail ever");
}

/* ===== End test cases ===== */

static int binder_alloc_test_init(struct kunit *test)
{
	struct binder_alloc_test *priv;
	int ret;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	test->priv = priv;

	/* binder_alloc_init requires mm to be attached */
	ret = kunit_attach_mm();
	if (ret) {
		kunit_err(test, "Failed to attach mm");
		return ret;
	}
	binder_alloc_init(&priv->alloc);

	priv->filp = anon_inode_getfile("binder_alloc_kunit",
					&binder_alloc_test_fops, &priv->alloc,
					O_RDWR | O_CLOEXEC);
	if (IS_ERR_OR_NULL(priv->filp)) {
		kunit_err(test, "Failed to open binder alloc test driver file");
		return priv->filp ? PTR_ERR(priv->filp) : -ENOMEM;
	}

	priv->mmap_uaddr = kunit_vm_mmap(test, priv->filp, 0, BINDER_MMAP_SIZE,
					 PROT_READ, MAP_PRIVATE | MAP_NORESERVE,
					 0);
	if (!priv->mmap_uaddr) {
		kunit_err(test, "Could not map the proc's transaction memory");
		return -ENOMEM;
	}
	KUNIT_ASSERT_NE_MSG(test, priv->mmap_uaddr, 0,
			    "Could not mmap the proc's transaction memory");

	ret = list_lru_init(&priv->binder_test_freelist);
	if (ret) {
		kunit_err(test, "Failed to initialize test freelist");
		return ret;
	}

	/*
	 * Associate a named kunit resource with the freelist we are using for
	 * testing so that it can be accessed inside of the binder driver code
	 * without importing other test-specific definitions.
	 */
	return kunit_add_named_resource(test, NULL, NULL, &priv->freelist_res,
					"binder_test_freelist",
					&priv->binder_test_freelist);
}

static void binder_alloc_test_exit(struct kunit *test)
{
	struct binder_alloc_test *priv = test->priv;


	// ynaffittodo: make sure to use the right free_list inside of release
	binder_alloc_deferred_release(&priv->alloc);
	if (!IS_ERR_OR_NULL(priv->filp))
		fput(priv->filp);
	kunit_remove_resource(test, &priv->freelist_res);

	list_lru_destroy(&priv->binder_test_freelist);
}

static struct kunit_case binder_alloc_test_cases[] = {
	KUNIT_CASE(binder_alloc_add_test_basic),
	KUNIT_CASE(binder_alloc_test_failure),
	KUNIT_CASE_SLOW(binder_alloc_orig),
	{}
};

static struct kunit_suite binder_alloc_test_suite = {
	.name = "binder_alloc",
	.test_cases = binder_alloc_test_cases,
	.init = binder_alloc_test_init,
	.exit = binder_alloc_test_exit,
};
kunit_test_suite(binder_alloc_test_suite);

MODULE_DESCRIPTION("Binder Alloc KUnit tests");
MODULE_LICENSE("GPL v2");
