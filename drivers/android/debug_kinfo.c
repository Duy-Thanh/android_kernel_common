// SPDX-License-Identifier: GPL-2.0
/*
 * debug_kinfo.c - backup kernel information for bootloader usage
 *
 * Copyright 2002 Rusty Russell <rusty@rustcorp.com.au> IBM Corporation
 * Copyright 2021 Google LLC
 */

#include <linux/platform_device.h>
#include <linux/kallsyms.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_device.h>
#include <linux/pgtable.h>
#include <asm/module.h>
#include "debug_kinfo.h"

/*
 * These will be re-linked against their real values
 * during the second link stage.
 */
extern const unsigned long kallsyms_addresses[] __weak;
extern const int kallsyms_offsets[] __weak;
extern const u8 kallsyms_names[] __weak;
extern const u8 kallsyms_seqs_of_names[] __weak;
/*
 * Tell the compiler that the count isn't in the small data section if the arch
 * has one (eg: FRV).
 */
extern const unsigned int kallsyms_num_syms __weak
__section(".rodata");

extern const unsigned long kallsyms_relative_base __weak
__section(".rodata");

extern const u8 kallsyms_token_table[] __weak;
extern const u16 kallsyms_token_index[] __weak;

extern const unsigned int kallsyms_markers[] __weak;

static struct debug_kinfo_variant_data *variant_data;

static void update_kernel_all_info(struct debug_kinfo_variant_data *data)
{
	int index;
	size_t info_size = data->info_size;

	*data->magic_num_ptr = DEBUG_KINFO_MAGIC;
	*data->combined_checksum_ptr = 0;
	for (index = 0; index < info_size / sizeof(u32); index++)
		*data->combined_checksum_ptr ^= data->checksum_info[index];
}

static int build_info_set(const char *str, const struct kernel_param *kp)
{
	size_t build_info_size;
	int ret = 0;

	if (!variant_data) {
		ret = -EPERM;
		goto Exit;
	}

	build_info_size = variant_data->build_info_size;
	memcpy(variant_data->build_info_ptr, str,
			min(build_info_size - 1, strlen(str)));
	update_kernel_all_info(variant_data);
	if (strlen(str) > build_info_size) {
		pr_warn("%s: Build info buffer (len: %zd) can't hold entire string '%s'\n",
				__func__, build_info_size, str);
		ret = -ENOMEM;
	}

Exit:
	return ret;
}

static const struct kernel_param_ops build_info_op = {
	.set = build_info_set,
};

module_param_cb(build_info, &build_info_op, NULL, 0200);
MODULE_PARM_DESC(build_info, "Write build info to field 'build_info' of debug kinfo.");

static void init_kinfo_common(struct kernel_info *info)
{
	info->enabled_all = IS_ENABLED(CONFIG_KALLSYMS_ALL);
	info->enabled_base_relative = IS_ENABLED(CONFIG_KALLSYMS_BASE_RELATIVE);
	info->enabled_absolute_percpu = IS_ENABLED(CONFIG_KALLSYMS_ABSOLUTE_PERCPU);
	info->enabled_cfi_clang = IS_ENABLED(CONFIG_CFI_CLANG);
	info->num_syms = kallsyms_num_syms;
	info->name_len = KSYM_NAME_LEN;
	info->bit_per_long = BITS_PER_LONG;
	info->module_name_len = MODULE_NAME_LEN;
	info->symbol_len = KSYM_SYMBOL_LEN;
	if (!info->enabled_base_relative)
		info->_addresses_pa = (u64)__pa_symbol((volatile void *)kallsyms_addresses);
	else {
		info->_relative_pa = (u64)__pa_symbol((volatile void *)kallsyms_relative_base);
		info->_offsets_pa = (u64)__pa_symbol((volatile void *)kallsyms_offsets);
	}
	info->_stext_pa = (u64)__pa_symbol(_stext);
	info->_etext_pa = (u64)__pa_symbol(_etext);
	info->_sinittext_pa = (u64)__pa_symbol(_sinittext);
	info->_einittext_pa = (u64)__pa_symbol(_einittext);
	info->_end_pa = (u64)__pa_symbol(_end);
	info->_names_pa = (u64)__pa_symbol((volatile void *)kallsyms_names);
	info->_token_table_pa = (u64)__pa_symbol((volatile void *)kallsyms_token_table);
	info->_token_index_pa = (u64)__pa_symbol((volatile void *)kallsyms_token_index);
	info->_markers_pa = (u64)__pa_symbol((volatile void *)kallsyms_markers);
	info->thread_size = THREAD_SIZE;
	info->swapper_pg_dir_pa = (u64)__pa_symbol(swapper_pg_dir);
	strscpy(info->last_uts_release, init_utsname()->release, sizeof(info->last_uts_release));
	info->enabled_modules_tree_lookup = IS_ENABLED(CONFIG_MODULES_TREE_LOOKUP);
	info->mod_mem_offset = offsetof(struct module, mem);
	info->mod_kallsyms_offset = offsetof(struct module, kallsyms);
}

static void init_kinfo_v2(struct kernel_info_v2 *info_v2)
{
	info_v2->_seqs_of_names_pa = (u64)__pa_symbol((volatile void *)kallsyms_seqs_of_names);
}

static void init_build_info(struct kernel_info *info, struct debug_kinfo_variant_data *data)
{
	data->build_info_ptr = info->build_info;
	data->build_info_size = sizeof(info->build_info);
}

static void init_all_info_v1(struct debug_kinfo_variant_data *data)
{
	struct kernel_all_info *all_info;
	struct kernel_info *info;

	memset(data->all_info_addr, 0, sizeof(struct kernel_all_info));
	all_info = (struct kernel_all_info *)data->all_info_addr;
	info = &(all_info->info);
	data->magic_num_ptr = &(all_info->magic_number);
	data->combined_checksum_ptr = &(all_info->combined_checksum);
	data->checksum_info = (u32 *)info;
	data->info_size = sizeof(*info);
	init_kinfo_common(info);
	init_build_info(info, data);
}

static void init_all_info_v2(struct debug_kinfo_variant_data *data)
{
	struct kernel_all_info_v2 *all_info_v2;
	struct kernel_info_v2 *info_v2;
	struct kernel_info *info;

	memset(data->all_info_addr, 0, sizeof(struct kernel_all_info_v2));
	all_info_v2 = (struct kernel_all_info_v2 *)data->all_info_addr;
	info = &(all_info_v2->info_v2.info);
	info_v2 = &(all_info_v2->info_v2);
	data->magic_num_ptr = &(all_info_v2->magic_number);
	data->combined_checksum_ptr = &(all_info_v2->combined_checksum);
	data->checksum_info = (u32 *)info_v2;
	data->info_size = sizeof(*info_v2);
	init_kinfo_common(info);
	init_kinfo_v2(info_v2);
	init_build_info(info, data);
}

static const struct debug_kinfo_variant drv_data_v1 = {
	.version = DEBUG_KINFO_VERSION_1,
};

static const struct debug_kinfo_variant drv_data_v2 = {
	.version = DEBUG_KINFO_VERSION_2,
};

static const struct of_device_id debug_kinfo_of_match[] = {
	{ .compatible	= "google,debug-kinfo",
	  .data = &drv_data_v1 },
	{ .compatible	= "google,debug-kinfo-v2",
	  .data = &drv_data_v2 },
	{},
};

static int debug_kinfo_probe(struct platform_device *pdev)
{
	struct device_node *mem_region;
	struct reserved_mem *rmem;
	const struct of_device_id *match;
	struct debug_kinfo_variant *drv_data;
	struct debug_kinfo_variant_data *data;

	mem_region = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!mem_region) {
		dev_warn(&pdev->dev, "no such memory-region\n");
		return -ENODEV;
	}

	rmem = of_reserved_mem_lookup(mem_region);
	if (!rmem) {
		dev_warn(&pdev->dev, "no such reserved mem of node name %s\n",
				pdev->dev.of_node->name);
		return -ENODEV;
	}

	match = of_match_device(debug_kinfo_of_match, &pdev->dev);
	if (!match) {
		dev_warn(&pdev->dev, "Matching device not found\n");
		return -ENODEV;
	}

	/* Need to wait for reserved memory to be mapped */
	if (!rmem->priv) {
		return -EPROBE_DEFER;
	}

	if (!rmem->base || !rmem->size) {
		dev_warn(&pdev->dev, "unexpected reserved memory\n");
		return -EINVAL;
	}

	drv_data = (struct debug_kinfo_variant *)match->data;
	if (!drv_data) {
		dev_warn(&pdev->dev, "unexpected data\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct debug_kinfo_variant_data), GFP_KERNEL);
	if (!data) {
		dev_warn(&pdev->dev, "Faild to allocate memory for data\n");
		return -ENOMEM;
	}

	data->all_info_addr = rmem->priv;
	data->all_info_size = rmem->size;
	switch (drv_data->version) {
	case DEBUG_KINFO_VERSION_1:
		if (rmem->size < sizeof(struct kernel_all_info)) {
			dev_warn(&pdev->dev, "unexpected reserved memory size\n");
			return -EINVAL;
		}
		init_all_info_v1(data);
		break;
	case DEBUG_KINFO_VERSION_2:
		if (rmem->size < sizeof(struct kernel_all_info_v2)) {
			dev_warn(&pdev->dev, "unexpected reserved memory size\n");
			return -EINVAL;
		}
		init_all_info_v2(data);
		break;
	default:
		dev_warn(&pdev->dev, "Unknown version\n");
		return -EINVAL;
	}
	update_kernel_all_info(data);
	variant_data = data;
	return 0;
}

static struct platform_driver debug_kinfo_driver = {
	.probe = debug_kinfo_probe,
	.driver = {
		.name = "debug-kinfo",
		.of_match_table = of_match_ptr(debug_kinfo_of_match),
	},
};
module_platform_driver(debug_kinfo_driver);

MODULE_AUTHOR("Jone Chou <jonechou@google.com>");
MODULE_DESCRIPTION("Debug Kinfo Driver");
MODULE_LICENSE("GPL v2");
