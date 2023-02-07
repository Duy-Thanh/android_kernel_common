// SPDX-License-Identifier: GPL-2.0
/*
 * Module to print ARM64 processor information such as:
 *
 *   - Memory info (granule size, vm address size, etc).
 *   - Cache information.
 *   - Aarch64 exception handling
 *   - System register information.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s() " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>


static int __init arm64info_init(void)
{
  pr_info("-----------------ARM64 CPU info-------------------\n");

  return 0;
}

static void __exit arm64info_exit(void)
{
  pr_info("Unloading the module\n");
}

module_init(arm64info_init);
module_exit(arm64info_exit);

MODULE_DESCRIPTION("ARM64 info");
MODULE_LICENSE("GPL v2");
