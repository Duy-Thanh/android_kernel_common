/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_USER_H_
#define _XE_USER_H_

#include "xe_device.h"


#define XE_WORK_PERIOD_INTERVAL 500

/**
 * struct xe_user - xe user structure
 *
 * This is a per UID structure for tracking an xe device client. It is
 * allocated when a new process/app opens the xe device and destroyed
 * when the last xe file belonging to this UID is destroyed.
 */
struct xe_user {
	/**
	 * @refcount: reference count
	 */
	struct kref refcount;

	/**
	 * @xe: pointer to the xe_device
	 */
	struct xe_device *xe;

	/**
	 * @filelist_lock: lock protecting this structure
	 */
	struct mutex lock;

	/**
	 * @filelist: list of xe files belonging to this xe user
	 */
	struct list_head filelist;

	/**
	 * @work: work to emit the gpu work period event for this
	 * xe user
	 */
	struct delayed_work delay_work;

	/**
	 * @id: index of this user into the xe device::users xarray
	 */
	u32 id;

	/**
	 * @uid: UID of this xe_user
	 */
	u32 uid;

	/**
	 * @active_duration_ns: sum total of xe_file.active_duration_ns
	 * for all xe files belonging to this xe user
	 */
	u64 active_duration_ns;

	/**
	 * @last_timestamp_ns: timestamp in ns when we last emitted event
	 * for this xe user
	 */
	u64 last_timestamp_ns;
};

#if IS_ENABLED(CONFIG_TRACE_GPU_WORK_PERIOD)

int xe_user_init(struct xe_device *xe, struct xe_file *xef, unsigned int uid);

void xe_user_fini(struct xe_device *xe);

void xe_user_cancel_workers(struct xe_device *xe);

void xe_user_resume_workers(struct xe_device *xe);

void __xe_user_free(struct kref *kref);

#else

static inline
int xe_user_init(struct xe_device *xe, struct xe_file *xef, unsigned int uid)
{
	return 0;
}

static inline void xe_user_fini(struct xe_device *xe)
{
}

static inline void __xe_user_free(struct kref *kref)
{
}

static inline void xe_user_cancel_workers(struct xe_device *xe)
{
}

static inline void xe_user_resume_workers(struct xe_device *xe)
{
}

#endif // CONFIG_TRACE_GPU_WORK_PERIOD

static inline struct xe_user *
xe_user_get_unless_zero(struct xe_user *user)
{
	if (kref_get_unless_zero(&user->refcount))
		return user;
	return NULL;
}

static inline struct xe_user *
xe_user_get(struct xe_user *user)
{
	kref_get(&user->refcount);
	return user;
}


static inline void xe_user_put(struct xe_user *user)
{
	kref_put(&user->refcount, __xe_user_free);
}

#endif // _XE_USER_H_

