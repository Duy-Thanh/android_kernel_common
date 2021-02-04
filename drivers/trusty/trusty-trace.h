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

#define _Args(...)	__VA_ARGS__
#define STRIP_PARENS(X)	X
#define DEL_PAREN(X)	STRIP_PARENS(_Args X)

/*
 * SMC fast call and std call numbers from linux/trusty/smcall.h
 */
#define SMC_NAME_LIST			\
	(smc_sc_name(RESTART_LAST)	\
	smc_sc_name(LOCKED_NOP)		\
	smc_sc_name(RESTART_FIQ)	\
	smc_sc_name(NOP)		\
	smc_fc_name(RESERVED)		\
	smc_fc_name(FIQ_EXIT)		\
	smc_fc_name(REQUEST_FIQ)	\
	smc_fc_name(GET_NEXT_IRQ)	\
	smc_fc_name(CPU_SUSPEND)	\
	smc_fc_name(CPU_RESUME)		\
	smc_fc_name(AARCH_SWITCH)	\
	smc_fc_name(GET_VERSION_STR)	\
	smc_fc_name(API_VERSION)	\
	smc_sc_name(VIRTIO_GET_DESCR)	\
	smc_sc_name(VIRTIO_START)	\
	smc_sc_name(VIRTIO_STOP)	\
	smc_sc_name(VDEV_RESET)		\
	smc_sc_name(VDEV_KICK_VQ)	\
	smc_nc_name(NC_VDEV_KICK_VQ))


#undef smc_sc_name
#undef smc_fc_name
#undef smc_nc_name

#define smc_sc_name_enum(x)	(TRACE_DEFINE_ENUM(SMC_SC_##x);)
#define smc_fc_name_enum(x)	(TRACE_DEFINE_ENUM(SMC_FC_##x);)
#define smc_nc_name_enum(x)	(TRACE_DEFINE_ENUM(SMC_##x);)

#define smc_sc_name(x)		DEL_PAREN(smc_sc_name_enum(x))
#define smc_fc_name(x)          DEL_PAREN(smc_fc_name_enum(x))
#define smc_nc_name(x)          DEL_PAREN(smc_nc_name_enum(x))

/* SMC_NAME_LIST */
DEL_PAREN(SMC_NAME_LIST)

#undef smc_sc_name
#undef smc_fc_name
#undef smc_nc_name

#define smc_sc_name(x)          { SMC_SC_##x, #x },
#define smc_fc_name(x)          { SMC_FC_##x, #x },
#define smc_nc_name(x)          { SMC_##x, #x }


#define smc_show_name(x) \
	__print_symbolic(x, DEL_PAREN(SMC_NAME_LIST))

DECLARE_EVENT_CLASS(trusty_smc4_class,
	TP_PROTO(unsigned long r0, unsigned long r1, unsigned long r2,
		 unsigned long r3),
	TP_ARGS(r0, r1, r2, r3),
	TP_STRUCT__entry(
		__field(unsigned long, r0)
		__field(unsigned long, r1)
		__field(unsigned long, r2)
		__field(unsigned long, r3)
	),
	TP_fast_assign(
		__entry->r0 = r0;
		__entry->r1 = r1;
		__entry->r2 = r2;
		__entry->r3 = r3;
	),
	TP_printk("smcnr=%s r0=0x%lx r1=0x%lx r2=0x%lx r3=0x%lx", smc_show_name(__entry->r0),
	__entry->r0, __entry->r1, __entry->r2, __entry->r3)
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
		__field(long, ret)
	),
	TP_fast_assign(
		__entry->ret = (long)ret;
	),
	TP_printk("ret=%d (0x%x)", __entry->ret, __entry->ret)
);

#define DEFINE_TRUSTY_SMC_RETURN_EVENT(name)	\
DEFINE_EVENT(trusty_smc_return_class, name,	\
	TP_PROTO(unsigned long ret), \
	TP_ARGS(ret))

DEFINE_TRUSTY_SMC_RETURN_EVENT(trusty_std_call32_done);
DEFINE_TRUSTY_SMC_RETURN_EVENT(trusty_smc_done);

TRACE_EVENT(trusty_share_memory,
	TP_PROTO(size_t len, unsigned int nents, bool lend),
	TP_ARGS(len, nents, lend),
	TP_STRUCT__entry(
		__field(size_t, len)
		__field(unsigned int, nents)
		__field(bool, lend)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->nents = nents;
		__entry->lend = lend;
	),
	TP_printk("len=%zu, nents=%u, lend=%u", __entry->len, __entry->nents, __entry->lend)
);

TRACE_EVENT(trusty_share_memory_done,
	TP_PROTO(size_t len, unsigned int nents, bool lend, u64 handle, int ret),
	TP_ARGS(len, nents, lend, handle, ret),
	TP_STRUCT__entry(
		__field(size_t, len)
		__field(unsigned int, nents)
		__field(bool, lend)
		__field(u64, handle)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->nents = nents;
		__entry->lend = lend;
		__entry->handle = handle;
		__entry->ret = ret;
	),
	TP_printk("len=%zu, nents=%u, lend=%u, ffa_handle=0x%llx, ret=%d", __entry->len,
	__entry->nents, __entry->lend, __entry->handle, __entry->ret)
);

TRACE_EVENT(trusty_enqueue_nop,
	TP_PROTO(struct trusty_nop *nop),
	TP_ARGS(nop),
	TP_STRUCT__entry(
		__field(u32, arg1)
		__field(u32, arg2)
		__field(u32, arg3)
	),
	TP_fast_assign(
		__entry->arg1 = nop ? nop->args[0] : 0U;
		__entry->arg2 = nop ? nop->args[1] : 0U;
		__entry->arg3 = nop ? nop->args[2] : 0U;
	),
	TP_printk("arg1=0x%x, arg2=0x%x, arg3=0x%x", __entry->arg1, __entry->arg2, __entry->arg3)
);

TRACE_EVENT(trusty_reclaim_memory,
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

TRACE_EVENT(trusty_reclaim_memory_done,
	TP_PROTO(u64 id, unsigned long ret),
	TP_ARGS(id, ret),
	TP_STRUCT__entry(
		__field(u64, id)
		__field(unsigned long, ret)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->ret = (long)ret;
	),
	TP_printk("id=%llu ret=%d (0x%x)", __entry->id, __entry->ret, __entry->ret)
);

#endif /* _TRUSTY_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trusty-trace
#include <trace/define_trace.h>
