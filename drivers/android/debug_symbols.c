// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/android_debug_symbols.h>

struct ads_entry {
	char const * const name;
	uintptr_t addr;
};

static struct ads_entry ads_entries[] = {
	{ .name = "_sdata", .addr = _sdata }, //ADS_SDATA
	{ .name = "__bss_stop", .addr = __bss_stop }, //ADS_BSS_END
	{ .name = "__per_cpu_start", .addr = __per_cpu_start }, //ADS_PER_CPU_START
	{ .name = "__per_cpu_end", .addr = __per_cpu_end }, //ADS_PER_CPU_END
	{ .name = "__start_ro_after_init", .addr = __start_ro_after_init },//ADS_START_RO_AFTER_INIT
	{ .name = "__end_ro_after_init", .addr = __end_ro_after_init },  //ADS_END_RO_AFTER_INIT
	{ .name = "ads_end", .addr = NULL } // ADS_END
};

/*
 * get_android_debug_symbol is used by vendor modules to find
 * address of a symbols needed for extending core kernel functionality.
 * symbol address is used to capturing the portion of memory
 * for offline dump analysis.
 *
 */
void *get_android_debug_symbol(enum android_debug_symbol symbol)
{
	if (symbol >= ADS_END)
		return ERR_PTR(-EINVAL);

	return (void *)ads_entries[symbol].addr;
}
EXPORT_SYMBOL_GPL(get_android_debug_symbol);
