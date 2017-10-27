// SPDX-License-Identifier: GPL-2.0
/*
 * Generic userspace implementations of gettimeofday() and similar.
 *
 * Copyright (C) 2018 ARM Limited
 * Copyright (C) 2017 Cavium, Inc.
 * Copyright (C) 2015 Mentor Graphics Corporation
 *
 */
#include <linux/compiler.h>
#include <linux/math64.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/hrtimer.h>
#include <vdso/datapage.h>

#ifdef ENABLE_COMPAT_VDSO
#include <asm/vdso/compat_gettimeofday.h>
#else
#include <asm/vdso/gettimeofday.h>
#endif /* ENABLE_COMPAT_VDSO */

/* To improve performances, in this file, __always_inline it is used
 * for the functions called multiple times.
 */
static __always_inline notrace u32 vdso_read_begin(const struct vdso_data *vd)
{
	u32 seq;

repeat:
	/* Trying to access concurrent shared memory */
	seq = READ_ONCE(vd->tb_seq_count);
	if (seq & 1) {
		cpu_relax();
		goto repeat;
	}

	/* smp_rmb() pairs with the second smp_wmb() in update_vsyscall */
	smp_rmb();
	return seq;
}

static __always_inline notrace u32 vdso_read_retry(const struct vdso_data *vd,
						   u32 start)
{
	u32 seq;

	/* smp_rmb() pairs with the first smp_wmb() in update_vsyscall */
	smp_rmb();
	/* Trying to access concurrent shared memory */
	seq = READ_ONCE(vd->tb_seq_count);
	return seq != start;
}

/*
 * Returns the clock delta, in nanoseconds left-shifted by the clock
 * shift.
 */
static __always_inline notrace u64 get_clock_shifted_nsec(u64 cycle_last,
							  u64 mult,
							  u64 mask)
{
	u64 res;

	/* Read the virtual counter. */
	res = clock_get_virtual_counter();

	if (res > cycle_last)
		res = res - cycle_last;
	/*
	 * VDSO Precision Mask: represents the
	 * precision bits we can guaranty.
	 */
	res &= mask;
	return res * mult;
}

#ifdef CONFIG_HAVE_ARCH_TIMER
static __always_inline notrace int __do_realtime_or_tai(
		const struct vdso_data *vd,
		struct __vdso_timespec *ts,
		bool is_tai)
{
	u32 seq, cs_mono_mult, cs_shift;
	u64 ns, sec;
	u64 cycle_last, cs_mono_mask;

	if (vd->use_syscall)
		return -1;
repeat:
	seq = vdso_read_begin(vd);
	cycle_last = vd->cs_cycle_last;
	cs_mono_mult = vd->cs_mono_mult;
	cs_shift = vd->cs_shift;
	cs_mono_mask = vd->cs_mono_mask;

	if (is_tai)
		sec = vd->tai_sec;
	else
		sec = vd->xtime_clock_sec;
	ns = vd->xtime_clock_nsec;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	ns += get_clock_shifted_nsec(cycle_last, cs_mono_mult, cs_mono_mask);
	ns >>= cs_shift;
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}
#else
static __always_inline notrace int __do_realtime_or_tai(
		const struct vdso_data *vd,
		struct __vdso_timespec *ts,
		bool is_tai)
{
	return -1;
}
#endif

/*
 * Handles CLOCK_REALTIME - A representation of the "wall-clock" time.
 * Can be both stepped and slewed by time adjustment code. It can move
 * forward and backward.
 */
static __always_inline notrace int do_realtime(const struct vdso_data *vd,
					       struct __vdso_timespec *ts)
{
	return __do_realtime_or_tai(vd, ts, false);
}

/*
 * Handles CLOCK_TAI - Like CLOCK_REALTIME, but uses the International
 * Atomic Time (TAI) reference instead of UTC to avoid jumping on leap
 * second updates.
 */
static notrace int do_tai(const struct vdso_data *vd,
			  struct __vdso_timespec *ts)
{
	return __do_realtime_or_tai(vd, ts, true);
}

#ifdef CONFIG_HAVE_ARCH_TIMER
static __always_inline notrace int __do_monotonic(const struct vdso_data *vd,
						  struct __vdso_timespec *ts,
						  bool boottime)
{
	u32 seq, cs_mono_mult, cs_shift;
	u64 ns, wtm_ns, sec;
	u64 cycle_last, cs_mono_mask;

	if (vd->use_syscall)
		return 1;

repeat:
	seq = vdso_read_begin(vd);

	cycle_last = vd->cs_cycle_last;
	cs_mono_mult = vd->cs_mono_mult;
	cs_shift = vd->cs_shift;
	cs_mono_mask = vd->cs_mono_mask;

	sec = vd->xtime_clock_sec;
	ns = vd->xtime_clock_nsec;
	sec += vd->wtm_clock_sec;

	if (boottime)
		wtm_ns = vd->wtm_clock_nsec + vd->btm_nsec;
	else
		ns += vd->wtm_clock_nsec << cs_shift;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	ns += get_clock_shifted_nsec(cycle_last, cs_mono_mult, cs_mono_mask);
	ns >>= cs_shift;

	if (boottime)
		ns += wtm_ns;

	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}
#else
static __always_inline notrace int __do_monotonic(const struct vdso_data *vd,
						  struct __vdso_timespec *ts,
						  bool boottime)
{
	return -1;
}
#endif

/*
 * Handles CLOCK_MONOTONIC - A representation of the interval from an
 * arbitrary given time. Can be slewed but not stepped by time adjustment
 * code. It can move forward but not backward.
 */
static notrace int do_monotonic(const struct vdso_data *vd,
				struct __vdso_timespec *ts)
{
	return __do_monotonic(vd, ts, false);
}

/*
 * Handles CLOCK_MONOTONIC_RAW - This is a version of CLOCK_MONOTONIC that can
 * be neither slewed nor stepped by time adjustment code. It cannot move
 * forward or backward.
 */
static notrace int do_monotonic_raw(const struct vdso_data *vd,
				    struct __vdso_timespec *ts)
{
	u32 seq, cs_raw_mult, cs_shift;
	u64 ns, sec;
	u64 cycle_last, cs_mono_mask;

	if (vd->use_syscall)
		return -1;

repeat:
	seq = vdso_read_begin(vd);

	cycle_last = vd->cs_cycle_last;
	cs_raw_mult = vd->cs_raw_mult;
	cs_shift = vd->cs_shift;
	cs_mono_mask = vd->cs_mono_mask;

	sec = vd->raw_time_sec;
	ns = vd->raw_time_nsec;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	ns += get_clock_shifted_nsec(cycle_last, cs_raw_mult, cs_mono_mask);
	ns >>= cs_shift;
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

/*
 * Handles CLOCK_REALTIME_COARSE - This is a version of CLOCK_REALTIME
 * at a lower resolution.
 */
static notrace void do_realtime_coarse(const struct vdso_data *vd,
				       struct __vdso_timespec *ts)
{
	u32 seq;
	u64 ns, sec;

repeat:
	seq = vdso_read_begin(vd);
	sec = vd->xtime_coarse_sec;
	ns = vd->xtime_coarse_nsec;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	ts->tv_sec = sec;
	ts->tv_nsec = ns;
}

/*
 * Handles CLOCK_MONOTONIC_COARSE - This is a version of CLOCK_MONOTONIC
 * at a lower resolution.
 */
static notrace void do_monotonic_coarse(const struct vdso_data *vd,
					struct __vdso_timespec *ts)
{
	u32 seq;
	u64 ns, wtm_ns, sec, wtm_sec;

repeat:
	seq = vdso_read_begin(vd);

	sec = vd->xtime_coarse_sec;
	ns = vd->xtime_coarse_nsec;
	wtm_sec = vd->wtm_clock_sec;
	wtm_ns = vd->wtm_clock_nsec;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	sec += wtm_sec;
	ns += wtm_ns;
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;
}

/*
 * Handles CLOCK_BOOTTIME - This is a version of CLOCK_MONOTONIC that keeps
 * into account the time spent in suspend mode.
 * Available on on 2.6.39+ kernel version.
 */
static notrace int do_boottime(const struct vdso_data *vd,
			       struct __vdso_timespec *ts)
{
	return __do_monotonic(vd, ts, true);
}

/*
 * This hook allows the architecture to validate the arguments
 * passed to the library.
 */
#ifndef __HAVE_VDSO_ARCH_VALIDATE_ARG
#define __arch_valid_arg(x)	true
#endif

static notrace int __cvdso_clock_gettime(clockid_t clock,
					 struct __vdso_timespec *ts)
{
	const struct vdso_data *vd = __arch_get_vdso_data();

	if (!__arch_valid_arg(ts))
		return -EFAULT;

	switch (clock) {
	case CLOCK_REALTIME:
		if (do_realtime(vd, ts))
			goto fallback;
		break;
	case CLOCK_TAI:
		if (do_tai(vd, ts))
			goto fallback;
		break;
	case CLOCK_MONOTONIC:
		if (do_monotonic(vd, ts))
			goto fallback;
		break;
	case CLOCK_MONOTONIC_RAW:
		if (do_monotonic_raw(vd, ts))
			goto fallback;
		break;
	case CLOCK_BOOTTIME:
		if (do_boottime(vd, ts))
			goto fallback;
		break;
	case CLOCK_REALTIME_COARSE:
		do_realtime_coarse(vd, ts);
		break;
	case CLOCK_MONOTONIC_COARSE:
		do_monotonic_coarse(vd, ts);
		break;
	default:
		goto fallback;
	}

	return 0;
fallback:
	return clock_gettime_fallback(clock, ts);
}

static notrace int __cvdso_gettimeofday(struct __vdso_timeval *tv,
					struct timezone *tz)
{
	const struct vdso_data *vd = __arch_get_vdso_data();

	if (likely(tv != NULL)) {
		struct __vdso_timespec ts;

		if (do_realtime(vd, &ts))
			return gettimeofday_fallback(tv, tz);

		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}

	if (unlikely(tz != NULL)) {
		tz->tz_minuteswest = vd->tz_minuteswest;
		tz->tz_dsttime = vd->tz_dsttime;
	}

	return 0;
}

#ifdef VDSO_HAS_TIME
static notrace time_t __cvdso_time(time_t *time)
{
	u32 seq;
	time_t t;
	const struct vdso_data *vd = __arch_get_vdso_data();

	if (!__arch_valid_arg(time))
		return -EFAULT;

repeat:
	seq = vdso_read_begin(vd);

	t = vd->xtime_coarse_sec;

	if (unlikely(vdso_read_retry(vd, seq)))
		goto repeat;

	if (unlikely(time != NULL))
		*time = t;

	return t;
}
#endif /* VDSO_HAS_TIME */

static notrace int __cvdso_clock_getres(clockid_t clock_id,
					struct __vdso_timespec *res)
{
	u64 ns;

	if (!__arch_valid_arg(res))
		return -EFAULT;

	if (clock_id == CLOCK_REALTIME ||
	    clock_id == CLOCK_TAI ||
	    clock_id == CLOCK_BOOTTIME ||
	    clock_id == CLOCK_MONOTONIC ||
	    clock_id == CLOCK_MONOTONIC_RAW)
		ns = MONOTONIC_RES_NSEC;
	else if (clock_id == CLOCK_REALTIME_COARSE ||
		 clock_id == CLOCK_MONOTONIC_COARSE)
		ns = LOW_RES_NSEC;
	else
		return clock_getres_fallback(clock_id, res);

	if (res) {
		res->tv_sec = 0;
		res->tv_nsec = ns;
	}

	return 0;
}

static notrace time_t __cvdso_time(time_t *t)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	time_t result = READ_ONCE(vd->xtime_coarse_sec);

	if (t)
		*t = result;
	return result;
}
