// SPDX-License-Identifier: GPL-2.0
/*
 * debug_kinfo.h - backup kernel information for bootloader usage
 *
 * Copyright 2021 Google LLC
 */

#ifndef DEBUG_KINFO_H
#define DEBUG_KINFO_H

#include <linux/utsname.h>

#define BUILD_INFO_LEN		256
#define DEBUG_KINFO_MAGIC	0xCCEEDDFF

/*
 * Header structure must be byte-packed, since the table is provided to
 * bootloader.
 */
struct kernel_info {
	/* For kallsyms */
	__u8 enabled_all;
	__u8 enabled_base_relative;
	__u8 enabled_absolute_percpu;
	__u8 enabled_cfi_clang;
	__u32 num_syms;
	__u16 name_len;
	__u16 bit_per_long;
	__u16 module_name_len;
	__u16 symbol_len;
	__u64 _addresses_pa;
	__u64 _relative_pa;
	__u64 _stext_pa;
	__u64 _etext_pa;
	__u64 _sinittext_pa;
	__u64 _einittext_pa;
	__u64 _end_pa;
	__u64 _offsets_pa;
	__u64 _names_pa;
	__u64 _token_table_pa;
	__u64 _token_index_pa;
	__u64 _markers_pa;

	/* For frame pointer */
	__u32 thread_size;

	/* For virt_to_phys */
	__u64 swapper_pg_dir_pa;

	/* For linux banner */
	__u8 last_uts_release[__NEW_UTS_LEN];

	/* Info of running build */
	__u8 build_info[BUILD_INFO_LEN];

	/* For module kallsyms */
	__u32 enabled_modules_tree_lookup;
	__u32 mod_mem_offset;
	__u32 mod_kallsyms_offset;
} __packed;

struct kernel_all_info {
	__u32 magic_number;
	__u32 combined_checksum;
	struct kernel_info info;
} __packed;

struct kernel_info_v2 {
	/*For Existing kernel_info */
	struct kernel_info info;

	/* For kallsyms */
	__u64 _seqs_of_names_pa;

	/* For extra elements in advance */
	__u64 reserved_data[4];
} __packed;

struct kernel_all_info_v2 {
	__u32 magic_number;
	__u32 combined_checksum;
	struct kernel_info_v2 info_v2;
} __packed;

struct debug_kinfo_variant {
	int version;
};

struct debug_kinfo_variant_data {
	void *all_info_addr;
	u32 all_info_size;

	/* For update_kernel_all_info function*/
	u32 *magic_num_ptr;
	u32 *combined_checksum_ptr;
	u32 *checksum_info;
	size_t info_size;

	/* For build_info_set function*/
	u8  *build_info_ptr;
	size_t build_info_size;
};

enum {
	DEBUG_KINFO_VERSION_1,
	DEBUG_KINFO_VERSION_2,
};

#endif // DEBUG_KINFO_H
