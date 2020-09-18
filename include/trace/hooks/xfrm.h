/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xfrm
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_XFRM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_XFRM_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct sk_buff;
struct net;
struct sock;
DECLARE_HOOK(android_vh_ip6_pkt_too_big,
		 TP_PROTO(const struct sk_buff *skb, unsigned int mtu, int *vh_ret),
		 TP_ARGS(skb, mtu, vh_ret));

DECLARE_HOOK(android_vh_ip6_fragment,
		 TP_PROTO(struct net *net, struct sock *sk, struct sk_buff *skb,
		 int (*output)(struct net *, struct sock *, struct sk_buff *), int *vh_ret),
		 TP_ARGS(net, sk, skb, output, vh_ret));

DECLARE_HOOK(android_vh__xfrm4_output,
		 TP_PROTO(struct net *net, struct sock *sk, struct sk_buff *skb, int *vh_ret),
		 TP_ARGS(net, sk, skb, vh_ret));
#else
#define trace_android_vh_ip6_pkt_too_big(skb, mtu, vh_ret)
#define trace_android_vh_ip6_fragment(net, sk, skb, output, vh_ret)
#define trace_android_vh__xfrm4_output(net, sk, skb, vh_ret)
#endif
#endif /* _TRACE_XFRM_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
