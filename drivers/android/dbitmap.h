#ifndef _LINUX_DBITMAP_H
#define _LINUX_DBITMAP_H

#include <linux/bitmap.h>

struct dbitmap {
	unsigned nbits;
	unsigned long *map;
};

static inline void
dbitmap_free(struct dbitmap *dbitmap)
{
	dbitmap->nbits = 0;
	kfree(dbitmap->map);
	dbitmap->map = NULL;
}

static inline void
dbitmap_expand(struct dbitmap *dbitmap, unsigned long *new, unsigned nbits)
{
	if (unlikely(!dbitmap->map || nbits <= dbitmap->nbits)) {
		/* raced with another expand or disable */
		kfree(new);
		return;
	}

	if (!new) {
		/* ENOMEM disable bitmap */
		dbitmap_free(dbitmap);
		return;
	}

	bitmap_copy(new, dbitmap->map, dbitmap->nbits);
	kfree(dbitmap->map);
	dbitmap->map = new;
	dbitmap->nbits = nbits;
}

static inline unsigned long
dbitmap_get_first_zero_bit(struct dbitmap *dbitmap)
{
	unsigned long bit;

	bit = find_first_zero_bit(dbitmap->map, dbitmap->nbits);
	if (bit < dbitmap->nbits)
		set_bit(bit, dbitmap->map);

	return bit;
}

static inline int
dbitmap_init(struct dbitmap *dbitmap, unsigned long nbits)
{
	dbitmap->map = bitmap_zalloc(nbits, GFP_KERNEL);
	if (!dbitmap->map)
		return -ENOMEM;

	dbitmap->nbits = nbits;
	/* 0 is reserved for context manager */
	set_bit(0, dbitmap->map);

	return 0;
}

#endif
