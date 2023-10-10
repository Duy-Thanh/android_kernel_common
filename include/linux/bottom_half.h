/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BH_H
#define _LINUX_BH_H

#include <linux/preempt.h>

#ifdef CONFIG_CMBC
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	__CPROVER_assert(0, "Shouldn't reach here");
}
#else
#if defined(CONFIG_PREEMPT_RT) || defined(CONFIG_TRACE_IRQFLAGS)
extern void __local_bh_disable_ip(unsigned long ip, unsigned int cnt);
#else
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	preempt_count_add(cnt);
	barrier();
}
#endif
#endif  // CONFIG_CMBC

static inline void local_bh_disable(void)
{
	__local_bh_disable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

extern void _local_bh_enable(void);
extern void __local_bh_enable_ip(unsigned long ip, unsigned int cnt);

static inline void local_bh_enable_ip(unsigned long ip)
{
#ifdef CONFIG_CBMC
	__CPROVER_assert(0, "Shouldn't reach here");
#else
	__local_bh_enable_ip(ip, SOFTIRQ_DISABLE_OFFSET);
#endif
}

static inline void local_bh_enable(void)
{
#ifdef CONFIG_CBMC
	__CPROVER_assert(0, "Shouldn't reach here");
#else
	__local_bh_enable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
#endif
}

#ifdef CONFIG_PREEMPT_RT
extern bool local_bh_blocked(void);
#else
static inline bool local_bh_blocked(void) { return false; }
#endif

#endif /* _LINUX_BH_H */
