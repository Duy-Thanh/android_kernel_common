/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_P3S_TLBFLUSH_H
#define _ASM_ARM64_P3S_TLBFLUSH_H

#include <linux/p3s/mm.h>

#define get_trans_granule_mm(mm) \
	(mm_is_compat(mm) ? TLBI_TTL_TG_4K : get_trans_granule())

#define __tlbi_level_tg(op, addr, level, tg) do {			\
	u64 arg = addr;							\
									\
	if (alternative_has_cap_unlikely(ARM64_HAS_ARMv8_4_TTL) &&	\
	    level >= 0 && level <= 3) {					\
		u64 ttl = level & 3;					\
		ttl |= (tg) << 2;					\
		arg &= ~TLBI_TTL_MASK;					\
		arg |= FIELD_PREP(TLBI_TTL_MASK, ttl);			\
	}								\
									\
	__tlbi(op, arg);						\
} while (0)

#define __tlbi_user_level_tg(op, arg, level, tg) do {				\
	if (arm64_kernel_unmapped_at_el0())					\
		__tlbi_level_tg(op, (arg | USER_ASID_FLAG), level, tg);		\
} while (0)

#define __TLBI_VADDR_RANGE_TG(baddr, asid, scale, num, ttl, tg)		\
	({								\
		unsigned long __ta = 0;					\
		unsigned long __ttl = (ttl >= 1 && ttl <= 3) ? ttl : 0;	\
		__ta |= FIELD_PREP(TLBIR_BADDR_MASK, baddr);		\
		__ta |= FIELD_PREP(TLBIR_TTL_MASK, __ttl);		\
		__ta |= FIELD_PREP(TLBIR_NUM_MASK, num);		\
		__ta |= FIELD_PREP(TLBIR_SCALE_MASK, scale);		\
		__ta |= FIELD_PREP(TLBIR_TG_MASK, tg);			\
		__ta |= FIELD_PREP(TLBIR_ASID_MASK, asid);		\
		__ta;							\
	})

#endif /* _ASM_ARM64_P3S_TLBFLUSH_H */
