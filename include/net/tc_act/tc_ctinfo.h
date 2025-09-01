/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_CTINFO_H
#define __NET_TC_CTINFO_H

#include <net/act_api.h>

struct tcf_ctinfo_params {
	struct rcu_head rcu;
	struct net *net;
	u32 dscpmask;
	u32 dscpstatemask;
	u32 cpmarkmask;
	u16 zone;
	u8 mode;
	u8 dscpmaskshift;
};

struct tcf_ctinfo {
	struct tc_action common;
	struct tcf_ctinfo_params __rcu *params;
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
	atomic64_t stats_dscp_set;
	atomic64_t stats_dscp_error;
	atomic64_t stats_cpmark_set;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
	u64 stats_dscp_set;
	u64 stats_dscp_error;
	u64 stats_cpmark_set;
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
};

enum {
	CTINFO_MODE_DSCP	= BIT(0),
	CTINFO_MODE_CPMARK	= BIT(1)
};

#define to_ctinfo(a) ((struct tcf_ctinfo *)a)

#endif /* __NET_TC_CTINFO_H */
