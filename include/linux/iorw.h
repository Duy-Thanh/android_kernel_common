/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */
#ifndef __LOG_IORW_H__
#define __LOG_IORW_H__

#include <linux/types.h>

#if IS_ENABLED(CONFIG_TRACE_RW)
void log_write_io(volatile void __iomem *addr);
void log_read_io(const volatile void __iomem *addr);
#else
static inline void log_write_io(volatile void __iomem *addr)
{ }
static inline void log_read_io(const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_RW */

#endif /* __LOG_IORW_H__  */
