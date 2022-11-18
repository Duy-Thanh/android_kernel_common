/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_LKL_SYSCALL_H
#define _ASM_LKL_SYSCALL_H  1

#include <uapi/linux/audit.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>		/* in_syscall() */

static inline int
syscall_get_arch(struct task_struct *task)
{
#ifdef CONFIG_X86_32
	return AUDIT_ARCH_I386;
#else
	return AUDIT_ARCH_X86_64;
#endif
}

#endif