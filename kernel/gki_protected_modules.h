/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Google LLC
 * Author: ramjiyani@google.com (Ramji Jiyani)
 */
#ifndef _GKI_PROTECTED_MODULES_H_
#define _GKI_PROTECTED_MODULES_H_

#include <linux/module.h>

#define NUM_OF_PROTECTED_MODULES \
	(ARRAY_SIZE(gki_protected_modules))

/*
 * gki_protected_modules: Sorted list of protected module names
 *
 * Modules appearing in this list will not be allowed to load if
 * they are not signed GKI modules.
 */
static const char gki_protected_modules[][MODULE_NAME_LEN] = {
	/* Keep sorted in ascending order */
	"6lowpan",
	"8021q",
	"bluetooth",
	"bsd_comp",
	"btbcm",
	"btqca",
	"btsdio",
	"can",
	"can-bcm",
	"can-dev",
	"can-gw",
	"can-raw",
	"cdc-acm",
	"cfg80211",
	"diag",
	"ftdi_sio",
	"hci_uart",
	"hidp",
	"ieee802154",
	"ieee802154_6lowpan",
	"ieee802154_socket",
	"l2tp_core",
	"l2tp_ppp",
	"libarc4",
	"mac80211",
	"mac802154",
	"nfc",
	"nhc_dest",
	"nhc_fragment",
	"nhc_hop",
	"nhc_ipv6",
	"nhc_mobility",
	"nhc_routing",
	"nhc_udp",
	"ppp_deflate",
	"ppp_generic",
	"ppp_mppe",
	"pppox",
	"pptp",
	"rfcomm",
	"rfkill",
	"slcan",
	"slhc",
	"tipc",
	"usbserial",
	"vcan",
};

#endif /* _GKI_PROTECTED_MODULES_H_ */
