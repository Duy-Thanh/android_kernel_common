// SPDX-License-Identifier: GPL-2.0
/* drivers/net/wireless/virt_wifi.c
 *
 * Demo virt_wifi extension hack.
 *
 * Copyright (C) 2019 Google, Inc.
 *
 * Author: schuffelen@google.com
 */

#include <linux/module.h>  /* Needed by all modules */
#include <net/cfg80211.h>

void virt_wifi_extension_function(struct wiphy*);

void virt_wifi_extension_function(struct wiphy* wiphy) {
  if (!wiphy) {
    printk(KERN_ERR "No wiphy provided");
  }
}
