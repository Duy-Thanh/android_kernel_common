/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Boilerplate shared by the PPPS selftest fixture modules.
 *
 * Nearly every fixture is a dynamic-minor misc device whose file_operations
 * exercise one kernel mmap/GUP/iov path under a 4K compat process.  The
 * module init/exit, the miscdevice and the MODULE_* tags are identical
 * across them, so a fixture is:
 *
 *	static const struct file_operations foo_fops = { ... };
 *	PPPS_MISC_MODULE("foo_ppps", &foo_fops, 0600,
 *			 ppps_misc_no_setup, ppps_misc_no_teardown,
 *			 "what this fixture exercises");
 *
 * @setup runs before the device is registered and returns 0 or -errno;
 * @teardown undoes it, both on a failed registration and at module exit.
 * PPPS_MISC_MODULE_POST() additionally takes a hook that runs after a
 * successful misc_register() with the registered miscdevice, for fixtures
 * that need the device it creates (its DMA mask, for instance).
 */
#ifndef __PPPS_MISC_MODULE_H
#define __PPPS_MISC_MODULE_H

#include <linux/miscdevice.h>
#include <linux/module.h>

static inline int ppps_misc_no_setup(void)
{
	return 0;
}

static inline void ppps_misc_no_teardown(void)
{
}

static inline int ppps_misc_no_post(struct miscdevice *device)
{
	return 0;
}

#define PPPS_MISC_MODULE_POST(devname, fops_ptr, devmode, setup_fn,	\
			      post_fn, teardown_fn, desc)		\
static struct miscdevice ppps_misc_device = {				\
	.minor = MISC_DYNAMIC_MINOR,					\
	.name = devname,						\
	.fops = fops_ptr,						\
	.mode = devmode,						\
};									\
									\
static int __init ppps_misc_module_init(void)				\
{									\
	int error = setup_fn();						\
									\
	if (error)							\
		return error;						\
	error = misc_register(&ppps_misc_device);			\
	if (!error) {							\
		error = post_fn(&ppps_misc_device);			\
		if (error)						\
			misc_deregister(&ppps_misc_device);		\
	}								\
	if (error)							\
		teardown_fn();						\
	return error;							\
}									\
									\
static void __exit ppps_misc_module_exit(void)				\
{									\
	misc_deregister(&ppps_misc_device);				\
	teardown_fn();							\
}									\
									\
module_init(ppps_misc_module_init);					\
module_exit(ppps_misc_module_exit);					\
MODULE_LICENSE("GPL");							\
MODULE_DESCRIPTION(desc)

#define PPPS_MISC_MODULE(devname, fops_ptr, devmode, setup_fn, teardown_fn, \
			 desc)						\
	PPPS_MISC_MODULE_POST(devname, fops_ptr, devmode, setup_fn,	\
			      ppps_misc_no_post, teardown_fn, desc)

#endif /* __PPPS_MISC_MODULE_H */
