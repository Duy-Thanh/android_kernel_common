/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct f2fs_sb_info;
struct dnode_of_data;

DECLARE_HOOK(android_vh_f2fs_init_sbi_oem_data,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

DECLARE_HOOK(android_vh_f2fs_record_data_alloc_type,
	TP_PROTO(struct f2fs_sb_info *sbi, int type),
	TP_ARGS(sbi, type));

DECLARE_HOOK(android_vh_f2fs_destory_sbi_oem_data,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

DECLARE_HOOK(android_vh_f2fs_init_inode_oem_data,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode));

DECLARE_HOOK(android_vh_f2fs_get_data_segment_type,
	TP_PROTO(struct inode *inode, pgoff_t pgofs, int *type),
	TP_ARGS(inode, pgofs, type));

DECLARE_HOOK(android_vh_f2fs_record_outplace_write_info,
	TP_PROTO(struct dnode_of_data *dn),
	TP_ARGS(dn));

DECLARE_HOOK(android_vh_f2fs_truncate_inode_oem_data,
	TP_PROTO(struct inode *inode, pgoff_t fofs,
				unsigned int len),
	TP_ARGS(inode, fofs, len));

DECLARE_HOOK(android_vh_f2fs_shrink_oem_data,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

DECLARE_HOOK(android_vh_f2fs_drop_inode_oem_data,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode));

DECLARE_HOOK(android_vh_f2fs_destroy_inode_oem_data,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode));

#endif /* _TRACE_HOOK_F2FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
