// SPDX-License-Identifier: GPL-2.0-only
/*
 * core.c - Core implementation of the net_apf genetlink family
 *
 * Copyright 2026 Google LLC
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/apf.h>
#include <linux/apf_interpreter.h>
#include <uapi/linux/net_apf.h>

static struct genl_family apf_family;

/*
 * Command handler implementations
 */
static int apf_get_info(struct sk_buff *skb, struct genl_info *info)
{
	struct apf_info apf_info = {};
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	int ret;

	if (!info->attrs[NET_APF_A_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_info) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->get_info(dev, &apf_info);
	if (ret)
		goto out_put_dev;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NET_APF_CMD_GET_INFO);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, NET_APF_A_INFO_VERSION, apf_info.apf_version) ||
	    nla_put_u32(msg, NET_APF_A_INFO_ID, apf_info.apf_id) ||
	    nla_put_u32(msg, NET_APF_A_INFO_TOTAL_RAM, apf_info.total_ram) ||
	    nla_put_u32(msg, NET_APF_A_INFO_USED_RAM, apf_info.used_ram) ||
	    nla_put_u32(msg, NET_APF_A_INFO_OVERHEAD, apf_info.overhead) ||
	    nla_put_u32(msg, NET_APF_A_INFO_GRANULARITY, apf_info.granularity)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_set_id(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;
	u32 id;

	if (!info->attrs[NET_APF_A_IFINDEX] || !info->attrs[NET_APF_A_ID]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index and ID required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->set_id) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF set_id not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	id = nla_get_u32(info->attrs[NET_APF_A_ID]);
	ret = dev->netdev_ops->apf_ops->set_id(dev, id);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_enable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	u32 ram_size;
	int ret;

	if (!info->attrs[NET_APF_A_IFINDEX] || !info->attrs[NET_APF_A_RAM_SIZE]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index and RAM size required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->enable) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF enable not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ram_size = nla_get_u32(info->attrs[NET_APF_A_RAM_SIZE]);
	ret = dev->netdev_ops->apf_ops->enable(dev, ram_size);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_disable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	if (!info->attrs[NET_APF_A_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->disable) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF disable not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->disable(dev);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_get_ram_size(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	u32 ram_size;
	void *hdr;
	int ret;

	if (!info->attrs[NET_APF_A_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_ram_size) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF get_ram_size not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ram_size = dev->netdev_ops->apf_ops->get_ram_size(dev);

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NET_APF_CMD_GET_RAM_SIZE);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, NET_APF_A_RAM_SIZE, ram_size)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_put_dev:
	dev_put(dev);
	return ret;

out_free:
	nlmsg_free(msg);
	dev_put(dev);
	return ret;
}

static int apf_read(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	u32 offset, len;
	void *hdr;
	int ret;
	u8 *buf;

	if (!info->attrs[NET_APF_A_IFINDEX] ||
	    !info->attrs[NET_APF_A_READ_OFFSET] ||
	    !info->attrs[NET_APF_A_DATA_LEN]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, read offset, and data length required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->read) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF read not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	offset = nla_get_u32(info->attrs[NET_APF_A_READ_OFFSET]);
	len = nla_get_u32(info->attrs[NET_APF_A_DATA_LEN]);
	if (!len || len > NET_APF_MAX_BUFFER_LEN) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid read length");
		ret = -EINVAL;
		goto out_put_dev;
	}

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->read(dev, offset, buf, len);
	if (ret)
		goto out_free;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE + nla_total_size(len), GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_free;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NET_APF_CMD_READ);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	if (nla_put(msg, NET_APF_A_DATA, len, buf)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_free:
	kfree(buf);
out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_write(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	const u8 *buf;
	s32 offset;
	int ret;
	u32 len;

	if (!info->attrs[NET_APF_A_IFINDEX] ||
	    !info->attrs[NET_APF_A_WRITE_OFFSET] ||
	    !info->attrs[NET_APF_A_DATA]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, write offset, and data required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->write) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF write not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	offset = nla_get_s32(info->attrs[NET_APF_A_WRITE_OFFSET]);
	if (offset < NET_APF_PROGRAM_OFFSET) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid offset");
		ret = -EINVAL;
		goto out_put_dev;
	}

	buf = nla_data(info->attrs[NET_APF_A_DATA]);
	len = nla_len(info->attrs[NET_APF_A_DATA]);
	if (!len || len > NET_APF_MAX_BUFFER_LEN) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid data length");
		ret = -EINVAL;
		goto out_put_dev;
	}

	if (info->attrs[NET_APF_A_DATA_LEN] &&
	    nla_get_u32(info->attrs[NET_APF_A_DATA_LEN]) != len) {
		NL_SET_ERR_MSG_MOD(info->extack, "Data length mismatch");
		ret = -EINVAL;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->write(dev, offset, buf, len);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_set_fast_path(struct sk_buff *skb, struct genl_info *info)
{
	bool enable_ipv6_fastpath;
	struct net_device *dev;
	const u8 *ucast_mac;
	__be32 ucast_addr4;
	s16 vlan_tag;
	int ret;

	if (!info->attrs[NET_APF_A_IFINDEX] ||
	    !info->attrs[NET_APF_A_FP_UCAST_MAC] ||
	    !info->attrs[NET_APF_A_FP_VLAN_TAG] ||
	    !info->attrs[NET_APF_A_FP_UCAST_ADDR4] ||
	    !info->attrs[NET_APF_A_FP_ENABLE_IPV6]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, MAC, VLAN tag, IPv4 address, and IPv6 fastpath required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NET_APF_A_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->fast_path) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF fast_path not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ucast_mac = nla_data(info->attrs[NET_APF_A_FP_UCAST_MAC]);
	vlan_tag = nla_get_s16(info->attrs[NET_APF_A_FP_VLAN_TAG]);
	ucast_addr4 = nla_get_be32(info->attrs[NET_APF_A_FP_UCAST_ADDR4]);
	enable_ipv6_fastpath = nla_get_u8(info->attrs[NET_APF_A_FP_ENABLE_IPV6]);

	ret = dev->netdev_ops->apf_ops->fast_path(dev, ucast_mac, vlan_tag,
						  ucast_addr4, enable_ipv6_fastpath);

out_put_dev:
	dev_put(dev);
	return ret;
}

/*
 * Attribute policy definitions
 */
static const struct nla_policy apf_policy[NET_APF_A_MAX + 1] = {
	[NET_APF_A_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[NET_APF_A_INFO_VERSION] = { .type = NLA_U32 },
	[NET_APF_A_INFO_ID] = { .type = NLA_U32 },
	[NET_APF_A_INFO_TOTAL_RAM] = { .type = NLA_U32 },
	[NET_APF_A_INFO_USED_RAM] = { .type = NLA_U32 },
	[NET_APF_A_INFO_OVERHEAD] = { .type = NLA_U32 },
	[NET_APF_A_INFO_GRANULARITY] = { .type = NLA_U32 },
	[NET_APF_A_ID] = { .type = NLA_U32 },
	[NET_APF_A_RAM_SIZE] = { .type = NLA_U32 },
	[NET_APF_A_READ_OFFSET] = { .type = NLA_U32 },
	[NET_APF_A_WRITE_OFFSET] = { .type = NLA_S32 },
	[NET_APF_A_DATA] = { .type = NLA_BINARY, .len = NET_APF_MAX_BUFFER_LEN },
	[NET_APF_A_DATA_LEN] = { .type = NLA_U32 },
	[NET_APF_A_FP_UCAST_MAC] = NLA_POLICY_ETH_ADDR,
	[NET_APF_A_FP_VLAN_TAG] = { .type = NLA_S16 },
	[NET_APF_A_FP_UCAST_ADDR4] = { .type = NLA_BE32 },
	[NET_APF_A_FP_ENABLE_IPV6] = { .type = NLA_U8 },
};

/*
 * Command definitions
 */
static const struct genl_ops apf_ops_list[] = {
	{
		.cmd = NET_APF_CMD_GET_INFO,
		.doit = apf_get_info,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM, // Requires CAP_NET_ADMIN
	},
	{
		.cmd = NET_APF_CMD_SET_ID,
		.doit = apf_set_id,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_ENABLE,
		.doit = apf_enable,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_DISABLE,
		.doit = apf_disable,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_GET_RAM_SIZE,
		.doit = apf_get_ram_size,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_READ,
		.doit = apf_read,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_WRITE,
		.doit = apf_write,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NET_APF_CMD_SET_FAST_PATH,
		.doit = apf_set_fast_path,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

/*
 * Genetlink family definition
 */
static struct genl_family apf_family = {
	.name = NET_APF_FAMILY_NAME,
	.version = NET_APF_FAMILY_VERSION,
	.maxattr = NET_APF_A_MAX,
	.ops = apf_ops_list,
	.n_ops = ARRAY_SIZE(apf_ops_list),
	.module = THIS_MODULE,
};

/*
 * This is called by the APF interpreter.
 * The 'ctx' is the net_device pointer passed from the driver's call to apf_run.
 */
uint8_t *apf_allocate_buffer(void *ctx, uint32_t size)
{
	/*
	 * Allocate buffer for APF packet construction.
	 */
	return kmalloc(size, GFP_ATOMIC);
}
EXPORT_SYMBOL(apf_allocate_buffer);

/*
 * This is called by the APF interpreter.
 */
int apf_transmit_buffer(void *ctx, uint8_t *ptr, uint32_t len, uint8_t dscp)
{
	struct net_device *dev = (struct net_device *)ctx;
	struct sk_buff *skb;
	struct ethhdr *eth;

	if (len == 0) {
		kfree(ptr);
		return 0;
	}

	/*
	 * Create an SKB around the buffer. We assume the interpreter has
	 * constructed a full L2 frame.
	 */
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		kfree(ptr);
		return -ENOMEM;
	}

	memcpy(skb_put(skb, len), ptr, len);
	kfree(ptr); /* Free the original buffer */

	skb_reset_mac_header(skb);
	skb->dev = dev;

	eth = (struct ethhdr *)skb->data;
	skb->protocol = eth->h_proto;

	if (dev_queue_xmit(skb) < 0) {
		netdev_warn(dev, "%s: failed to transmit skb\n", __func__);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(apf_transmit_buffer);

/*
 * Module initialization and exit
 */
static int __init apf_init(void)
{
	return genl_register_family(&apf_family);
}

static void __exit apf_exit(void)
{
	genl_unregister_family(&apf_family);
}

module_init(apf_init);
module_exit(apf_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Advanced Packet Filter (APF) subsystem");
