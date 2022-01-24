#if !defined(__FONGER_DMA_OP_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __FONGER_DMA_OP_TRACE_H__

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fonger_dma_op

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE fonger_dma_op_trace

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/time64.h>
#include <linux/time.h>
#include <linux/timekeeping.h>



TRACE_EVENT(fonger_dma_op_for_device,
    TP_PROTO(phys_addr_t paddr, size_t size, int dir, long pid, long start_sec, long start_ns, long end_sec, long end_ns),

    TP_ARGS(paddr, size, dir, pid, start_sec, start_ns, end_sec, end_ns),

    TP_STRUCT__entry(
        __field(phys_addr_t, paddr)
        __field(size_t, size)
        __field(int, dir)
        __field(long, pid)
        __field(long, start_sec)
        __field(long, start_ns)
        __field(long, end_sec)
        __field(long, end_ns)
       ),

    TP_fast_assign(
        __entry->paddr = paddr;
        __entry->size = size;
        __entry->dir = dir;
        __entry->pid = pid;
        __entry->start_sec = start_sec;
        __entry->start_ns = start_ns;
        __entry->end_sec = end_sec;
        __entry->end_ns = end_ns;
    ),

    TP_printk("Fonger_dma_sync_for_device: PA: %p Size: %zx, Dir: %d, PID: %ld, Start_Sec: %ld Start_NS: %ld, End_Sec, End_NS: %ld",
          __entry->paddr, __entry->size, __entry->dir, __entry->pid, __entry->start_sec, __entry->start_ns, __entry->end_sec, __entry->end_ns)
);



TRACE_EVENT(fonger_dma_op_for_cpu,
    TP_PROTO(phys_addr_t paddr, size_t size, int dir, long pid, long start_sec, long start_ns, long end_sec, long end_ns),

    TP_ARGS(paddr, size, dir, pid, start_sec, start_ns, end_sec, end_ns),

    TP_STRUCT__entry(
        __field(phys_addr_t, paddr)
        __field(size_t, size)
        __field(int, dir)
        __field(long, pid)
        __field(long, start_sec)
	__field(long, start_ns)
        __field(long, end_sec)
        __field(long, end_ns)
       ),

    TP_fast_assign(
        __entry->paddr = paddr;
        __entry->size = size;
        __entry->dir = dir;
        __entry->pid = pid;
        __entry->start_sec = start_sec;
	__entry->start_ns = start_ns;
        __entry->end_sec = end_sec;
        __entry->end_ns = end_ns;

    ),

    TP_printk("Fonger_dma_sync_for_cpu: PA: %p Size: %zx, Dir: %d, PID: %ld, Start_Sec: %ld Start_NS: %ld, End_Sec, End_NS: %ld",
          __entry->paddr, __entry->size, __entry->dir, __entry->pid, __entry->start_sec, __entry->start_ns, __entry->end_sec, __entry->end_ns)
);


TRACE_EVENT(fonger_dma_op_prep_coh,
    TP_PROTO(phys_addr_t paddr, size_t size, int dir),

    TP_ARGS(paddr, size, dir),

    TP_STRUCT__entry(
        __field(phys_addr_t, paddr)
        __field(size_t, size)
	__field(int, dir)
       ),

    TP_fast_assign(
        __entry->paddr = paddr;
        __entry->size = size;
	__entry->dir = dir;
    ),

    TP_printk("Fonger_dma_prep_coh: PA: %p Size: %zx, Dir: %d",
          __entry->paddr, __entry->size, __entry->dir)
);


#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../fonger_trace
#include <trace/define_trace.h>
