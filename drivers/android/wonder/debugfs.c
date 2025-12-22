// SPDX-License-Identifier: GPL-2.0
/*
 * Google Wonder WiFi Virtual Soft-MAC Driver
 *
 * Debugfs implementation for the Wonder driver.
 */
#define pr_fmt(fmt) "[wonder][debugfs] " fmt
#define LOG_MODULE_NAME "debugfs"

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#include "core.h"
#include "mac80211.h"

static int wonder_freq_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_set_freq_params *freq = &wonder->wondertap_data.cached_freq;

	seq_printf(m, "%u\n", freq->freq);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_freq);

static int wonder_bandwidth_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_set_freq_params *freq = &wonder->wondertap_data.cached_freq;

	seq_printf(m, "%u\n", freq->bandwidth);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_bandwidth);

static int wonder_tx_rate_preamble_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_fixed_tx_rate_params *rate = &wonder->wondertap_data.cached_tx_rate;

	seq_printf(m, "%u\n", rate->preamble);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_tx_rate_preamble);

static int wonder_tx_rate_mcs_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_fixed_tx_rate_params *rate = &wonder->wondertap_data.cached_tx_rate;

	seq_printf(m, "%u\n", rate->mcs);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_tx_rate_mcs);

static int wonder_tx_rate_gi_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_fixed_tx_rate_params *rate = &wonder->wondertap_data.cached_tx_rate;

	seq_printf(m, "%u\n", rate->gi);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_tx_rate_gi);

static int wonder_tx_rate_bw_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_fixed_tx_rate_params *rate = &wonder->wondertap_data.cached_tx_rate;

	seq_printf(m, "%u\n", rate->bw);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_tx_rate_bw);

static int wonder_country_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;

	seq_printf(m, "%.2s\n", wonder->wondertap_data.cached_country_code);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_country);

static int wonder_mac_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	u8 *addr = wonder->wondertap_data.mac_addr;

	seq_printf(m, "%02x:XX:XX:XX:XX:%02x\n", addr[0], addr[5]);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_mac);

static int wonder_bssid_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	u8 *addr = wonder->wondertap_data.cached_bssid;

	seq_printf(m, "%02x:XX:XX:XX:XX:%02x\n", addr[0], addr[5]);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wonder_bssid);

static ssize_t wonder_force_stop_tx_write(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct wonder_data *wonder = file->private_data;
	bool stop;
	int ret;

	ret = kstrtobool_from_user(user_buf, count, &stop);
	if (ret)
		return ret;

	if (!wonder || !wonder->vdev) {
		pr_err("vdev not available\n");
		return -ENODEV;
	}

	if (stop) {
		wonder->tx_stop = true;
	} else {
		wonder->tx_stop = false;
	}

	return count;
}

static const struct file_operations fops_force_stop_tx = {
	.write = wonder_force_stop_tx_write,
	.open = simple_open,
	.owner = THIS_MODULE,
};

int wonder_debugfs_init(void *wonder)
{
	struct dentry *wonder_debugfs_root;

	wonder_debugfs_root = debugfs_create_dir("wonder", NULL);
	if (!wonder_debugfs_root)
		return -ENODEV;

	debugfs_create_file("force_stop_tx", 0200, wonder_debugfs_root,
			    wonder, &fops_force_stop_tx);
	debugfs_create_file("freq", 0400, wonder_debugfs_root, wonder, &wonder_freq_fops);
	debugfs_create_file("bandwidth", 0400, wonder_debugfs_root, wonder, &wonder_bandwidth_fops);
	debugfs_create_file("tx_rate_preamble", 0400, wonder_debugfs_root, wonder,
			    &wonder_tx_rate_preamble_fops);
	debugfs_create_file("tx_rate_mcs", 0400, wonder_debugfs_root, wonder,
			    &wonder_tx_rate_mcs_fops);
	debugfs_create_file("tx_rate_gi", 0400, wonder_debugfs_root, wonder,
			    &wonder_tx_rate_gi_fops);
	debugfs_create_file("tx_rate_bw", 0400, wonder_debugfs_root, wonder,
			    &wonder_tx_rate_bw_fops);
	debugfs_create_file("country_code", 0400, wonder_debugfs_root, wonder, &wonder_country_fops);
	debugfs_create_file("mac_addr", 0400, wonder_debugfs_root, wonder, &wonder_mac_fops);
	debugfs_create_file("bssid", 0400, wonder_debugfs_root, wonder, &wonder_bssid_fops);

	return 0;
}

void wonder_debugfs_exit(void)
{
	debugfs_lookup_and_remove("wonder", NULL);
}
