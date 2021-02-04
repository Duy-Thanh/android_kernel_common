/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM trusty

#if !defined(_TRUSTY_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRUSTY_TRACE_H

#include <linux/tracepoint.h>
#include <linux/trusty/smcall.h>

DECLARE_EVENT_CLASS(trusty_smc4_class,
	TP_PROTO(unsigned long r0, unsigned long r1, unsigned long r2,
		 unsigned long r3),
	TP_ARGS(r0, r1, r2, r3),
	TP_STRUCT__entry(
		__array(char, smcnr, 16)
		__field(unsigned long, r0)
		__field(unsigned long, r1)
		__field(unsigned long, r2)
		__field(unsigned long, r3)
	),
	TP_fast_assign(
		memcpy(__entry->smcnr, smc_name(r0), 16);
		__entry->r0 = r0;
		__entry->r1 = r1;
		__entry->r2 = r2;
		__entry->r3 = r3;
	),
	TP_printk("smcnr=%s r0=0x%lx r1=0x%lx r2=0x%lx r3=0x%lx", __entry->smcnr, __entry->r0, __entry->r1, __entry->r2,
		  __entry->r3)
);

#define DEFINE_TRUSTY_SMC4_EVENT(name)	\
DEFINE_EVENT(trusty_smc4_class, name,	\
	TP_PROTO(unsigned long r0, unsigned long r1, unsigned long r2, \
		 unsigned long r3), \
	TP_ARGS(r0, r1, r2, r3))

DEFINE_TRUSTY_SMC4_EVENT(trusty_std_call32);
DEFINE_TRUSTY_SMC4_EVENT(trusty_smc);

DECLARE_EVENT_CLASS(trusty_smc_return_class,
	TP_PROTO(unsigned long ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(unsigned long, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("ret=%d (0x%lx)", (int)__entry->ret, __entry->ret)
);

#define DEFINE_TRUSTY_SMC_RETURN_EVENT(name)	\
DEFINE_EVENT(trusty_smc_return_class, name,	\
	TP_PROTO(unsigned long ret), \
	TP_ARGS(ret))

DEFINE_TRUSTY_SMC_RETURN_EVENT(trusty_std_call32_done);
DEFINE_TRUSTY_SMC_RETURN_EVENT(trusty_smc_done);

TRACE_EVENT(trusty_share_memory,
	TP_PROTO(size_t len, unsigned int nents),
	TP_ARGS(len, nents),
	TP_STRUCT__entry(
		__field(size_t, len)
		__field(unsigned int, nents)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->nents = nents;
	),
	TP_printk("len=%zu, nents=%u", __entry->len, __entry->nents)
);

TRACE_EVENT(trusty_share_memory_done,
	TP_PROTO(size_t len, unsigned int nents, u64 id),
	TP_ARGS(len, nents, id),
	TP_STRUCT__entry(
		__field(size_t, len)
		__field(unsigned int, nents)
		__field(u64, id)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->nents = nents;
		__entry->id = id;
	),
	TP_printk("len=%zu, nents=%u, id=0x%llx", __entry->len, __entry->nents,
		  __entry->id)
);

DECLARE_EVENT_CLASS(trusty_memid_class,
	TP_PROTO(u64 id),
	TP_ARGS(id),
	TP_STRUCT__entry(
		__field(u64, id)
	),
	TP_fast_assign(
		__entry->id = id;
	),
	TP_printk("id=%llu", __entry->id)
);

#define DEFINE_TRUSTY_MEMID_EVENT(name)	\
DEFINE_EVENT(trusty_memid_class, name,	\
	TP_PROTO(u64 id), \
	TP_ARGS(id))

DEFINE_TRUSTY_MEMID_EVENT(trusty_reclaim_memory);
DEFINE_TRUSTY_MEMID_EVENT(trusty_reclaim_memory_done);

#endif /* _TRUSTY_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trusty-trace
#include <trace/define_trace.h>
