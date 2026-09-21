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

		start_slice = (vmg->start >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;
		total_slices = start_slice + ((vmg->end - vmg->start) >> PAGE_SHIFT_4KB);

		return vmg->next->vm_pgoff == vmg->pgoff + (total_slices >> P3S_SLICE_SHIFT) &&
		       vma_slice_off(vmg->next) == ((vmg->end >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK);
	}

	if (!vmg->prev)
		return false;

	return vma_can_merge_offsets(vmg->prev, vmg->pgoff,
				     vmg->middle ? vma_slice_off(vmg->middle) :
				     vma_slice_offset(vmg->prev, vmg->start),
				     vmg->start, vmg_is_p3s_4k(vmg), !!vmg->file);
}
