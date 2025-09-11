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
void xe_gt_sriov_pf_sanitize_hw(struct xe_gt *gt, unsigned int vfid);
<<<<<<< HEAD   (9ddeb7663df3e9b001fd17e2e0993ff8e749b10d ANDROID: GKI: Update symbol list for Amlogic am: 6879524e1c)
||||||| BASE   (6879524e1c5adf156b4cf196ed96b6aa21e16b2f ANDROID: GKI: Update symbol list for Amlogic)
=======
void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt);
>>>>>>> BRANCH (2e5a4bace74a835f6a733c1a628cbd76501f2d62 Merge tag 'android16-6.12.40_r00' into android16-6.12)
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
{
}

static inline void xe_gt_sriov_pf_restart(struct xe_gt *gt)
{
}
#endif

#endif
