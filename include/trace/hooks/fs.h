/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <trace/hooks/vendor_hooks.h>

struct dentry;
struct inode;
struct page;
struct bio;
struct f2fs_sb_info;

DECLARE_HOOK(android_vh_f2fs_create,
	TP_PROTO(struct inode *inode, struct dentry *dentry),
	TP_ARGS(inode, dentry));

DECLARE_HOOK(android_vh_f2fs_submit_write_page,
	TP_PROTO(struct page *page, struct bio *bio),
	TP_ARGS(page, bio));

DECLARE_HOOK(android_vh_f2fs_set_sbi_flag,
	TP_PROTO(struct f2fs_sb_info *sbi, unsigned int type, const char *func, int line),
	TP_ARGS(sbi, type, func, line));

#endif /* _TRACE_HOOK_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
