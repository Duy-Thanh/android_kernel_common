// SPDX-License-Identifier: GPL-2.0
/* drivers/net/wireless/virt_wifi.c
 *
 * Demo virt_wifi extension hack.
 *
 * Copyright (C) 2019 Google, Inc.
 *
 * Author: schuffelen@google.com
 */

#include <net/cfg80211.h>

void virt_wifi_extension_function(struct wiphy*);

void virt_wifi_extension_function(struct wiphy* wiphy) {
  printk(KERN_ERR "Extension logic for wiphy %p", wiphy);
}
