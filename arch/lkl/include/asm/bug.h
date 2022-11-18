/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_BUG_H
#define _ASM_LKL_BUG_H

#include <linux/stringify.h>

extern void lkl_arch_bug(const char* msg);
extern void lkl_arch_warn(const char* msg);

#define HAVE_ARCH_BUG
#define BUG() \
    do { \
        lkl_arch_bug("failure at "__FILE__":" __stringify(__LINE__)); \
        unreachable(); \
    } while (0)

#define __WARN_FLAGS(flags) \
    do { \
        lkl_arch_warn("warning at "__FILE__":" __stringify(__LINE__)); \
    } while (0)

#include <asm-generic/bug.h>

#endif /* _ASM_LKL_BUG_H */
