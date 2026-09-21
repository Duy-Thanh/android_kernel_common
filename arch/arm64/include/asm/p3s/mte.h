/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_P3S_MTE_H
#define _ASM_ARM64_P3S_MTE_H

#include <linux/p3s/vma.h>
#include <linux/coredump.h>
#include <asm/mte-def.h>

static inline unsigned long mte_tag_dump_offset(unsigned long page_offset)
{
	return page_offset * MTE_TAG_SIZE /
		(MTE_GRANULE_SIZE * BITS_PER_BYTE);
}

static inline void mte_remote_tags_chunk(const struct vm_area_struct *vma,
					 unsigned long addr, size_t len,
					 unsigned long *offset, unsigned long *tags)
{
	unsigned long page_offset = vma_offset_in_page(vma, addr);

	*offset = vma_folio_offset(vma, addr);
	*tags = min(len, (vma_page_size(vma) - page_offset) / MTE_GRANULE_SIZE);
}

#endif /* _ASM_ARM64_P3S_MTE_H */
