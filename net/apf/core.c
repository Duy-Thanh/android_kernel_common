// SPDX-License-Identifier: GPL-2.0-only
/*
 * core.c - Core implementation of the nl_apf genetlink family
 *
 * Copyright 2024-2026 Google LLC
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/apf.h>
#include <uapi/linux/nl_apf.h>

/*
 * Forward declarations for our command handlers.
 */
static int apf_get_caps(struct sk_buff *skb, struct genl_info *info);
static int apf_set_filter(struct sk_buff *skb, struct genl_info *info);
static int apf_get_filter(struct sk_buff *skb, struct genl_info *info);
static int apf_set_fast_path(struct sk_buff *skb, struct genl_info *info);

/*
 * Attribute policy definitions
 */
static const struct nla_policy apf_policy[NL_APF_ATTR_MAX + 1] = {
	[NL_APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[NL_APF_ATTR_PROGRAM] = { .type = NLA_BINARY, .len = NL_APF_MAX_PROGRAM_LEN },
	[NL_APF_ATTR_FLAGS] = { .type = NLA_U32 },
	[NL_APF_ATTR_UCAST_MAC] = NLA_POLICY_ETH_ADDR,
	[NL_APF_ATTR_VLAN_TAG] = { .type = NLA_S16 },
	[NL_APF_ATTR_UCAST_ADDR4] = { .type = NLA_BE32 },
	[NL_APF_ATTR_ENABLE_IPV6_FASTPATH] = { .type = NLA_U8 },
};

/*
 * Command definitions
 */
static const struct genl_ops apf_ops_list[] = {
	{
		.cmd = NL_APF_CMD_GET_CAPABILITIES,
		.doit = apf_get_caps,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN */
	},
	{
		.cmd = NL_APF_CMD_SET_FILTER,
		.doit = apf_set_filter,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_APF_CMD_GET_FILTER,
		.doit = apf_get_filter,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL_APF_CMD_SET_FAST_PATH,
		.doit = apf_set_fast_path,
		.policy = apf_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

/*
 * Genetlink family definition
 */
static struct genl_family apf_family = {
	.name = NL_APF_FAMILY_NAME,
	.version = NL_APF_VERSION,
	.maxattr = NL_APF_ATTR_MAX,
	.ops = apf_ops_list,
	.n_ops = ARRAY_SIZE(apf_ops_list),
	.module = THIS_MODULE,
};

/*
 * Command handler implementations
 */
static int apf_get_caps(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	u32 version, max_program_size, flags;
	int ret;

	if (!info->attrs[NL_APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_caps) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->get_caps(dev, &version, &max_program_size, &flags);
	if (ret)
		goto out_put_dev;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NL_APF_CMD_GET_CAPABILITIES);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	if (nla_put_u32(msg, NL_APF_ATTR_VERSION, version) ||
	    nla_put_u32(msg, NL_APF_ATTR_MAX_PROGRAM_SIZE, max_program_size) ||
	    nla_put_u32(msg, NL_APF_ATTR_FLAGS, flags)) {
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

static int apf_set_filter(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	const u8 *program;
	u32 len, flags = 0;
	int ret;

	if (!info->attrs[NL_APF_ATTR_IFINDEX] ||
	    !info->attrs[NL_APF_ATTR_PROGRAM]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index and program required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->set_filter) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF set_filter not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	program = nla_data(info->attrs[NL_APF_ATTR_PROGRAM]);
	len = nla_len(info->attrs[NL_APF_ATTR_PROGRAM]);
	if (info->attrs[NL_APF_ATTR_FLAGS])
		flags = nla_get_u32(info->attrs[NL_APF_ATTR_FLAGS]);

	ret = dev->netdev_ops->apf_ops->set_filter(dev, program, len, flags);
	if (ret)
		NL_SET_ERR_MSG_MOD(info->extack, "Driver failed to install APF filter");

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_get_filter(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *msg;
	void *hdr;
	u8 *program;
	u32 len = 0, version, max_len = 0, flags;
	int ret;

	if (!info->attrs[NL_APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->get_caps ||
	    !dev->netdev_ops->apf_ops->get_filter) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF get_filter not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->get_caps(dev, &version, &max_len, &flags);
	if (ret || !max_len || max_len > NL_APF_MAX_PROGRAM_LEN) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "Failed to get APF capabilities or invalid size");
		ret = ret ? ret : -EINVAL;
		goto out_put_dev;
	}

	program = kmalloc(max_len, GFP_KERNEL);
	if (!program) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	len = max_len;
	ret = dev->netdev_ops->apf_ops->get_filter(dev, program, &len);
	if (ret) {
		NL_SET_ERR_MSG_MOD(info->extack, "Driver failed to retrieve APF filter");
		goto out_free;
	}

	if (len > max_len) {
		ret = -EINVAL;
		goto out_free;
	}

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE + nla_total_size(len), GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto out_free;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &apf_family, 0,
			  NL_APF_CMD_GET_FILTER);
	if (!hdr) {
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	if (nla_put(msg, NL_APF_ATTR_PROGRAM, len, program) ||
	    nla_put_u32(msg, NL_APF_ATTR_PROGRAM_LEN, len)) {
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		ret = -EMSGSIZE;
		goto out_free;
	}

	genlmsg_end(msg, hdr);
	ret = genlmsg_reply(msg, info);

out_free:
	kfree(program);
out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_set_fast_path(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	u8 ucast_mac[ETH_ALEN] = { 0 };
	s16 vlan_tag = -1;
	__be32 ucast_addr4 = 0;
	bool enable_ipv6_fastpath = false;
	int ret;

	if (!info->attrs[NL_APF_ATTR_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(info->extack, "Interface index required");
		return -EINVAL;
	}

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[NL_APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return -ENODEV;
	}

	if (!dev->netdev_ops->apf_ops || !dev->netdev_ops->apf_ops->fast_path) {
		NL_SET_ERR_MSG_MOD(info->extack, "APF fast_path not supported on device");
		ret = -EOPNOTSUPP;
		goto out_put_dev;
	}

	if (info->attrs[NL_APF_ATTR_UCAST_MAC])
		memcpy(ucast_mac, nla_data(info->attrs[NL_APF_ATTR_UCAST_MAC]), ETH_ALEN);
	else
		ether_addr_copy(ucast_mac, dev->dev_addr);

	if (info->attrs[NL_APF_ATTR_VLAN_TAG])
		vlan_tag = nla_get_s16(info->attrs[NL_APF_ATTR_VLAN_TAG]);

	if (info->attrs[NL_APF_ATTR_UCAST_ADDR4])
		ucast_addr4 = nla_get_be32(info->attrs[NL_APF_ATTR_UCAST_ADDR4]);

	if (info->attrs[NL_APF_ATTR_ENABLE_IPV6_FASTPATH])
		enable_ipv6_fastpath = !!nla_get_u8(info->attrs[NL_APF_ATTR_ENABLE_IPV6_FASTPATH]);

	ret = dev->netdev_ops->apf_ops->fast_path(dev, ucast_mac, vlan_tag,
						  ucast_addr4, enable_ipv6_fastpath);
	if (ret)
		NL_SET_ERR_MSG_MOD(info->extack, "Driver failed to configure APF fast path");

out_put_dev:
	dev_put(dev);
	return ret;
}

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
