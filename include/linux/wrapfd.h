/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WrapFD kernel API
 *
 * Copyright (C) 2025 Google, Inc.
 */

#ifndef _LINUX_WRAPFD_H
#define _LINUX_WRAPFD_H

union wrapfd_mappable {
	struct dma_buf *dmabuf;
};

struct wrapfd_listener_operations {
	void (*on_content_release)(void *param);
};

#ifdef CONFIG_ANDROID_WRAPFD

/* Check if the vma is mapping a wrapfd file. */
bool is_wrapfd_vma(struct vm_area_struct *vma);

/*
 * Get mappable object. Caller also gets buffer ownership.
 * Function does not take file references, therefore the caller should
 * ensure the file is stable until wrapfd_put_mappable() is called.
 *
 * file: wrap file to get the mappable object.
 * dev: device requesting the operation.
 * mappable: mappable object.
 *
 * On success returns mappable object. On error returns:
 * -EBADF: file is invalid.
 * -ENODEV: device is invalid.
 * -EINVAL: buffer is mapped or mappable is NULL.
 * -EBUSY: buffer is owned by another device.
 * -ENOENT: wrap is empty (buffer got freed or moved)
 */
int wrapfd_get_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable);

/*
 * Release mappable object and buffer ownership. Caller should own the buffer.
 * Note that the caller might still keep the buffer mapped. If the buffer
 * is reused or freed then the next wrapfd_get_mappable() call will fail with
 * -ENOENT and the mapping should be invalidated. If wrapfd_get_mappable()
 * succeeds then previous mappings are still valid and can be reused.
 *
 * file: wrap file associated with the mappable object.
 * dev: device requesting the operation.
 * mappable: mappable object returned by wrapfd_get_mappable().
 *
 * On success returns 0. On error returns:
 * -EBADF: file is invalid.
 * -ENODEV: device is invalid.
 * -EBUSY: buffer is not owned by the caller.
 * -EINVAL: wrap is empty or mappable is NULL or mappable's contents does not
 *	    match the one passed to wrapfd_get_mappable.
 */
int wrapfd_put_mappable(struct file *file, struct device *dev,
			union wrapfd_mappable *mappable);

/*
 * Register a listener for the mappable object currently wrapped by the @file
 * object. Caller should own the buffer.
 * The caller can keep the mappable object mapped in its device's address space
 * but it has to unmap it onve on_content_release callback is called.
 *
 * file: wrap file associated with the mappable object.
 * dev: device requesting the operation.
 * ops: wrapfd lintener structure with event handlers.
 * param: parameter passed to the event handler.
 *
 * On success returns 0. On error returns:
 * -EBADF: file is invalid.
 * -ENODEV: device is invalid.
 * -EBUSY: buffer is not owned by the caller.
 * -ENOENT: wrap is empty (buffer got freed or moved).
 * -ENOMEM: not enough memory.
 */
int wrapfd_add_listener(struct file *file, struct device *dev,
			const struct wrapfd_listener_operations *ops,
			void *param);

/*
 * Unregister previously registered listener. Caller does not have to own the
 * buffer.
 * The caller can unregister its listener if it no longer needs to monitor
 * the content of the wrapfd object.
 *
 * file: wrap file associated with the mappable object.
 * param: parameter passed to wrapfd_add_listener().
 *
 * On success returns 0. On error returns:
 * -EBADF: file is invalid.
 * -EINVAL: listener is not found.
 */
int wrapfd_del_listener(struct file *file, void *param);

#else /* CONFIG_ANDROID_WRAPFD */

static inline bool is_wrapfd_vma(struct vm_area_struct *vma)
{
	return false;
}

static inline int wrapfd_get_mappable(struct file *file, struct device *dev,
				      union wrapfd_mappable *mappable)
{
	return -ENOENT;
}

static inline int wrapfd_put_mappable(struct file *file, struct device *dev,
				      union wrapfd_mappable *mappable)
{
	return -ENOENT;
}

static inline int wrapfd_add_listener(struct file *file, struct device *dev,
				      const struct wrapfd_listener_operations *ops,
				      void *param);
{
	return -EINVAL;
}

static inline int wrapfd_del_listener(struct file *file, void *param)
{
	return -EINVAL;
}

#endif /* CONFIG_ANDROID_WRAPFD */

#endif /* _LINUX_WRAPFD_H */
