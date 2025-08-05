/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_H

#include <trace/hooks/vendor_hooks.h>

#include <linux/f2fs_fs.h>
#include <../../../fs/f2fs/f2fs.h>

struct inode;

DECLARE_HOOK(android_vh_modify_cp_reason,
		TP_PROTO(struct inode *inode, enum cp_reason_type *cp_reason),
		TP_ARGS(inode, cp_reason));

DECLARE_HOOK(android_vh_f2fs_file_write_end,
		TP_PROTO(struct inode *inode),
		TP_ARGS(inode));

#endif /* _TRACE_HOOK_F2FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
