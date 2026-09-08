// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/sched.h>

#include <media/frame_vector.h>

/**
 * get_vaddr_frames_range() - pin a byte range into a frame vector
 * @start: first user byte
 * @length: requested byte length
 * @write: request write access
 * @vec: output, with capacity for mm_user_range_pages() entries
 *
 * Returns the number of pinned entries, possibly short, or -errno. The
 * vector owns those pins until put_vaddr_frames(). Frame offsets and lengths
 * describe the exact byte range, not allocation padding.
 */
int get_vaddr_frames_range(unsigned long start, size_t length, bool write,
			   struct frame_vector *vec)
{
	struct mm_struct *mm = current->mm;
	unsigned int gup_flags = FOLL_LONGTERM;
	int ret;

	if (write)
		gup_flags |= FOLL_WRITE;
	frame_vector_set_frame_size(vec, PAGE_SIZE);
	ret = pin_user_pages_range(mm, start, length, vec->nr_allocated,
				   gup_flags, (struct page **)vec->ptrs,
				   frame_vector_spans(vec));
	vec->got_ref = ret > 0;
	vec->is_pfns = false;
	vec->nr_frames = ret > 0 ? ret : 0;
	return ret;
}
EXPORT_SYMBOL(get_vaddr_frames_range);

/* Legacy frame-count interface: cover through the last requested page. */
int get_vaddr_frames(unsigned long start, unsigned int nr_frames, bool write,
		     struct frame_vector *vec)
{
	size_t length;
	int ret;

	if (!nr_frames)
		return 0;
	if (WARN_ON_ONCE(nr_frames > vec->nr_allocated))
		nr_frames = vec->nr_allocated;
	if (!nr_frames)
		return -EFAULT;
	if (nr_frames > (ULONG_MAX >> PAGE_SHIFT))
		return -EOVERFLOW;
	length = ((size_t)nr_frames << PAGE_SHIFT) -
		 offset_in_page(start);
	ret = get_vaddr_frames_range(start, length, write, vec);
	return ret ? ret : -EFAULT;
}
EXPORT_SYMBOL(get_vaddr_frames);

/**
 * put_vaddr_frames() - drop references to pages if get_vaddr_frames() acquired
 *			them
 * @vec:	frame vector to put
 *
 * Drop references to pages if get_vaddr_frames() acquired them. We also
 * invalidate the frame vector so that it is prepared for the next call into
 * get_vaddr_frames().
 */
void put_vaddr_frames(struct frame_vector *vec)
{
	struct page **pages;

	if (!vec->got_ref)
		goto out;
	pages = frame_vector_pages(vec);
	/*
	 * frame_vector_pages() might needed to do a conversion when
	 * get_vaddr_frames() got pages but vec was later converted to pfns.
	 * But it shouldn't really fail to convert pfns back...
	 */
	if (WARN_ON(IS_ERR(pages)))
		goto out;

	unpin_user_pages(pages, vec->nr_frames);
	vec->got_ref = false;
out:
	vec->nr_frames = 0;
}
EXPORT_SYMBOL(put_vaddr_frames);

/**
 * frame_vector_to_pages - convert frame vector to contain page pointers
 * @vec:	frame vector to convert
 *
 * Convert @vec to contain array of page pointers.  If the conversion is
 * successful, return 0. Otherwise return an error. Note that we do not grab
 * page references for the page structures.
 */
int frame_vector_to_pages(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (!vec->is_pfns)
		return 0;
	nums = frame_vector_pfns(vec);
	for (i = 0; i < vec->nr_frames; i++)
		if (!pfn_valid(nums[i]))
			return -EINVAL;
	pages = (struct page **)nums;
	for (i = 0; i < vec->nr_frames; i++)
		pages[i] = pfn_to_page(nums[i]);
	vec->is_pfns = false;
	return 0;
}
EXPORT_SYMBOL(frame_vector_to_pages);

/**
 * frame_vector_to_pfns - convert frame vector to contain pfns
 * @vec:	frame vector to convert
 *
 * Convert @vec to contain array of pfns.
 */
void frame_vector_to_pfns(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (vec->is_pfns)
		return;
	pages = (struct page **)(vec->ptrs);
	nums = (unsigned long *)pages;
	for (i = 0; i < vec->nr_frames; i++)
		nums[i] = page_to_pfn(pages[i]);
	vec->is_pfns = true;
}
EXPORT_SYMBOL(frame_vector_to_pfns);

/**
 * frame_vector_create() - allocate & initialize structure for pinned pfns
 * @nr_frames:	number of pfns slots we should reserve
 *
 * Allocate and initialize struct pinned_pfns to be able to hold @nr_pfns
 * pfns.
 */
struct frame_vector *frame_vector_create(unsigned int nr_frames)
{
	struct frame_vector *vec;
	size_t ptrs_size = struct_size(vec, ptrs, nr_frames);
	size_t spans_size;
	size_t size;

	if (WARN_ON_ONCE(nr_frames == 0))
		return NULL;
	/*
	 * This is absurdly high. It's here just to avoid strange effects when
	 * arithmetics overflows.
	 */
	if (WARN_ON_ONCE(nr_frames > INT_MAX / sizeof(void *) / 2))
		return NULL;
	if (check_mul_overflow((size_t)nr_frames, sizeof(struct page_span),
			       &spans_size) ||
	    check_add_overflow(spans_size, sizeof(struct frame_vector_tail),
			       &spans_size) ||
	    check_add_overflow(ptrs_size, spans_size, &size))
		return NULL;
	/*
	 * Avoid higher order allocations, use vmalloc instead. It should
	 * be rare anyway.
	 */
	vec = kvmalloc(size, GFP_KERNEL);
	if (!vec)
		return NULL;
	vec->nr_allocated = nr_frames;
	vec->nr_frames = 0;
	frame_vector_set_frame_size(vec, PAGE_SIZE);
	return vec;
}
EXPORT_SYMBOL(frame_vector_create);

/**
 * frame_vector_destroy() - free memory allocated to carry frame vector
 * @vec:	Frame vector to free
 *
 * Free structure allocated by frame_vector_create() to carry frames.
 */
void frame_vector_destroy(struct frame_vector *vec)
{
	/* Make sure put_vaddr_frames() got called properly... */
	VM_BUG_ON(vec->nr_frames > 0);
	kvfree(vec);
}
EXPORT_SYMBOL(frame_vector_destroy);
