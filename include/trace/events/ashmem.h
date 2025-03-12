/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ashmem

#if !defined(_ASHMEM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _ASHMEM_TRACE_H

#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(ashmem_deprecated_unset_prot,

	TP_PROTO(const char *buf_name),

	TP_ARGS(buf_name),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__array(char, buf_name, ASHMEM_NAME_LEN)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		memcpy(__entry->buf_name, buf_name, ASHMEM_NAME_LEN);
	),

	TP_printk("%s %s", __entry->comm,
		  __entry->buf_name)
);

DEFINE_EVENT(ashmem_deprecated_unset_prot, unset_prot_read,

	TP_PROTO(const char *buf_name),

	TP_ARGS(buf_name)
);

DEFINE_EVENT(ashmem_deprecated_unset_prot, unset_prot_exec,

	TP_PROTO(const char *buf_name),

	TP_ARGS(buf_name)
);

DECLARE_EVENT_CLASS(ashmem_deprecated_pinning_usage,

	TP_PROTO(const char *buf_name, long ret),

	TP_ARGS(buf_name, ret),

	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__array(char, buf_name, ASHMEM_NAME_LEN)
		__field(long, ret)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		memcpy(__entry->buf_name, buf_name, ASHMEM_NAME_LEN);
		__entry->ret = ret;
	),

	TP_printk("%s %s ret: %ld", __entry->comm, __entry->buf_name, __entry->ret)
);


DEFINE_EVENT(ashmem_deprecated_pinning_usage, unpin_range,

	TP_PROTO(const char *buf_name, long ret),

	TP_ARGS(buf_name, ret)
);

DEFINE_EVENT(ashmem_deprecated_pinning_usage, pin_range,

	TP_PROTO(const char *buf_name, long ret),

	TP_ARGS(buf_name, ret)
);

DEFINE_EVENT(ashmem_deprecated_pinning_usage, get_range_pin_status,

	TP_PROTO(const char *buf_name, long ret),

	TP_ARGS(buf_name, ret)
);

TRACE_EVENT(purge_all_caches,

	    TP_PROTO(long ret),

	    TP_ARGS(ret),

	    TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(long, ret)
	    ),

	    TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->ret = ret;
	    ),

	    TP_printk("%s purge all caches ret: %ld", __entry->comm, __entry->ret)
);

#endif /* _TRACE_ASHMEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
