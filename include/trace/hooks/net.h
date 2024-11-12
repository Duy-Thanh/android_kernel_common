/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM net
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_NET_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_NET_VH_H
#include <trace/hooks/vendor_hooks.h>

struct packet_type;
struct list_head;
DECLARE_HOOK(android_vh_ptype_head,
	TP_PROTO(const struct packet_type *pt, struct list_head *vendor_pt),
	TP_ARGS(pt, vendor_pt));

struct sock;
struct sockaddr_in6;
DECLARE_HOOK(android_vh_tcp_v4_connect,
	TP_PROTO(struct sock *sk, struct sockaddr *uaddr), TP_ARGS(sk, uaddr));
DECLARE_HOOK(android_vh_tcp_v6_connect,
	TP_PROTO(struct sock *sk, struct sockaddr *uaddr), TP_ARGS(sk, uaddr));
DECLARE_HOOK(android_vh_udp_v4_connect,
	TP_PROTO(struct sock *sk, __be32 daddr, __be16 dport, uint16_t family),
	TP_ARGS(sk, daddr, dport, family));
DECLARE_HOOK(android_vh_udp_v6_connect,
	TP_PROTO(struct sock *sk, struct sockaddr_in6 *sin6), TP_ARGS(sk, sin6));
DECLARE_HOOK(android_vh_udp_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
DECLARE_HOOK(android_vh_udp6_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
DECLARE_HOOK(android_vh_sk_alloc,
	TP_PROTO(struct sock *sk), TP_ARGS(sk));
DECLARE_HOOK(android_vh_sk_free,
	TP_PROTO(struct sock *sk), TP_ARGS(sk));
DECLARE_HOOK(android_vh_sk_clone_lock,
	TP_PROTO(struct sock *nsk), TP_ARGS(nsk));
DECLARE_HOOK(android_vh_tcp_write_timeout_estab_retrans,
	TP_PROTO(struct sock *sk), TP_ARGS(sk));
struct request_sock;
DECLARE_HOOK(android_vh_inet_csk_clone_lock,
	TP_PROTO(struct sock *newsk, const struct request_sock *req), TP_ARGS(newsk, req));
DECLARE_HOOK(android_vh_tcp_clean_rtx_queue,
	TP_PROTO(struct sock *sk, int flag, long seq_rtt_us),
	TP_ARGS(sk, flag, seq_rtt_us));
struct inet_connection_sock;
DECLARE_HOOK(android_vh_tcp_rcv_synack,
	TP_PROTO(struct inet_connection_sock *icsk), TP_ARGS(icsk));
<<<<<<< HEAD   (171d963ed71fe5c19af40cf566db837920137e90 ANDROID: GKI: Add symbol to symbol list for unisoc)
||||||| BASE   (cbd012971e3bfd1a24ca0e2df1055fd9b1246d52 ANDROID: Add vendor hooks to signal.)
DECLARE_HOOK(android_vh_udp_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
DECLARE_HOOK(android_vh_udp6_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
=======
DECLARE_HOOK(android_vh_udp_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
DECLARE_HOOK(android_vh_udp6_unicast_rcv_skb,
	TP_PROTO(struct sk_buff *skb, struct sock *sk),
	TP_ARGS(skb, sk));
DECLARE_HOOK(android_vh_tcp_rcv_established_fast_path,
	TP_PROTO(struct sock *sk), TP_ARGS(sk));
DECLARE_HOOK(android_vh_tcp_rcv_established_slow_path,
	TP_PROTO(struct sock *sk), TP_ARGS(sk));
>>>>>>> CHANGE (0defa678590c9f83d74dd42a14f743425ab6f489 ANDROID: GKI: net: add vendor hook to check if out of order )
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_NET_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
