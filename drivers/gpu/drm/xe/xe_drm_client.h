/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DRM_CLIENT_H_
#define _XE_DRM_CLIENT_H_

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct drm_file;
struct drm_printer;
struct xe_bo;

struct xe_drm_client {
	struct kref kref;
	unsigned int id;
#ifdef CONFIG_PROC_FS
	/**
	 * @bos_lock: lock protecting @bos_list
	 */
	spinlock_t bos_lock;
	/**
	 * @bos_list: list of bos created by this client
	 *
	 * Protected by @bos_lock.
	 */
	struct list_head bos_list;
#endif
};

struct xe_user {
	struct kref refcount;
	struct xe_device *xe;
	struct mutex filelist_lock;
	struct list_head filelist;
	struct list_head entry;
	struct work_struct work;
	u32 uid;
	u64 active_duration_ns;
	u64 last_timestamp_ns;
};

	static inline struct xe_drm_client *
xe_drm_client_get(struct xe_drm_client *client)
{
	kref_get(&client->kref);
	return client;
}

void __xe_drm_client_free(struct kref *kref);

static inline void xe_drm_client_put(struct xe_drm_client *client)
{
	kref_put(&client->kref, __xe_drm_client_free);
}

struct xe_drm_client *xe_drm_client_alloc(void);
static inline struct xe_drm_client *
xe_drm_client_get(struct xe_drm_client *client);
static inline void xe_drm_client_put(struct xe_drm_client *client);
#ifdef CONFIG_PROC_FS
void xe_drm_client_fdinfo(struct drm_printer *p, struct drm_file *file);
void xe_drm_client_add_bo(struct xe_drm_client *client,
			  struct xe_bo *bo);
void xe_drm_client_remove_bo(struct xe_bo *bo);
#else
static inline void xe_drm_client_add_bo(struct xe_drm_client *client,
					struct xe_bo *bo)
{
}

static inline void xe_drm_client_remove_bo(struct xe_bo *bo)
{
}
#endif

struct xe_user *xe_user_alloc(void);

static inline struct xe_user *
xe_user_get(struct xe_user *user)
{
	kref_get(&user->refcount);
	return user;
}

void __xe_user_free(struct kref *kref);

static inline void xe_user_put(struct xe_user *user)
{
	kref_put(&user->refcount, __xe_user_free);
}
#endif
