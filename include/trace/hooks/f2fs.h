/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_H

#include <trace/hooks/vendor_hooks.h>

/* struct f2fs_sb_info */
#include <linux/f2fs_fs.h>
#include <../fs/f2fs/f2fs.h>

DECLARE_HOOK(android_vh_limit_reserve_root,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

#endif /* _TRACE_HOOK_F2FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
