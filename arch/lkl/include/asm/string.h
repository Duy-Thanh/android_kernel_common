/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_STRING_H
#define _ASM_LKL_STRING_H

#include <asm/types.h>
#include <asm/host_ops.h>

#define __HAVE_ARCH_MEMCPY
void *memcpy(void *to, const void *from, size_t len);
static inline void *__memcpy(void *dest, const void *src, size_t count)
{
	return lkl_ops->memcpy(dest, src, count);
}

#define __HAVE_ARCH_MEMSET
void *memset(void *s, int c, size_t n);
static inline void *__memset(void *s, int c, size_t n)
{
	return lkl_ops->memset(s, c, n);
}

#define __HAVE_ARCH_MEMMOVE
void *memmove(void *dest, const void *src, size_t count);
static inline void *__memmove(void *dest, const void *src, size_t count)
{
	return lkl_ops->memmove(dest, src, count);
}

#if defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__)
/*
 * For files that not instrumented (e.g. mm/slub.c) we
 * should use not instrumented version of mem* functions.
 */
#undef memcpy
#define memcpy(dst, src, len) __memcpy(dst, src, len)
#define memmove(dst, src, len) __memmove(dst, src, len)
#define memset(s, c, n) __memset(s, c, n)

#ifndef __NO_FORTIFY
#define __NO_FORTIFY /* FORTIFY_SOURCE uses __builtin_memcpy, etc. */
#endif

#endif /* defined(CONFIG_KASAN) && !defined(__SANITIZE_ADDRESS__) */

#endif /* _ASM_LKL_STRING_H */
