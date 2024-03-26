// SPDX-License-Identifier: GPL-2.0-only
/* binder_genl.c
 *
 * Android IPC Subsystem
 *
 * Copyright (C) 2024 Google, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "binder_genl.h"
#include "binder_trace.h"

/**
 * The registered process that would receive binder reports.
 */
static u32 binder_report_pid;

static const struct nla_policy binder_report_policy[BINDER_GENL_ATTR_MAX + 1] = {
	[BINDER_GENL_ATTR_PID] = { .type = NLA_U32 },
};

static struct genl_family binder_gnl_family;

static int binder_cmd_register(struct genl_info *info, u32 pid)
{
	int len;
	struct sk_buff *skb;
	void *hdr;

	len = sizeof(pid);
	skb = genlmsg_new(len, GFP_KERNEL);
	if (!skb) {
		pr_err("Failed to alloc binder genl message\n");
		return -ENOMEM;
	}

	hdr = genlmsg_put(skb, pid, 0, &binder_gnl_family, 0,
			BINDER_GENL_CMD_REPLY);
	if (!hdr) {
		pr_err("Failed to set binder genl header\n");
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (nla_put(skb, BINDER_GENL_ATTR_PID, len, &pid)) {
		genlmsg_cancel(skb, hdr);
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	genlmsg_end(skb, hdr);

	if (genlmsg_reply(skb, info)) {
		pr_err("Failed to send binder genl message\n");
		return -EFAULT;
	}

	binder_report_pid = pid;
	pr_info("Binder report registered from pid %d\n", pid);

	return 0;
}

static int binder_genl_cmd_doit(struct sk_buff *skb, struct genl_info *info) {
	int pid;

	pid = nlmsg_hdr(skb)->nlmsg_pid;

	// Only the 1st registered process would receive binder reports
	if (binder_report_pid == 0) {
		return binder_cmd_register(info, pid);
	} else {
		pr_err("Redundant binder report registration from pid %d\n",
				pid);
		return -EACCES;
	}
}

static struct genl_small_ops binder_genl_ops[] = {
	{
		.cmd = BINDER_GENL_CMD_REGISTER,
		.doit = binder_genl_cmd_doit,
	}
};

static struct genl_family binder_gnl_family = {
	.name = BINDER_GENL_FAMILY_NAME,
	.version = BINDER_GENL_VERSION,
	.maxattr = BINDER_GENL_ATTR_MAX,
	.policy	= binder_report_policy,
	.small_ops = binder_genl_ops,
	.n_small_ops = ARRAY_SIZE(binder_genl_ops),
};

int binder_init_genl(void)
{
	int ret = genl_register_family(&binder_gnl_family);
	if (ret) {
		pr_err("Failed to register binder genl\n");
		return ret;
	}

	pr_info("Binder genl v%d inited\n", BINDER_GENL_VERSION);

	return 0;
}

inline bool binder_report_enabled(struct binder_context *context, u32 mask)
{
	return (context->report_flags & mask) != 0;
}

void binder_send_report(struct binder_report *report, int len)
{
	struct sk_buff *skb;
	void *hdr;

	trace_binder_send_report(report, len);

	skb = genlmsg_new(len, GFP_KERNEL);
	if (!skb) {
		pr_err("Failed to alloc binder genl message\n");
		return;
	}

	hdr = genlmsg_put(skb, binder_report_pid, 0, &binder_gnl_family, 0,
			BINDER_GENL_CMD_REPORT);
	if (!hdr) {
		pr_err("Failed to set binder genl header\n");
		kfree_skb(skb);
		return;
	}

	if (nla_put(skb, BINDER_GENL_ATTR_REPORT, len, report)) {
		genlmsg_cancel(skb, hdr);
		nlmsg_free(skb);
		return;
	}

	genlmsg_end(skb, hdr);

	if (genlmsg_unicast(&init_net, skb, binder_report_pid))
		pr_err("Failed to send binder genl message\n");
}
