/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google Wonder WiFi Virtual Soft-MAC Driver
 *
 * Main entry point for the Wonder driver module. This file handles the
 * initialization and cleanup of the driver, locating the physical
 * network device and kicking off the mac80211 registration process.
 */

#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/component.h>
#include <linux/android/wondertap.h>

#include "core.h"
#include "mac80211.h"
#include "wondertap_internal.h"

/* Module parameter for setting the physical device name */
module_param(physical_name, charp, 0444);
MODULE_PARM_DESC(physical_name, "Interface name to use (e.g., wlan0, radiotap0, ...)");

static int wonder_probe(struct auxiliary_device *adev,
			const struct auxiliary_device_id *id)
{
	struct wonder_data *wonder;
	struct wondertap_data *wondertap;
	struct wondertap_priv *wlan_priv;
	struct device *dev = &adev->dev;

	wonder = wonder_mac80211_init();
	if (!wonder)
		return -ENODEV;

	wondertap = &wonder->wondertap_data;
	/* Assign wondertap interface version will be used in the match process. */
	wondertap->ver = WONDER_VERSION_1_4;

	wlan_priv = dev_get_drvdata(dev);
	if (!wlan_priv || !wlan_priv->wonder_ops) {
		dev_err(dev, "Missing wonder_ops in aux device\n");
		wonder_mac80211_exit(wonder);
		return -EINVAL;
	}

	auxiliary_set_drvdata(adev, wonder);

	if (wlan_priv->ver != wondertap->ver) {
		dev_err(dev, "%s(): wondertap interface version mismatch(%d,%d)!\n",
			__func__, wlan_priv->ver, wondertap->ver);
		wonder_mac80211_exit(wonder);
		return -EINVAL;
	}

	/* All matched, hook the ops to wondertap interface. */
	wondertap->wonder_ops = wlan_priv->wonder_ops;
	dev_info(dev, "%s(): Connected to wlan ver %d!\n", __func__, wlan_priv->ver);

	return wonder_debugfs_init(wonder);
}

static void wonder_remove(struct auxiliary_device *adev)
{
	struct wonder_data *wonder = auxiliary_get_drvdata(adev);

	wonder_debugfs_exit();
	wonder_mac80211_exit(wonder);
}

static const struct auxiliary_device_id wonder_aux_id_table[] = {
	{ .name = "bcmdhd4390.wondertap" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, wonder_aux_id_table);

static struct auxiliary_driver wonder_driver = {
	.probe = wonder_probe,
	.remove = wonder_remove,
	.driver = {
		.name = "wonder",
	},
	.id_table = wonder_aux_id_table,
};
module_auxiliary_driver(wonder_driver);

MODULE_DESCRIPTION("Google Wonder Virtual mac80211 Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google Android WiFi Team");
