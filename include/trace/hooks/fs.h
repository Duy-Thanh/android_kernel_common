/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct file;
struct inode;
struct f2fs_sb_info;
struct f2fs_defragment;

DECLARE_HOOK(android_vh_f2fs_ioctl_defrag,
	TP_PROTO(int(*ptr)(struct f2fs_sb_info *sbi, struct file *filp,
	struct f2fs_defragment *range), struct file *filp, unsigned int *cmd,
	unsigned long arg, unsigned int ioc_getversion),
	TP_ARGS(ptr, filp, cmd, arg, ioc_getversion));

DECLARE_HOOK(android_vh_f2fs_set_gc_status,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

DECLARE_HOOK(android_vh_f2fs_restore_gc_status,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

DECLARE_HOOK(android_vh_f2fs_set_gc_mode,
	TP_PROTO(int *gc_mode),
	TP_ARGS(gc_mode));

DECLARE_HOOK(android_vh_f2fs_get_is_idle,
	TP_PROTO(struct f2fs_sb_info *sbi, int *gc_mode, int *type),
	TP_ARGS(sbi, gc_mode, type));

DECLARE_HOOK(android_vh_f2fs_get_pages_address,
	TP_PROTO(struct f2fs_sb_info *sbi, int *type),
	TP_ARGS(sbi, type));

DECLARE_HOOK(android_vh_f2fs_inode_flag_set,
	TP_PROTO(int *flag),
	TP_ARGS(flag));

DECLARE_HOOK(android_vh_f2fs_set_is_file,
	TP_PROTO(int *type),
	TP_ARGS(type));
#endif /* _TRACE_HOOK_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
