// SPDX-License-Identifier: GPL-2.0
/*
 * ARM64 compat userspace implementations of gettimeofday() and similar.
 *
 * Copyright (C) 2018 ARM Limited
 *
 */
#include <linux/time.h>
#include <linux/types.h>

notrace int __vdso_clock_gettime(clockid_t clock,
				 struct __vdso_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

notrace int __vdso_gettimeofday(struct __vdso_timeval *tv,
				struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

notrace int __vdso_clock_getres(clockid_t clock_id,
				struct __vdso_timespec *res)
{
	return __cvdso_clock_getres(clock_id, res);
}

notrace time_t __vdso_time(time_t *t)
{
	return __cvdso_time(t);
}

/* Avoid unresolved references emitted by GCC */

void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}

void __aeabi_unwind_cpp_pr2(void)
{
}
