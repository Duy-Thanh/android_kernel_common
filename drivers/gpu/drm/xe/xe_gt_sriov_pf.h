/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_H_
#define _XE_GT_SRIOV_PF_H_

struct xe_gt;

#ifdef CONFIG_PCI_IOV
int xe_gt_sriov_pf_init_early(struct xe_gt *gt);
void xe_gt_sriov_pf_init_hw(struct xe_gt *gt);
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
void xe_gt_sriov_pf_sanitize_hw(struct xe_gt *gt, unsigned int vfid);
void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt);
void xe_gt_sriov_pf_restart(struct xe_gt *gt);
#else
static inline int xe_gt_sriov_pf_init_early(struct xe_gt *gt)
{
	return 0;
}

static inline void xe_gt_sriov_pf_init_hw(struct xe_gt *gt)
{
}

static inline void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void xe_gt_sriov_pf_restart(struct xe_gt *gt);
#else
static inline int xe_gt_sriov_pf_init_early(struct xe_gt *gt)
{
	return 0;
}

static inline void xe_gt_sriov_pf_init_hw(struct xe_gt *gt)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
}

static inline void xe_gt_sriov_pf_restart(struct xe_gt *gt)
{
}
#endif

#endif
