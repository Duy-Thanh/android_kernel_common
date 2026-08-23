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

enum apf_mode {
	APF_MODE_INVALID = 0,
	APF_MODE_V6_1,          // get_info, get_ram_size, read, write
	APF_MODE_V6_1_FASTPATH, // get_info, get_ram_size, read, write, fast_path
	APF_MODE_V8,            // all 8 methods
};

static enum apf_mode apf_get_mode(const struct apf_ops *ops,
				  struct netlink_ext_ack *extack)
{
	if (!ops) {
		NL_SET_ERR_MSG_MOD(extack, "APF not supported on device");
		return APF_MODE_INVALID;
	}

	if (!ops->get_info || !ops->get_ram_size || !ops->read || !ops->write) {
		NL_SET_ERR_MSG_MOD(extack, "Incomplete APF driver ops on device");
		return APF_MODE_INVALID;
	}

	// Combination 1: APFv8 (all 8 methods implemented)
	if (ops->set_id && ops->enable && ops->disable && ops->fast_path)
		return APF_MODE_V8;

	// Combinations 2 & 3: APFv6.1 (v8 lifecycle methods must be NULL)
	if (!ops->set_id && !ops->enable && !ops->disable)
		return ops->fast_path ? APF_MODE_V6_1_FASTPATH : APF_MODE_V6_1;

	NL_SET_ERR_MSG_MOD(extack, "Invalid APF driver ops configuration");
	return APF_MODE_INVALID;
}

static struct net_device *apf_get_netdev(struct genl_info *info)
{
	struct net_device *dev;
	enum apf_mode mode;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_IFINDEX))
		return ERR_PTR(-EINVAL);

	dev = dev_get_by_index(genl_info_net(info),
			       nla_get_u32(info->attrs[APF_ATTR_IFINDEX]));
	if (!dev) {
		NL_SET_ERR_MSG_MOD(info->extack, "Device not found");
		return ERR_PTR(-ENODEV);
	}

	mode = apf_get_mode(dev->netdev_ops->apf_ops, info->extack);
	if (mode == APF_MODE_INVALID) {
		dev_put(dev);
		return ERR_PTR(-EOPNOTSUPP);
	}

	switch (info->genlhdr->cmd) {
	case APF_CMD_SET_ID:
	case APF_CMD_ENABLE:
	case APF_CMD_DISABLE:
		if (mode < APF_MODE_V8) {
			NL_SET_ERR_MSG_MOD(info->extack, "Command requires APFv8 support");
			dev_put(dev);
			return ERR_PTR(-EOPNOTSUPP);
		}
		break;
	case APF_CMD_SET_FAST_PATH:
		if (mode < APF_MODE_V6_1_FASTPATH) {
			NL_SET_ERR_MSG_MOD(info->extack, "Fast path not supported on device");
			dev_put(dev);
			return ERR_PTR(-EOPNOTSUPP);
		}
		break;
	default:
		break;
	}

	return dev;
}

static struct sk_buff *apf_genlmsg_new(struct genl_info *info, size_t payload, void **hdr)
{
	struct sk_buff *skb = genlmsg_new(NLMSG_DEFAULT_SIZE + payload, GFP_KERNEL);

	if (!skb) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to allocate netlink response message");
		return NULL;
	}

	*hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq, &apf_family, 0,
			   info->genlhdr->cmd);
	if (!*hdr) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to write generic netlink header");
		nlmsg_free(skb);
		return NULL;
	}

	return skb;
}

static int apf_get_info(struct sk_buff *in_skb, struct genl_info *info)
{
	struct apf_info apf_info = {};
	struct net_device *dev;
	struct sk_buff *skb;
	void *hdr;
	int ret;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->get_info(dev, info->extack, &apf_info);
	if (ret)
		goto out_put_dev;

	skb = apf_genlmsg_new(info, 0, &hdr);
	if (!skb) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	if (nla_put_u32(skb, APF_ATTR_INFO_VERSION, apf_info.apf_version) ||
	    nla_put_u32(skb, APF_ATTR_INFO_ID, apf_info.apf_id) ||
	    nla_put_u32(skb, APF_ATTR_INFO_TOTAL_RAM, apf_info.total_ram) ||
	    nla_put_u32(skb, APF_ATTR_INFO_USED_RAM, apf_info.used_ram) ||
	    nla_put_u32(skb, APF_ATTR_INFO_OVERHEAD, apf_info.overhead) ||
	    nla_put_u32(skb, APF_ATTR_INFO_GRANULARITY, apf_info.granularity)) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to encode info attributes");
		nlmsg_free(skb);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	genlmsg_end(skb, hdr);
	ret = genlmsg_reply(skb, info);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_set_id(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_INFO_ID))
		return -EINVAL;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->set_id(dev, info->extack,
					       nla_get_u32(info->attrs[APF_ATTR_INFO_ID]));
	dev_put(dev);
	return ret;
}

static int apf_enable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_RAM_SIZE))
		return -EINVAL;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->enable(dev, info->extack,
					       nla_get_u32(info->attrs[APF_ATTR_RAM_SIZE]));
	dev_put(dev);
	return ret;
}

static int apf_get_ram_size(struct sk_buff *in_skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *skb;
	void *hdr;
	int ret;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->get_ram_size(dev, info->extack);
	if (ret < 0)
		goto out_put_dev;

	skb = apf_genlmsg_new(info, 0, &hdr);
	if (!skb) {
		ret = -ENOMEM;
		goto out_put_dev;
	}

	if (nla_put_u32(skb, APF_ATTR_RAM_SIZE, ret)) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to encode RAM size attribute");
		nlmsg_free(skb);
		ret = -EMSGSIZE;
		goto out_put_dev;
	}

	genlmsg_end(skb, hdr);
	ret = genlmsg_reply(skb, info);

out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_read(struct sk_buff *in_skb, struct genl_info *info)
{
	struct net_device *dev;
	struct sk_buff *skb;
	u32 offset, len;
	s32 req_len;
	void *hdr;
	u8 *buf;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_OFFSET) ||
	    GENL_REQ_ATTR_CHECK(info, APF_ATTR_DATA_LEN))
		return -EINVAL;

	req_len = nla_get_s32(info->attrs[APF_ATTR_DATA_LEN]);
	if (!req_len) {
		NL_SET_ERR_MSG_MOD(info->extack, "Read length must be non-zero or -1");
		return -EINVAL;
	}

	offset = nla_get_s32(info->attrs[APF_ATTR_OFFSET]);
	if (req_len == -1 && offset != 0) {
		NL_SET_ERR_MSG_MOD(info->extack, "Full RAM read requires offset 0");
		return -EINVAL;
	}

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	if (req_len == -1) {
		ret = dev->netdev_ops->apf_ops->get_ram_size(dev, info->extack);
		if (ret <= 0) {
			if (!ret) {
				NL_SET_ERR_MSG_MOD(info->extack, "No APF RAM allocated on device");
				ret = -ENODATA;
			}
			goto out_put_dev;
		}

		len = ret;
	} else {
		len = req_len;
	}

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to allocate read buffer");
		ret = -ENOMEM;
		goto out_put_dev;
	}

	ret = dev->netdev_ops->apf_ops->read(dev, info->extack, offset, buf, len);
	if (ret)
		goto out_free_buf;

	skb = apf_genlmsg_new(info, nla_total_size(len), &hdr);
	if (!skb) {
		ret = -ENOMEM;
		goto out_free_buf;
	}

	if (nla_put(skb, APF_ATTR_DATA, len, buf)) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to encode read data attribute");
		nlmsg_free(skb);
		ret = -EMSGSIZE;
		goto out_free_buf;
	}

	genlmsg_end(skb, hdr);
	ret = genlmsg_reply(skb, info);

out_free_buf:
	kfree(buf);
out_put_dev:
	dev_put(dev);
	return ret;
}

static int apf_write(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	u32 len;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_OFFSET) ||
	    GENL_REQ_ATTR_CHECK(info, APF_ATTR_DATA))
		return -EINVAL;

	len = nla_len(info->attrs[APF_ATTR_DATA]);
	if (!len) {
		NL_SET_ERR_MSG_MOD(info->extack, "Write data buffer cannot be empty");
		return -EINVAL;
	}

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->write(dev, info->extack,
					      nla_get_s32(info->attrs[APF_ATTR_OFFSET]),
					      nla_data(info->attrs[APF_ATTR_DATA]), len);
	dev_put(dev);
	return ret;
}

static int apf_disable(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->disable(dev, info->extack);
	dev_put(dev);
	return ret;
}

static int apf_set_fast_path(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, APF_ATTR_FP_UCAST_MAC) ||
	    GENL_REQ_ATTR_CHECK(info, APF_ATTR_FP_VLAN_TAG) ||
	    GENL_REQ_ATTR_CHECK(info, APF_ATTR_FP_UCAST_ADDR4) ||
	    GENL_REQ_ATTR_CHECK(info, APF_ATTR_FP_ENABLE_IPV6))
		return -EINVAL;

	dev = apf_get_netdev(info);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ret = dev->netdev_ops->apf_ops->fast_path(dev, info->extack,
			nla_data(info->attrs[APF_ATTR_FP_UCAST_MAC]),
			nla_get_s16(info->attrs[APF_ATTR_FP_VLAN_TAG]),
			nla_get_be32(info->attrs[APF_ATTR_FP_UCAST_ADDR4]),
			nla_get_u8(info->attrs[APF_ATTR_FP_ENABLE_IPV6]));
	dev_put(dev);
	return ret;
}

static const struct nla_policy apf_ifindex_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
};

static const struct nla_policy apf_set_id_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_INFO_ID] = { .type = NLA_U32 },
};

static const struct nla_policy apf_enable_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_RAM_SIZE] = NLA_POLICY_MIN(NLA_U32, 1),
};

static const struct nla_policy apf_read_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_OFFSET] = NLA_POLICY_MIN(NLA_S32, 0),
	[APF_ATTR_DATA_LEN] = NLA_POLICY_MIN(NLA_S32, -1),
};

static const struct nla_policy apf_write_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_OFFSET] = NLA_POLICY_MIN(NLA_S32, -1),
	[APF_ATTR_DATA] = { .type = NLA_BINARY },
};

static const struct nla_policy apf_set_fast_path_policy[APF_ATTR_MAX + 1] = {
	[APF_ATTR_IFINDEX] = NLA_POLICY_MIN(NLA_U32, 1),
	[APF_ATTR_FP_UCAST_MAC] = NLA_POLICY_ETH_ADDR,
	[APF_ATTR_FP_VLAN_TAG] = NLA_POLICY_RANGE(NLA_S16, -1, 4095),
	[APF_ATTR_FP_UCAST_ADDR4] = { .type = NLA_BE32 },
	[APF_ATTR_FP_ENABLE_IPV6] = NLA_POLICY_RANGE(NLA_U8, 0, 1),
};

static const struct genl_ops apf_ops_list[] = {
	{
		.cmd = APF_CMD_GET_INFO,
		.doit = apf_get_info,
		.policy = apf_ifindex_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_SET_ID,
		.doit = apf_set_id,
		.policy = apf_set_id_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_ENABLE,
		.doit = apf_enable,
		.policy = apf_enable_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_GET_RAM_SIZE,
		.doit = apf_get_ram_size,
		.policy = apf_ifindex_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_READ,
		.doit = apf_read,
		.policy = apf_read_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_WRITE,
		.doit = apf_write,
		.policy = apf_write_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_DISABLE,
		.doit = apf_disable,
		.policy = apf_ifindex_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = APF_CMD_SET_FAST_PATH,
		.doit = apf_set_fast_path,
		.policy = apf_set_fast_path_policy,
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
