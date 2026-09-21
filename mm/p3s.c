// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/mm_types.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/p3s/mm.h>
#include <linux/p3s/vma.h>
#include "vma.h"

enum p3s_mode p3s_mode __ro_after_init = P3S_MODE_OFF;
EXPORT_SYMBOL(p3s_mode);

static int __init parse_p3s(char *str)
{
	unsigned int val;
	bool enabled;

	if (!str) {
		p3s_mode = P3S_MODE_ON;
		return 0;
	}

	if (!kstrtouint(str, 0, &val)) {
		if (val <= P3S_MODE_ALTERNATE) {
			p3s_mode = val;
			return 0;
		}
		return -EINVAL;
	}

	if (kstrtobool(str, &enabled))
		return -EINVAL;

	p3s_mode = enabled ? P3S_MODE_ON : P3S_MODE_OFF;
	return 0;
}
early_param("p3s", parse_p3s);

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
						     vmg_is_compat(vmg),
						     !!vmg->file);

		if (!vmg_is_compat(vmg))
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
				     vmg->start, vmg_is_compat(vmg), !!vmg->file);
}
