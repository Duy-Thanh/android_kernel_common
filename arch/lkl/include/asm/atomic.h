/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_ATOMIC_H
#define _ASM_LKL_ATOMIC_H

#include <linux/types.h>

#define ATOMIC64_INIT(i)	{ (i) }

extern s64 arch_atomic64_read(const atomic64_t *v);
extern void arch_atomic64_set(atomic64_t *v, s64 i);

#define arch_atomic64_set_release(v, i)	arch_atomic64_set((v), (i))

#define ATOMIC64_OP(op)							\
extern void	 arch_atomic64_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OP_RETURN(op)						\
extern s64 arch_atomic64_##op##_return(s64 a, atomic64_t *v);

#define ATOMIC64_FETCH_OP(op)						\
extern s64 arch_atomic64_fetch_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_OP_RETURN(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(add)
ATOMIC64_OPS(sub)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

extern s64 arch_atomic64_dec_if_positive(atomic64_t *v);
#define arch_atomic64_dec_if_positive arch_atomic64_dec_if_positive
extern s64 arch_atomic64_cmpxchg(atomic64_t *v, s64 o, s64 n);
extern s64 arch_atomic64_xchg(atomic64_t *v, s64 new);
extern s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u);
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless

#include <asm-generic/atomic.h>

#endif // _ASM_LKL_ATOMIC_H