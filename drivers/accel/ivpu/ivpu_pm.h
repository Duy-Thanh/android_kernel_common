/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_PM_H__
#define __IVPU_PM_H__

#include <linux/rwsem.h>
#include <linux/types.h>

struct ivpu_device;

struct ivpu_pm_info {
	struct ivpu_device *vdev;
	struct delayed_work job_timeout_work;
	struct work_struct recovery_work;
	struct rw_semaphore reset_lock;
	atomic_t reset_counter;
	atomic_t reset_pending;
	bool is_warmboot;
	u8 dct_active_percent;
};

void ivpu_pm_init(struct ivpu_device *vdev);
void ivpu_pm_enable(struct ivpu_device *vdev);
void ivpu_pm_disable(struct ivpu_device *vdev);
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
void ivpu_pm_disable_recovery(struct ivpu_device *vdev);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void ivpu_pm_cancel_recovery(struct ivpu_device *vdev);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

int ivpu_pm_suspend_cb(struct device *dev);
int ivpu_pm_resume_cb(struct device *dev);
int ivpu_pm_runtime_suspend_cb(struct device *dev);
int ivpu_pm_runtime_resume_cb(struct device *dev);

void ivpu_pm_reset_prepare_cb(struct pci_dev *pdev);
void ivpu_pm_reset_done_cb(struct pci_dev *pdev);

int __must_check ivpu_rpm_get(struct ivpu_device *vdev);
void ivpu_rpm_put(struct ivpu_device *vdev);

void ivpu_pm_trigger_recovery(struct ivpu_device *vdev, const char *reason);
void ivpu_start_job_timeout_detection(struct ivpu_device *vdev);
void ivpu_stop_job_timeout_detection(struct ivpu_device *vdev);

int ivpu_pm_dct_init(struct ivpu_device *vdev);
int ivpu_pm_dct_enable(struct ivpu_device *vdev, u8 active_percent);
int ivpu_pm_dct_disable(struct ivpu_device *vdev);
void ivpu_pm_dct_irq_thread_handler(struct ivpu_device *vdev);

#endif /* __IVPU_PM_H__ */
