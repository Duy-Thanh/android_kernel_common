#ifndef _LINUX_DMA_PREFAULT_H
#define _LINUX_DMA_PREFAULT_H

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/compiler.h>

static inline void dma_prefault_page_range(struct page *page,
					   size_t offset, size_t size)
{

	size_t start = offset >> PAGE_SHIFT;
	size_t end   = (offset + size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	size_t i;

	for (i = start; i < end; i++) {
		struct page *p = page + i;

		void *vaddr = kmap_local_page(p);

        READ_ONCE(*(char *)vaddr);

        kunmap_local(vaddr);
	}
}

static inline void dma_prefault_range(const void *addr, size_t size)
{
	unsigned long start = ALIGN_DOWN((unsigned long)addr, PAGE_SIZE);
	unsigned long end   = ALIGN_DOWN((unsigned long)addr + size - 1, PAGE_SIZE);

	for (unsigned long a = start; a <= end; a += PAGE_SIZE) {
		if (is_vmalloc_addr((void *)a)) {
			unsigned long vaddr = a;

			READ_ONCE(*(volatile char *)vaddr);

		} else if (virt_addr_valid((void *)a)) {
			unsigned long vaddr = a;

			READ_ONCE(*(char *)vaddr);
		} else {
			continue;
		}
	}
}


static inline void dma_prefault_sgt(struct sg_table *sgt)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		struct page *p = sg_page(sg);
		size_t off = sg->offset;
		size_t len = sg->length;

		while (len) {
			size_t off_in_page = off & (PAGE_SIZE - 1);
			size_t chunk = min_t(size_t, len,
						PAGE_SIZE - off_in_page);

			void *vaddr = kmap_local_page(p);

			dma_prefault_range(vaddr + off_in_page, chunk);

			kunmap_local(vaddr);

			off += chunk;
			len -= chunk;

			if (!(off & (PAGE_SIZE - 1)))
				p = nth_page(p, 1);
		}
	}
}

#endif /* _LINUX_DMA_PREFAULT_H */

