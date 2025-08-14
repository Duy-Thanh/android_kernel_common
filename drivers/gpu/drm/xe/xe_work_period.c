// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "linux/workqueue_types.h"
#include <drm/drm_drv.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#include "xe_assert.h"
#include "xe_device_types.h"
#include "xe_exec_queue.h"
#include "xe_pm.h"
#include "xe_work_period.h"

#define CREATE_TRACE_POINTS
#include "xe_gpu_work_period_trace.h"


/**
 * DOC: Xe User
 *
 * Xe User adds support for handling UID (i.e. persistent, unique ID of the
 * Android app) based requirements for Android platforms.
 *
 * For Android GPU work period event we need to track the runtime on the GPU
 * for each UID. This means we can have multiple xe files opened by different
 * processes/threads that belongs to the same UID. All these xe files need to
 * be grouped together so that one can easily identify them while calculating
 * the run time for the given UID.
 *
 * Currently, the xe driver doesn't record the user id of the calling process.
 * Also, all the xe files created using open call are clubbed together inside
 * the xe device structure with no way to distinguish between them based on
 * the UID of the calling process.
 *
 * To remedy these limitations we are adding another layer of indirection
 * between the xe device and the xe file. xe device will now also have a list
 * of xe users each with a given UID, and each xe user will have a list of xe
 * files that are created by a process that belongs to this UID.
 *
 * The lifetime of a xe user structure should be between when a process with
 * a new UID has first opened the xe device, and when the last xe file
 * belonging to this UID is closed.
 *
 * In order to implement this we maintain an xarray of xe user structures
 * inside our xe device instance. Whenever a new xe file is created via an
 * open call, we check if the calling process' UID is already present in our
 * xarray. If so, we increment the refcount for the associated xe user and add
 * our newly created xe file to the list of xe files belonging to this xe user.
 * Otherwise, we allocate a new xe user structure for this UID and initialize
 * its file list with our newly create xe file.
 *
 * Whenever an xe file is being destroyed, we decrement the refcount of the
 * associated xe user. When the last xe file in the xe user's file list is
 * destroyed, the xe user refcount should drop to zero and the xe user should
 * be cleaned up. During the cleanup path we remove the xarray entry in our xe
 * device for this xe user and free up its memory.
 */

static void xe_work_period_worker(struct work_struct *work);
static void xe_user_free(struct kref *kref);

static void xe_user_get(struct xe_user *user)
{
	struct xe_device *xe = user->xe;
	lockdep_assert_held(&xe->work_period.lock);
	kref_get(&user->refcount);
}

static bool xe_user_get_unless_zero(struct xe_user *user)
{
	return kref_get_unless_zero(&user->refcount);
}

/* xe_user_put() - drop reference to xe_user
 * @xe_user: The user struct
 *
 * Context: Must not be holding xe->work_period.lock.
 * Return: true if the object was removed (refcount reached 0)
 */
static bool xe_user_put(struct xe_user *user)
{
	struct xe_device *xe = user->xe;
	if (kref_put_mutex(&user->refcount, xe_user_free, &xe->work_period.lock)) {
		mutex_unlock(&xe->work_period.lock);
		return true;
	}

	return false;
}

static void xe_user_init(struct xe_device *xe, struct xe_user *user,
			 unsigned int uid)
{
	user->uid = uid;
	user->last_timestamp_ns = ktime_get_raw_ns();
	user->xe = xe;

	kref_init(&user->refcount);
	mutex_init(&user->lock);
	INIT_LIST_HEAD(&user->filelist);
	INIT_DELAYED_WORK(&user->delay_work, xe_work_period_worker);

	drm_dev_get(&xe->drm);
}

static void xe_user_fini(struct xe_user *user)
{
	drm_dev_put(&user->xe->drm);
	mutex_destroy(&user->lock);
}

/**
 * xe_user_alloc() - Allocate xe user
 *
 * Allocate xe user struct to track activity on the gpu
 * by the application. Call this API whenever a new app
 * has opened xe device.
 *
 * Return: pointer to user struct or NULL if can't allocate
 */
static struct xe_user *xe_user_alloc(struct xe_file *xef, unsigned int uid)
{
	struct xe_device *xe = xef->xe;
	struct xe_user *user;
	int ret;

	user = kzalloc(sizeof(*user), GFP_KERNEL);
	if (!user)
		return NULL;
	xe_user_init(xe, user, uid);

	/* must hold lock to prevent lookup from finding the user and dropping
	 * its ref before we obtain our own ref.
	 */
	lockdep_assert_held(&xe->work_period.lock);
	ret = xa_alloc(&xe->work_period.users, &user->id, user, xa_limit_32b,
		       GFP_KERNEL);
	if (ret < 0) {
		xe_user_fini(user);
		kfree(user);
		return NULL;
	}

	return user;
}

static void xe_user_free(struct kref *kref)
{
	struct xe_user *user =
		container_of(kref, struct xe_user, refcount);
	struct xe_device *xe = user->xe;

	/* must take lock to prevent lookup from finding the user while it is
	 * being cleaned up.
	 */
	lockdep_assert_held(&xe->work_period.lock);
	xa_erase(&xe->work_period.users, user->id);

	xe_user_fini(user);
	kfree(user);
}

static inline void schedule_next_work(struct xe_user *user)
{
	struct xe_device *xe = user->xe;

	/* If user wasn't dropped yet, try to take a ref to keep it alive.
	 *
	 * If acquired, try to schedule another work, otherwise drop our new ref
	 * because prior-scheduled worker already has one.
	 *
	 * must not hold the lock, since xe_user_put() may attempt to take it.
	 */
	lockdep_assert_not_held(&xe->work_period.lock);
	if (xe_user_get_unless_zero(user)) {
		if (!schedule_delayed_work(&user->delay_work,
				msecs_to_jiffies(XE_WORK_PERIOD_INTERVAL)))
			xe_user_put(user);
	}
}

static void xe_work_period_worker(struct work_struct *work)
{
	struct xe_user *user = container_of(work, struct xe_user, delay_work.work);
	struct xe_device *xe = user->xe;
	struct xe_exec_queue *q;
	struct xe_file *xef;

	/*
	 * The GPU work period event requires the following parameters
	 *
	 * gpuid:           GPU index in case the platform has more than one GPU
	 * uid:             user id of the app
	 * start_time:      start time for the sampling period in nanosecs
	 * end_time:        end time for the sampling period in nanosecs
	 * active_duration: Total runtime in nanosecs for this uid in
	 *                  the current sampling period.
	 */
	u64 active_duration, last_active_duration, period_duration;
	u32 gpuid = 0, uid = user->uid;
	u64 start_time, end_time;
	unsigned long i;

	if (xe_pm_runtime_get_if_active(xe)) {
		mutex_lock(&user->lock);

		last_active_duration = user->active_duration_ns;

		/* TODO: robustly prevent double-counting for simultaneously
		 * executed workloads submitted by different xe_files or same
		 * xe_file on different exec_queues.
		 */
		list_for_each_entry(xef, &user->filelist, user_link) {
			/* Accumulate all the exec queues from this file */
			mutex_lock(&xef->exec_queue.lock);
			xa_for_each(&xef->exec_queue.xa, i, q) {
				xe_exec_queue_get(q);
				mutex_unlock(&xef->exec_queue.lock);

				xe_exec_queue_update_run_ticks(q);

				mutex_lock(&xef->exec_queue.lock);
				xe_exec_queue_put(q);
			}
			mutex_unlock(&xef->exec_queue.lock);
			user->active_duration_ns += xef->active_duration_ns;
			xef->active_duration_ns = 0;
		}

		xe_pm_runtime_put(xe);

		/* Each duration must be non-zero and non-overlapping.
		 * @start_time is inclusive, while @end_time is exclusive.
		 *
		 * Additionally, according to the tracepoint specification:
		 *   The |active_duration| value must be less than or equal to
		 *   the period duration (|end_time| - |start_time|).
		 *
		 *   If the aggregation approach might violate this requirement
		 *   then the driver must clamp |active_duration| to be at most
		 *   the period duration.
		 *
		 *   A period's duration (|end_time| - |start_time|) must be at
		 *   most 1 second.
		 */
		start_time = user->last_timestamp_ns;
		end_time = ktime_get_raw_ns();
		if (start_time < end_time) {
			period_duration =
				min(NSEC_PER_SEC, end_time - start_time);
			start_time = end_time - period_duration;
			active_duration = min(period_duration,
					      user->active_duration_ns -
						      last_active_duration);
			trace_gpu_work_period(gpuid, uid, start_time, end_time,
					      active_duration);
		}
		user->last_timestamp_ns = end_time;

		mutex_unlock(&user->lock);
	}

	/* Release the worker's ref and if there are still refs on the
	 * user, attempt to schedule the next work item.
	 */
	if (!xe_user_put(user))
		schedule_next_work(user);
}

/**
 * xe_user_lookup() - Lookup existing xe user by uid
 * @xe: The xe device
 * @uid: uid to match
 *
 * Searches for xe user with matching uid, taking a ref and returning on
 * success.
 *
 * Return: pointer to user struct or NULL if not found
 */
static struct xe_user *xe_user_lookup(struct xe_device *xe, u32 uid)
{
	struct xe_user *user = NULL;
	struct xe_user *tmp;
	unsigned long i;

	/* lock to avoid racing with user destruction between finding a matching
	 * entry in the xarray and getting a ref.
	 */
	lockdep_assert_held(&xe->work_period.lock);
	xa_for_each(&xe->work_period.users, i, tmp) {
		if (tmp->uid == uid) {
			user = tmp;
			xe_user_get(user);
			break;
		}
	}

	return user;
}
int xe_work_period_attach(struct xe_device *xe, struct xe_file *xef, unsigned int uid)

{
	struct xe_user *user = NULL;

	/*
	 * Check if the calling process/uid has already been registered with the
	 * xe device during a previous open call. If so then take a reference to
	 * this xe user and add this xe file to the filelist belonging to this
	 * xe user.
	 */
	mutex_lock(&xe->work_period.lock);
	user = xe_user_lookup(xe, uid);
	if (!user) {
		user = xe_user_alloc(xef, uid);
		if (!user)
			return -ENOMEM;

		mutex_unlock(&xe->work_period.lock);
		schedule_next_work(user);
	} else {
		mutex_unlock(&xe->work_period.lock);
	}

	xef->user = user;
	mutex_lock(&user->lock);
	list_add(&xef->user_link, &user->filelist);
	mutex_unlock(&user->lock);

	return 0;
}

void xe_work_period_detach(struct xe_file *xef)
{
	if (xef->user) {
		mutex_lock(&xef->user->lock);
		list_del(&xef->user_link);
		mutex_unlock(&xef->user->lock);
		xe_user_put(xef->user);
		xef->user = NULL;
	}
}

void xe_work_period_suspend(struct xe_device *xe)
{
	struct xe_user *user = NULL;
	unsigned long i = 0;

	xa_for_each(&xe->work_period.users, i, user) {
		if (cancel_delayed_work_sync(&user->delay_work))
			xe_user_put(user);
	}
}

void xe_work_period_resume(struct xe_device *xe)
{
	struct xe_user *user = NULL;
	unsigned long i = 0;

	/* no need to lock because schedule_next_work() atomically checks the
	 * validity of each user and acquires its own ref. If a user is dropped
	 * between reaching it in this iterator and attempting to get a ref, it
	 * can be safely excluded from rescheduling since there are no other
	 * remaining reference holders.
	 */
	xa_for_each(&xe->work_period.users, i, user) {
		user->last_timestamp_ns = ktime_get_raw_ns();
		schedule_next_work(user);
	}
}

void xe_work_period_init(struct xe_device *xe)
{
	mutex_init(&xe->work_period.lock);
	xa_init_flags(&xe->work_period.users, XA_FLAGS_ALLOC1);
}

void xe_work_period_fini(struct xe_device *xe)
{
	xe_work_period_suspend(xe);
	xa_destroy(&xe->work_period.users);
	mutex_destroy(&xe->work_period.lock);
}
