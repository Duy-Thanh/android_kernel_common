/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SYSCALL_WRAPPER_H
#define __ASM_SYSCALL_WRAPPER_H

#include <linux/types.h>

struct pt_regs;

#define __SYSCALL_DEFINEx(x, name, ...)                                        \
	__SYSCALL_DEFINE_ARCH(x, name, __VA_ARGS__)                            \
	__diag_push();                                                         \
	__diag_ignore(GCC, 8, "-Wattribute-alias",                             \
		      "Type aliasing is used to sanitize syscall arguments");  \
	asmlinkage long sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))            \
		__attribute__((alias(__stringify(__se_sys##name))));           \
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);                               \
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__));   \
	asmlinkage long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__));      \
	asmlinkage long __se_sys##name(__MAP(x, __SC_LONG, __VA_ARGS__))       \
	{                                                                      \
		long ret = __do_sys##name(__MAP(x, __SC_CAST, __VA_ARGS__));   \
		__MAP(x, __SC_TEST, __VA_ARGS__);                              \
		__PROTECT(x, ret, __MAP(x, __SC_ARGS, __VA_ARGS__));           \
		return ret;                                                    \
	}                                                                      \
	__diag_pop();                                                          \
	static inline long __do_sys##name(__MAP(x, __SC_DECL, __VA_ARGS__))

#define SYSCALL_DEFINE0(sname)                                                 \
	SYSCALL_METADATA(_##sname, 0);                                         \
	__SYSCALL_DEFINE_ARCH(0, _##sname);                                    \
	asmlinkage long sys_##sname(void);                                     \
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);                             \
	asmlinkage long sys_##sname(void)

#endif /* __ASM_SYSCALL_WRAPPER_H */
