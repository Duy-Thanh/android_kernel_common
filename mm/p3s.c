// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/mm_types.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/p3s/mm.h>
#include <linux/p3s/vma.h>
#include "vma.h"

enum p3s_4k_mode p3s_4k_mode __ro_after_init = P3S_4K_MODE_OFF;
EXPORT_SYMBOL(p3s_4k_mode);

static int __init parse_p3s_4k(char *str)
{
	bool enabled;

	if (!str)
		return -EINVAL;

	if (!kstrtobool(str, &enabled)) {
		p3s_4k_mode = enabled ? P3S_4K_MODE_ON : P3S_4K_MODE_OFF;
		return 0;
	}

	if (!strcmp(str, "alternate")) {
		p3s_4k_mode = P3S_4K_MODE_ALTERNATE;
		return 0;
	}

	return -EINVAL;
}
early_param("p3s_4k", parse_p3s_4k);

/*
 * vmg_new_range_slice - Slice at vmg->start of a proposed new range
 * @vmg: VMA merge structure without a middle VMA
 * @neighbour: The VMA to merge with, mapping the same file
 *
 * Private /dev/zero mappings keep vm_file without vm_ops and, like
 * anonymous memory, take their slice from the virtual address.
 */
static unsigned int vmg_new_range_slice(struct vma_merge_struct *vmg,
					struct vm_area_struct *neighbour)
{
	if (!neighbour->vm_ops)
		return (vmg->start >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;
	return vmg->slice_off;
}

/*
 * vmg_can_merge_offsets - Verify offset & slice continuity for VMA merge
 * @vmg: VMA merge structure
 * @merge_next: True if merging with vmg->next, false if merging with vmg->prev
 */
bool vmg_can_merge_offsets(struct vma_merge_struct *vmg, bool merge_next)
{
	if (merge_next) {
		unsigned int start_slice;
		unsigned long total_slices;

		if (vmg->middle)
			return vma_can_merge_offsets(vmg->middle,
						     vmg->next->vm_pgoff,
						     vma_slice_off(vmg->next),
						     vmg->end,
						     vmg_is_p3s_4k(vmg),
						     !!vmg->file);

		if (!vmg_is_p3s_4k(vmg))
			return vmg->next->vm_pgoff ==
			       vmg->pgoff + PHYS_PFN(vmg->end - vmg->start);

		if (!vmg->file)
			return vmg->next->vm_pgoff ==
			       vmg->pgoff + ((vmg->end - vmg->start) >> PAGE_SHIFT_4KB);

		start_slice = vmg_new_range_slice(vmg, vmg->next);
		total_slices = start_slice + ((vmg->end - vmg->start) >> PAGE_SHIFT_4KB);

		return vmg->next->vm_pgoff == vmg->pgoff + (total_slices >> P3S_SLICE_SHIFT) &&
		       vma_slice_offset(vmg->next, vmg->end) ==
		       (total_slices & P3S_SLICE_MASK);
	}

	if (!vmg->prev)
		return false;

	return vma_can_merge_offsets(vmg->prev, vmg->pgoff,
				     vmg->middle ? vma_slice_off(vmg->middle) :
				     vmg_new_range_slice(vmg, vmg->prev),
				     vmg->start, vmg_is_p3s_4k(vmg), !!vmg->file);
}

/*
 * p3s_remap_phys_range - Map physical memory into a 4KB compat VMA
 * @vma: 4KB compat VMA
 * @addr: First user address to map, 4KB aligned
 * @phys: Physical address to map at @addr, 4KB aligned
 * @size: Number of bytes to map, a multiple of 4KB
 * @prot: Page protection
 *
 * Each 4KB user page maps the next 4KB of physical memory, a slice of a
 * native page frame.  On failure, the pages mapped so far are zapped.
 */
static int p3s_remap_phys_range(struct vm_area_struct *vma, unsigned long addr,
				phys_addr_t phys, unsigned long size,
				pgprot_t prot)
{
	unsigned long off;
	int err = 0;

	for (off = 0; off < size; off += PAGE_SIZE_4KB) {
		phys_addr_t pa = phys + off;

		err = remap_pfn_range_slice(vma, addr + off,
					    pa >> PAGE_SHIFT_KERNEL,
					    (pa >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK,
					    PAGE_SIZE_4KB, prot);
		if (err)
			break;
	}
	if (err && off)
		zap_page_range_single(vma, addr, off, NULL);
	return err;
}

/*
 * p3s_remap_pfn_range - remap_pfn_range() for a 4KB compat VMA
 * @vma: 4KB compat VMA
 * @addr: First user address to map
 * @pfn: Native page frame mapped at @addr
 * @size: Number of bytes to map
 * @prot: Page protection
 *
 * Drivers compute @pfn and @size in native pages, typically from vm_pgoff,
 * so the range starts at a native page boundary and may extend past vm_end
 * of a VMA that is not a whole number of native pages.  Map it 4KB at a
 * time and stop at vm_end.  A file offset that is not native-page aligned
 * cannot be expressed in native pages, so such a VMA is rejected.
 */
int p3s_remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
			unsigned long pfn, unsigned long size, pgprot_t prot)
{
	unsigned long end;

	if (!IS_ALIGNED(addr, PAGE_SIZE_4KB) || addr < vma->vm_start ||
	    addr >= vma->vm_end || !size || vma_slice_off(vma))
		return -EINVAL;
	size = ALIGN(size, PAGE_SIZE_4KB);
	end = (!size || size > vma->vm_end - addr) ? vma->vm_end : addr + size;

	if (is_cow_mapping(vma->vm_flags)) {
		if (addr != vma->vm_start || end != vma->vm_end)
			return -EINVAL;
		vma->vm_pgoff = pfn;
	}

	return p3s_remap_phys_range(vma, addr,
				    (phys_addr_t)pfn << PAGE_SHIFT_KERNEL,
				    end - addr, prot);
}

/*
 * p3s_vm_iomap_memory - vm_iomap_memory() for a 4KB compat VMA
 * @vma: 4KB compat VMA
 * @start: Physical start of the memory area
 * @len: Size of the memory area in bytes
 *
 * As in the native version, an unaligned @start maps from the page that
 * contains it, here a 4KB page.
 */
int p3s_vm_iomap_memory(struct vm_area_struct *vma, phys_addr_t start,
			unsigned long len)
{
	unsigned long vm_len = vma->vm_end - vma->vm_start;
	u64 offset = vma_file_offset(vma);

	if (start + len < start)
		return -EINVAL;
	len += start & ~PAGE_MASK_4KB;
	start &= PAGE_MASK_4KB;
	if (len > ULONG_MAX - (PAGE_SIZE_4KB - 1))
		return -EINVAL;
	len = ALIGN(len, PAGE_SIZE_4KB);
	if (offset > len || vm_len > len - offset)
		return -EINVAL;
	return p3s_remap_phys_range(vma, vma->vm_start, start + offset, vm_len,
				    pgprot_decrypted(vma->vm_page_prot));
}

/*
 * p3s_vm_map_pages - vm_map_pages() for a 4KB compat VMA
 * @vma: 4KB compat VMA
 * @pages: Native pages of the object
 * @num: Number of pages in @pages
 * @offset: Byte offset in the object to map at vm_start
 *
 * Each 4KB user page maps the matching 4KB slice of the object, as each
 * native user page maps the matching object page.
 */
int p3s_vm_map_pages(struct vm_area_struct *vma, struct page **pages,
		     unsigned long num, u64 offset)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	u64 size = (u64)num << PAGE_SHIFT_KERNEL;
	unsigned long off;
	int err;

	if (offset >= size || len > size - offset)
		return -ENXIO;
	for (off = 0; off < len; off += PAGE_SIZE_4KB) {
		u64 pos = offset + off;

		err = vm_insert_page_slice(vma, vma->vm_start + off,
					   pages[pos >> PAGE_SHIFT_KERNEL],
					   (pos >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK);
		if (err)
			return err;
	}
	return 0;
}

/*
 * p3s_mmap_phys_object - Map a 4KB compat VMA onto a contiguous object
 * @vma: 4KB compat VMA, whose file offset selects the start in the object
 * @phys: Physical start of the object, 4KB aligned
 * @size: Size of the object in bytes
 *
 * Returns -ENXIO if the VMA's file range does not fit in the object.
 */
int p3s_mmap_phys_object(struct vm_area_struct *vma, phys_addr_t phys,
			 size_t size)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	u64 offset = vma_file_offset(vma);

	size = ALIGN(size, PAGE_SIZE_4KB);
	if (offset >= size || len > size - offset)
		return -ENXIO;
	return p3s_remap_phys_range(vma, vma->vm_start, phys + offset, len,
				    vma->vm_page_prot);
}
