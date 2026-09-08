// SPDX-License-Identifier: GPL-2.0
#ifndef _MEDIA_FRAME_VECTOR_H
#define _MEDIA_FRAME_VECTOR_H

#include <linux/mm_types.h>

/* Container for pinned pfns / pages in frame_vector.c */
struct frame_vector {
	unsigned int nr_allocated;	/* Number of frames we have space for */
	unsigned int nr_frames;	/* Number of frames stored in ptrs array */
	bool got_ref;		/* Did we pin pages by getting page ref? */
	bool is_pfns;		/* Does array contain pages or pfns? */
	void *ptrs[];		/* Array of pinned pfns / pages. Use
				 * pfns_vector_pages() or pfns_vector_pfns()
				 * for access */
};

/* Private metadata allocated after the ABI-visible ptrs[] array. */
struct frame_vector_tail {
	unsigned int frame_size;
	struct page_span spans[];
};

struct frame_vector *frame_vector_create(unsigned int nr_frames);
void frame_vector_destroy(struct frame_vector *vec);
int get_vaddr_frames(unsigned long start, unsigned int nr_pfns,
		     bool write, struct frame_vector *vec);
int get_vaddr_frames_range(unsigned long start, size_t length, bool write,
			   struct frame_vector *vec);
void put_vaddr_frames(struct frame_vector *vec);
int frame_vector_to_pages(struct frame_vector *vec);
void frame_vector_to_pfns(struct frame_vector *vec);

static inline unsigned int frame_vector_count(struct frame_vector *vec)
{
	return vec->nr_frames;
}

static inline unsigned int frame_vector_frame_size(struct frame_vector *vec)
{
	struct frame_vector_tail *tail =
		(struct frame_vector_tail *)&vec->ptrs[vec->nr_allocated];

	return tail->frame_size;
}

static inline void frame_vector_set_frame_size(struct frame_vector *vec,
					unsigned int frame_size)
{
	struct frame_vector_tail *tail =
		(struct frame_vector_tail *)&vec->ptrs[vec->nr_allocated];

	tail->frame_size = frame_size;
}

static inline struct page_span *frame_vector_spans(struct frame_vector *vec)
{
	struct frame_vector_tail *tail =
		(struct frame_vector_tail *)&vec->ptrs[vec->nr_allocated];

	return tail->spans;
}

static inline unsigned int
frame_vector_frame_offset(struct frame_vector *vec, unsigned int frame)
{
	return frame_vector_spans(vec)[frame].offset;
}

static inline struct page **frame_vector_pages(struct frame_vector *vec)
{
	if (vec->is_pfns) {
		int err = frame_vector_to_pages(vec);

		if (err)
			return ERR_PTR(err);
	}
	return (struct page **)(vec->ptrs);
}

static inline unsigned long *frame_vector_pfns(struct frame_vector *vec)
{
	if (!vec->is_pfns)
		frame_vector_to_pfns(vec);
	return (unsigned long *)(vec->ptrs);
}

#endif /* _MEDIA_FRAME_VECTOR_H */
