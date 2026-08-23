// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core implementation of the 'net_apf' genetlink family
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

static int apf_get_info(struct sk_buff *skb, struct genl_info *info)
{
	struct apf_info apf_info = {};
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	int ret;

	if (!info->attrs[APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_info) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->get_info(dev, info->extack, &apf_info);
	if (ret)
		goto out_put_dev;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  APF_CMD_GET_INFO);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, APF_ATTR_INFO_VERSION, apf_info.apf_version) ||
	    nla_put_u32(msg, APF_ATTR_INFO_ID, apf_info.apf_id) ||
	    nla_put_u32(msg, APF_ATTR_INFO_TOTAL_RAM, apf_info.total_ram) ||
	    nla_put_u32(msg, APF_ATTR_INFO_USED_RAM, apf_info.used_ram) ||
	    nla_put_u32(msg, APF_ATTR_INFO_OVERHEAD, apf_info.overhead) ||
	    nla_put_u32(msg, APF_ATTR_INFO_GRANULARITY, apf_info.granularity)) {
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

	if (!info->attrs[APF_ATTR_IFINDEX] || !info->attrs[APF_ATTR_ID]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index and ID required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->set_id) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF set_id not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	id = nla_get_u32(info->attrs[APF_ATTR_ID]);
	ret = dev->netdev_ops->apf_ops->set_id(dev, info->extack, id);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_enable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	u32 ram_size;
	int ret;

	if (!info->attrs[APF_ATTR_IFINDEX] || !info->attrs[APF_ATTR_RAM_SIZE]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index and RAM size required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->enable) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF enable not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ram_size = nla_get_u32(info->attrs[APF_ATTR_RAM_SIZE]);
	ret = dev->netdev_ops->apf_ops->enable(dev, info->extack, ram_size);

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

	if (!info->attrs[APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_ram_size) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF get_ram_size not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ram_size = dev->netdev_ops->apf_ops->get_ram_size(dev, info->extack);

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  APF_CMD_GET_RAM_SIZE);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, APF_ATTR_RAM_SIZE, ram_size)) {
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

	if (!info->attrs[APF_ATTR_IFINDEX] ||
	    !info->attrs[APF_ATTR_READ_OFFSET] ||
	    !info->attrs[APF_ATTR_DATA_LEN]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, read offset, and data length required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->read) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF read not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	offset = nla_get_u32(info->attrs[APF_ATTR_READ_OFFSET]);
	len = nla_get_u32(info->attrs[APF_ATTR_DATA_LEN]);
	if (!len || len > APF_MAX_BUFFER_LEN) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid read length");
		ret = -EINVAL;
		goto out_put_dev;
	}

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->read(dev, info->extack, offset, buf, len);
	if (ret)
		goto out_free;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE + nla_total_size(len), GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_free;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  APF_CMD_READ);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	if (nla_put(msg, APF_ATTR_DATA, len, buf)) {
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

	if (!info->attrs[APF_ATTR_IFINDEX] ||
	    !info->attrs[APF_ATTR_WRITE_OFFSET] ||
	    !info->attrs[APF_ATTR_DATA]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, write offset, and data required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->write) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF write not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	offset = nla_get_s32(info->attrs[APF_ATTR_WRITE_OFFSET]);
	if (offset < APF_PROGRAM_OFFSET) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid offset");
		ret = -EINVAL;
		goto out_put_dev;
	}

	buf = nla_data(info->attrs[APF_ATTR_DATA]);
	len = nla_len(info->attrs[APF_ATTR_DATA]);
	if (!len || len > APF_MAX_BUFFER_LEN) {
		NL_SET_ERR_MSG_MOD(info->extack, "Invalid data length");
		ret = -EINVAL;
		goto out_put_dev;
	}

	if (info->attrs[APF_ATTR_DATA_LEN] &&
	    nla_get_u32(info->attrs[APF_ATTR_DATA_LEN]) != len) {
		NL_SET_ERR_MSG_MOD(info->extack, "Data length mismatch");
		ret = -EINVAL;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->write(dev, info->extack, offset, buf, len);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_disable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	if (!info->attrs[APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->disable) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF disable not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->disable(dev, info->extack);

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

	if (!info->attrs[APF_ATTR_IFINDEX] ||
	    !info->attrs[APF_ATTR_FP_UCAST_MAC] ||
	    !info->attrs[APF_ATTR_FP_VLAN_TAG] ||
	    !info->attrs[APF_ATTR_FP_UCAST_ADDR4] ||
	    !info->attrs[APF_ATTR_FP_ENABLE_IPV6]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Interface index, MAC, VLAN tag, IPv4 address, and IPv6 fastpath required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->fast_path) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF fast_path not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ucast_mac = nla_data(info->attrs[APF_ATTR_FP_UCAST_MAC]);
	vlan_tag = nla_get_s16(info->attrs[APF_ATTR_FP_VLAN_TAG]);
	ucast_addr4 = nla_get_be32(info->attrs[APF_ATTR_FP_UCAST_ADDR4]);
	enable_ipv6_fastpath = nla_get_u8(info->attrs[APF_ATTR_FP_ENABLE_IPV6]);

	ret = dev->netdev_ops->apf_ops->fast_path(dev, info->extack, ucast_mac,
						  vlan_tag, ucast_addr4,
						  enable_ipv6_fastpath);

out_put_dev:
	dev_put(dev);
	return ret;
}

static const struct nla_policy apf_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_INFO_VERSION] = { .type = NLA_REJECT },
	[APF_ATTR_INFO_ID] = { .type = NLA_REJECT },
	[APF_ATTR_INFO_TOTAL_RAM] = { .type = NLA_REJECT },
	[APF_ATTR_INFO_USED_RAM] = { .type = NLA_REJECT },
	[APF_ATTR_INFO_OVERHEAD] = { .type = NLA_REJECT },
	[APF_ATTR_INFO_GRANULARITY] = { .type = NLA_REJECT },
	[APF_ATTR_ID] = { .type = NLA_U32 },
	[APF_ATTR_RAM_SIZE] = { .type = NLA_U32 },
	[APF_ATTR_READ_OFFSET] = { .type = NLA_U32 },
	[APF_ATTR_WRITE_OFFSET] = { .type = NLA_S32 },
	[APF_ATTR_DATA] = { .type = NLA_BINARY, .len = APF_MAX_BUFFER_LEN },
	[APF_ATTR_DATA_LEN] = { .type = NLA_U32 },
	[APF_ATTR_FP_UCAST_MAC] = NLA_POLICY_ETH_ADDR,
	[APF_ATTR_FP_VLAN_TAG] = { .type = NLA_S16 },
	[APF_ATTR_FP_UCAST_ADDR4] = { .type = NLA_BE32 },
	[APF_ATTR_FP_ENABLE_IPV6] = { .type = NLA_U8 },
};

static const struct genl_ops apf_ops_list[] = {
	{
		.cmd = APF_CMD_GET_INFO,
		.doit = apf_get_info,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_SET_ID,
		.doit = apf_set_id,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_ENABLE,
		.doit = apf_enable,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_GET_RAM_SIZE,
		.doit = apf_get_ram_size,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_READ,
		.doit = apf_read,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_WRITE,
		.doit = apf_write,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_DISABLE,
		.doit = apf_disable,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_SET_FAST_PATH,
		.doit = apf_set_fast_path,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family apf_family = {
	.name = APF_FAMILY_NAME,
	.version = APF_FAMILY_VERSION,
	.maxattr = APF_ATTR_MAX,
	.ops = apf_ops_list,
	.n_ops = ARRAY_SIZE(apf_ops_list),
	.module = THIS_MODULE,
};

// Called by the APF interpreter to allocate buffer for packet construction
uint8_t *apf_allocate_buffer(void *ctx, uint32_t size)
{
	return kmalloc(size, GFP_ATOMIC);
}
EXPORT_SYMBOL(apf_allocate_buffer);

// Called by the APF interpreter to transmit a constructed packet
int apf_transmit_buffer(void *ctx, uint8_t *ptr, uint32_t len, uint8_t dscp)
{
	struct net_device *dev = (struct net_device *)ctx;
	struct sk_buff *skb;
	struct ethhdr *eth;

	if (len == 0) {
		kfree(ptr);
		return 0;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		kfree(ptr);
		return -ENOMEM;
	}

	memcpy(skb_put(skb, len), ptr, len);
	kfree(ptr);

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
