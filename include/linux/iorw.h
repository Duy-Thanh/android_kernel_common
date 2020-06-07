/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */
#ifndef __LOG_IORW_H__
#define __LOG_IORW_H__

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/tracepoint-defs.h>

#if IS_ENABLED(CONFIG_TRACE_RW)
DECLARE_TRACEPOINT(rwio_write);
DECLARE_TRACEPOINT(rwio_writeq);
DECLARE_TRACEPOINT(rwio_read);
DECLARE_TRACEPOINT(rwio_post_read);
DECLARE_TRACEPOINT(rwio_post_readq);

void __log_write_io(u32 val, volatile void __iomem *addr);
void __log_writeq_io(u64 val, volatile void __iomem *addr);
void __log_read_io(const volatile void __iomem *addr);
void __log_post_read_io(u32 val, const volatile void __iomem *addr);
void __log_post_readq_io(u64 val, const volatile void __iomem *addr);

#define log_write_io(val, addr)				\
do {							\
	if (tracepoint_enabled(rwio_write))		\
		__log_write_io(val, addr);		\
} while (0)

#define log_writeq_io(val, addr)				\
do {							\
	if (tracepoint_enabled(rwio_writeq))		\
		__log_writeq_io(val, addr);		\
} while (0)

#define log_read_io(addr)				\
do {							\
	if (tracepoint_enabled(rwio_read))		\
		__log_read_io(addr);			\
} while (0)

#define log_post_read_io(val, addr)			\
do {							\
	if (tracepoint_enabled(rwio_post_read))		\
		__log_post_read_io(val, addr);		\
} while (0)

#define log_post_readq_io(val, addr)			\
do {							\
	if (tracepoint_enabled(rwio_post_readq))	\
		__log_post_read_io(val, addr);		\
} while (0)

#else
static inline void log_write_io(u32 val, volatile void __iomem *addr)
{ }
static inline void log_writeq_io(u64 val, volatile void __iomem *addr)
{ }
static inline void log_read_io(const volatile void __iomem *addr)
{ }
static inline void log_post_read_io(u32 val, const volatile void __iomem *addr)
{ }
static inline void log_post_readq_io(u64 val, const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_RW */

#endif /* __LOG_IORW_H__  */
