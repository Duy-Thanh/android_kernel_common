/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

#include <linux/sched.h>
#include <linux/xarray.h>

#ifdef __GENKSYMS__
/*
 * ANDROID ABI HACK
 * This structure got removed from the file in the 5.10.162 release and moved
 * to be a private structure.  Unfortunatly the kernel abi checker already had a
 * "signature" of it in the .xml database.  As this structure is never used or
 * touched by any module/driver/whatever it is safe to be an anonymous
 * structure, so the change was all good.  However we need to keep the "old"
 * signature of the structure around in order to keep the CRC checks "stable"
 * and allow new kernels to work with older modules that are not rebuilt.
 *
 * Yes, it's a horrible hack, and this really is not the true structure, but so
 * life is in the world of stable apis...
 * The real structure is in io_uring/io_uring.c, see the one there if you need
 * to touch or do anything with it.
 *
 * NEVER touch this structure definition, it is fake and not even valid code.
 */
struct io_uring_task {
	/* submission side */
	struct xarray		xa;
	struct wait_queue_head	wait;
	struct file		*last;
	struct percpu_counter	inflight;
	struct io_identity	__identity;
	struct io_identity	*identity;
	atomic_t		in_idle;
	bool			sqpoll;
};
#endif


#if defined(CONFIG_IO_URING)
struct sock *io_uring_get_socket(struct file *file);
void __io_uring_cancel(bool cancel_all);
void __io_uring_free(struct task_struct *tsk);

static inline void io_uring_files_cancel(void)
{
	if (current->io_uring)
		__io_uring_cancel(false);
}
static inline void io_uring_task_cancel(void)
{
	if (current->io_uring)
		__io_uring_cancel(true);
}
static inline void io_uring_free(struct task_struct *tsk)
{
	if (tsk->io_uring)
		__io_uring_free(tsk);
}
#else
static inline struct sock *io_uring_get_socket(struct file *file)
{
	return NULL;
}
static inline void io_uring_task_cancel(void)
{
}
static inline void io_uring_files_cancel(void)
{
}
static inline void io_uring_free(struct task_struct *tsk)
{
}
#endif

#endif
