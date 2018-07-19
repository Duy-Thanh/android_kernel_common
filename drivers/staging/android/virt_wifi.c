// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/android/staging/virt_wifi.c
 *
 * A fake implementation of cfg80211_ops that can be tacked on to an ethernet
 * net_device to make it appear as a wireless connection.
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * Author: schuffelen@google.com
 */

#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/module.h>

static struct ieee80211_channel channel = {
	.band = NL80211_BAND_5GHZ,
	.center_freq = 5500,
	.hw_value = 5500,

	.flags = 0, /* ieee80211_channel_flags */
	.max_antenna_gain = 20,
	.max_power = 5500,
	.max_reg_power = 9999,
};

static struct ieee80211_rate bitrate = {
	.flags = IEEE80211_RATE_SHORT_PREAMBLE, /* ieee80211_rate_flags */
	.bitrate = 1000,
};

static struct ieee80211_supported_band band_5ghz = {
	.channels = &channel,
	.bitrates = &bitrate,
	.band = NL80211_BAND_5GHZ,
	.n_channels = 1,
	.n_bitrates = 1,
};

static struct cfg80211_inform_bss mock_inform_bss = {
	/* ieee80211_channel* */ .chan = &channel,
	/* nl80211_bss_scan_width */ .scan_width = NL80211_BSS_CHAN_WIDTH_20,
	/* s32 */ .signal = 99,
};

static u8 fake_router_bssid[] = {4, 4, 4, 4, 4, 4};

static int virt_wifi_scan(struct wiphy *wiphy,
			  struct cfg80211_scan_request *request)
{
	char ssid[] = "__VirtWifi";
	struct cfg80211_bss *informed_bss;
	struct cfg80211_scan_info scan_info = {
		.aborted = false,
	};

	wiphy_debug(wiphy, "scan\n");
	if (request->ie_len > 0)
		wiphy_debug(wiphy, "scan: first ie: %d\n", (int)request->ie[0]);

	if (request->n_ssids > 0) {
		int i;
		u8 request_ssid_copy[IEEE80211_MAX_SSID_LEN + 1];

		for (i = 0; i < request->n_ssids; i++) {
			strncpy(request_ssid_copy, request->ssids[i].ssid,
				request->ssids[i].ssid_len);
			request_ssid_copy[request->ssids[i].ssid_len] = 0;
			wiphy_debug(wiphy, "scan: ssid: %s\n",
				    request_ssid_copy);
		}
	}

	/* Some userspace frameworks validate that the scan complete timestamp
	 * comes after the scan start timstamp. Since timing can sometimes cause
	 * them to happen at the same timestamp when we return immediately,
	 * manually move it forward.
	 */
	mock_inform_bss.boottime_ns = ktime_get_boot_ns() + 1000000;

	ssid[0] = WLAN_EID_SSID;
	/* size of the array minus null terminator, length byte, tag byte */
	ssid[1] = sizeof(ssid) - 3;

	informed_bss =
		cfg80211_inform_bss_data(wiphy, &mock_inform_bss,
					 CFG80211_BSS_FTYPE_PRESP,
					 fake_router_bssid,
					 mock_inform_bss.boottime_ns,
					 WLAN_CAPABILITY_ESS, 0, ssid,
					 /* Truncate before the terminator. */
					 sizeof(ssid) - 1, GFP_KERNEL);
	cfg80211_put_bss(wiphy, informed_bss);

	informed_bss =
		cfg80211_inform_bss_data(wiphy, &mock_inform_bss,
					 CFG80211_BSS_FTYPE_BEACON,
					 fake_router_bssid,
					 mock_inform_bss.boottime_ns,
					 WLAN_CAPABILITY_ESS, 0, ssid,
					 /* Truncate before the terminator. */
					 sizeof(ssid) - 1, GFP_KERNEL);
	cfg80211_put_bss(wiphy, informed_bss);

	request->scan_start--; /* See timestamp addition above. */

	cfg80211_scan_done(request, &scan_info);

	return 0;
}

static struct ieee80211_mgmt auth_mgmt_frame = {
	.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT
				     | IEEE80211_STYPE_AUTH),
	.duration = cpu_to_le16(1), /* ??? */
	.u = {
		.auth = {
			.auth_alg = WLAN_AUTH_OPEN,
			/* auth request has 1, auth response has 2 */
			.auth_transaction = cpu_to_le16(2),
		},
	},
};

static int virt_wifi_auth(struct wiphy *wiphy,  struct net_device *dev,
			  struct cfg80211_auth_request *req)
{
	wiphy_debug(wiphy, "auth\n");
	memcpy(auth_mgmt_frame.da, dev->dev_addr, dev->addr_len);
	memcpy(auth_mgmt_frame.sa, fake_router_bssid,
	       sizeof(fake_router_bssid));
	memcpy(auth_mgmt_frame.bssid, fake_router_bssid,
	       sizeof(fake_router_bssid));
	/* Must call cfg80211_rx_mlme_mgmt to notify about the response to this.
	 * This must hold the mutex for the wedev while calling the function.
	 * Luckily the nl80211 code invoking this already holds that mutex.
	 */
	cfg80211_rx_mlme_mgmt(dev, (const u8 *)&auth_mgmt_frame,
			      sizeof(auth_mgmt_frame));
	return 0;
}

static struct ieee80211_mgmt assoc_mgmt_frame = {
	.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT
				     | IEEE80211_STYPE_ASSOC_RESP),
	.duration = cpu_to_le16(1), /* ??? */
	.u = {
		.assoc_resp = {
			.capab_info = cpu_to_le16(1),
			.status_code = cpu_to_le16(0),
			.aid = cpu_to_le16(2), /* "association id" */
		},
	},
};

static int virt_wifi_assoc(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_assoc_request *req)
{
	wiphy_debug(wiphy, "assoc\n");
	memcpy(assoc_mgmt_frame.da, dev->dev_addr, dev->addr_len);
	memcpy(assoc_mgmt_frame.sa, fake_router_bssid,
	       sizeof(fake_router_bssid));
	memcpy(assoc_mgmt_frame.bssid, fake_router_bssid,
	       sizeof(fake_router_bssid));
	/* Must call cfg80211_rx_assoc_resp to notify about the response to
	 * this. This must hold the mutex for the wedev while calling the
	 * function. Luckily the nl80211 code invoking this already holds that
	 * mutex.
	 */
	cfg80211_rx_assoc_resp(dev, req->bss, (const u8 *)&assoc_mgmt_frame,
			       sizeof(assoc_mgmt_frame), -1);
	return 0;
}

static struct ieee80211_mgmt deauth_mgmt_frame = {
	.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT
				     | IEEE80211_STYPE_DEAUTH),
	.duration = cpu_to_le16(1), /* ??? */
};

static int virt_wifi_deauth(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_deauth_request *req)
{
	wiphy_debug(wiphy, "deauth\n");
	memcpy(deauth_mgmt_frame.da, dev->dev_addr, dev->addr_len);
	memcpy(deauth_mgmt_frame.sa, fake_router_bssid,
	       sizeof(fake_router_bssid));
	memcpy(deauth_mgmt_frame.bssid, fake_router_bssid,
	       sizeof(fake_router_bssid));
	deauth_mgmt_frame.u.deauth.reason_code = cpu_to_le16(req->reason_code);
	/* Must call cfg80211_rx_mlme_mgmt to notify about the response to this.
	 * This must hold the mutex for the wedev while calling the function.
	 * Luckily the nl80211 code invoking this already holds that mutex.
	 */
	cfg80211_rx_mlme_mgmt(dev, (const u8 *)&deauth_mgmt_frame,
			      sizeof(auth_mgmt_frame));
	return 0;
}

static struct ieee80211_mgmt disassoc_mgmt_frame = {
	.frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT
				     | IEEE80211_STYPE_DISASSOC),
	.duration = cpu_to_le16(1), /* ??? */
};

static int virt_wifi_disassoc(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_disassoc_request *req)
{
	wiphy_debug(wiphy, "disassoc\n");
	memcpy(disassoc_mgmt_frame.da, dev->dev_addr, dev->addr_len);
	memcpy(disassoc_mgmt_frame.sa, fake_router_bssid,
	       sizeof(fake_router_bssid));
	memcpy(disassoc_mgmt_frame.bssid, fake_router_bssid,
	       sizeof(fake_router_bssid));
	disassoc_mgmt_frame.u.disassoc.reason_code =
		cpu_to_le16(req->reason_code);
	/* Must call cfg80211_rx_mlme_mgmt to notify about the response to this.
	 * This must hold the mutex for the wedev while calling the function.
	 * Luckily the nl80211 code invoking this already holds that mutex.
	 */
	cfg80211_rx_mlme_mgmt(dev, (const u8 *)&disassoc_mgmt_frame,
			      sizeof(auth_mgmt_frame));
	return 0;
}

static int virt_wifi_get_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac, struct station_info *sinfo)
{
	wiphy_debug(wiphy, "get_station\n");
	/* Only the values used by netlink_utils.cpp. */
	sinfo->filled = BIT(NL80211_STA_INFO_TX_PACKETS) |
		BIT(NL80211_STA_INFO_TX_FAILED) | BIT(NL80211_STA_INFO_SIGNAL) |
		BIT(NL80211_STA_INFO_TX_BITRATE);
	sinfo->tx_packets = 1;
	sinfo->tx_failed = 0;
	sinfo->signal = -1; /* -1 is the maximum signal strength, somehow. */
	sinfo->txrate = (struct rate_info) {
		.legacy = 10000, /* units are 100kbit/s */
	};
	return 0;
}

static const struct cfg80211_ops virt_wifi_cfg80211_ops = {
	.scan = virt_wifi_scan,

	.auth = virt_wifi_auth,
	.assoc = virt_wifi_assoc,
	.deauth = virt_wifi_deauth,
	.disassoc = virt_wifi_disassoc,

	.get_station = virt_wifi_get_station,
};

static struct wireless_dev *virtio_wireless_dev(struct device *device)
{
	struct wireless_dev *wdev;
	struct wiphy *wiphy;

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);

	if (!wdev)
		return ERR_PTR(-ENOMEM);

	wdev->iftype = NL80211_IFTYPE_STATION;
	wiphy = wiphy_new(&virt_wifi_cfg80211_ops, 0);

	if (!wiphy) {
		kfree(wdev);
		return ERR_PTR(-ENOMEM);
	}

	wdev->wiphy = wiphy;

	/* 100 SSIDs should be enough for anyone! */
	wiphy->max_scan_ssids = 101;
	wiphy->max_scan_ie_len = 1000;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	wiphy->bands[NL80211_BAND_5GHZ] = &band_5ghz;
	wiphy->bands[NL80211_BAND_60GHZ] = NULL;

	/* Don't worry about frequency regulations. */
	wiphy->regulatory_flags = REGULATORY_WIPHY_SELF_MANAGED;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_P2P_CLIENT) |
				     BIT(NL80211_IFTYPE_P2P_GO) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_MESH_POINT) |
				     BIT(NL80211_IFTYPE_MONITOR);
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
			    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
			    WIPHY_FLAG_AP_UAPSD |
			    WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR |
			       NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			       NL80211_FEATURE_STATIC_SMPS |
			       NL80211_FEATURE_DYNAMIC_SMPS |
			       NL80211_FEATURE_AP_SCAN |
			       NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	set_wiphy_dev(wiphy, device);

	return wdev;
}

struct virt_wifi_netdev_priv {
	struct net_device *lowerdev;
	struct net_device *upperdev;
	struct work_struct register_wiphy_work;
};

static netdev_tx_t virt_wifi_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	skb->dev = priv->lowerdev;
	return dev_queue_xmit(skb);
}

static const struct net_device_ops wifi_vlan_ops = {
	.ndo_start_xmit = virt_wifi_start_xmit,
};

static void free_wiphy(struct net_device *dev)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	flush_work(&priv->register_wiphy_work);
	if (dev->ieee80211_ptr && !IS_ERR(dev->ieee80211_ptr)) {
		if (dev->ieee80211_ptr->wiphy->registered)
			wiphy_unregister(dev->ieee80211_ptr->wiphy);
		wiphy_free(dev->ieee80211_ptr->wiphy);
		kfree(dev->ieee80211_ptr);
	}
}

static void virt_wifi_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &wifi_vlan_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = free_wiphy;
}

/* Called under rcu_read_lock() from netif_receive_skb */
static rx_handler_result_t virt_wifi_rx_handler(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct virt_wifi_netdev_priv *priv =
		rcu_dereference(skb->dev->rx_handler_data);

	/* macvlan uses GFP_ATOMIC here. */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb) {
		dev_err(&priv->upperdev->dev, "can't skb_share_check\n");
		return RX_HANDLER_CONSUMED;
	}

	*pskb = skb;
	skb->dev = priv->upperdev;
	skb->pkt_type = PACKET_HOST;
	return RX_HANDLER_ANOTHER;
}

static void virt_wifi_register_wiphy(struct work_struct *work)
{
	struct virt_wifi_netdev_priv *priv =
		container_of(work, struct virt_wifi_netdev_priv,
			     register_wiphy_work);
	struct wireless_dev *wdev = priv->upperdev->ieee80211_ptr;
	int err;

	err = wiphy_register(wdev->wiphy);
	if (err < 0) {
		dev_err(&priv->upperdev->dev, "can't wiphy_register (%d)\n",
			err);

		/* Roll back the net_device, it's not going to do wifi. */
		rtnl_lock();
		err = rtnl_delete_link(priv->upperdev);
		rtnl_unlock();

		/* rtnl_delete_link should only throw errors if it's not a
		 * netlink device, but we know here it is already a virt_wifi
		 * device.
		 */
		WARN_ONCE(err, "rtnl_delete_link failed on a virt_wifi device");
	}
}

/* Called with rtnl lock held. */
static int virt_wifi_newlink(struct net *src_net, struct net_device *dev,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);
	int err;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	priv->upperdev = dev;
	priv->lowerdev = __dev_get_by_index(src_net,
					    nla_get_u32(tb[IFLA_LINK]));

	if (!priv->lowerdev)
		return -ENODEV;
	if (!tb[IFLA_MTU])
		dev->mtu = priv->lowerdev->mtu;
	else if (dev->mtu > priv->lowerdev->mtu)
		return -EINVAL;

	err = netdev_rx_handler_register(priv->lowerdev, virt_wifi_rx_handler,
					 priv);
	if (err != 0) {
		dev_err(&priv->lowerdev->dev,
			"can't netdev_rx_handler_register: %ld\n",
			PTR_ERR(dev->ieee80211_ptr));
		return err;
	}

	eth_hw_addr_inherit(dev, priv->lowerdev);
	netif_stacked_transfer_operstate(priv->lowerdev, dev);

	SET_NETDEV_DEV(dev, &priv->lowerdev->dev);
	dev->ieee80211_ptr = virtio_wireless_dev(&priv->lowerdev->dev);

	if (IS_ERR(dev->ieee80211_ptr)) {
		dev_err(&priv->lowerdev->dev, "can't init wireless: %ld\n",
			PTR_ERR(dev->ieee80211_ptr));
		return PTR_ERR(dev->ieee80211_ptr);
	}

	err = register_netdevice(dev);
	if (err) {
		dev_err(&priv->lowerdev->dev, "can't register_netdevice: %d\n",
			err);
		goto remove_handler;
	}

	err = netdev_upper_dev_link(priv->lowerdev, dev);
	if (err) {
		dev_err(&priv->lowerdev->dev, "can't netdev_upper_dev_link: %d\n",
			err);
		goto unregister_netdev;
	}

	/* The newlink callback is invoked while holding the rtnl lock, but
	 * register_wiphy wants to claim the rtnl lock itself.
	 */
	INIT_WORK(&priv->register_wiphy_work, virt_wifi_register_wiphy);
	schedule_work(&priv->register_wiphy_work);

	return 0;
remove_handler:
	netdev_rx_handler_unregister(priv->lowerdev);
unregister_netdev:
	unregister_netdevice(dev);

	return err;
}

/** Called with rtnl lock held. */
static void virt_wifi_dellink(struct net_device *dev,
			      struct list_head *head)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	netdev_rx_handler_unregister(priv->lowerdev);
	netdev_upper_dev_unlink(priv->lowerdev, dev);

	unregister_netdevice_queue(dev, head);

	/* Deleting the wiphy is handled in the netdev destructor. */
}

static struct rtnl_link_ops virt_wifi_link_ops = {
	.kind		= "virt_wifi",
	.setup		= virt_wifi_setup,
	.newlink	= virt_wifi_newlink,
	.dellink	= virt_wifi_dellink,
	.priv_size	= sizeof(struct virt_wifi_netdev_priv),
};

static int __init virt_wifi_init_module(void)
{
	return rtnl_link_register(&virt_wifi_link_ops);
}

static void __exit virt_wifi_cleanup_module(void)
{
	rtnl_link_unregister(&virt_wifi_link_ops);
}

module_init(virt_wifi_init_module);
module_exit(virt_wifi_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("A. Cody Schuffelen <schuffelen@google.com>");
MODULE_DESCRIPTION("Driver for a wireless wrapper of ethernet devices");
MODULE_ALIAS_RTNL_LINK("virt_wifi");
