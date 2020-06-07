// SPDX-License-Identifier: GPL-2.0
/*
 * Register read write tracepoints
 *
 */

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/iorw.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwio.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(raw_write);
EXPORT_TRACEPOINT_SYMBOL_GPL(raw_read);

void log_write_io(volatile void __iomem *addr)
{
	trace_raw_write(CALLER_ADDR0, addr);
}
EXPORT_SYMBOL(log_write_io);

void log_read_io(const volatile void __iomem *addr)
{
	trace_raw_read(CALLER_ADDR0, addr);
}
EXPORT_SYMBOL(log_read_io);
