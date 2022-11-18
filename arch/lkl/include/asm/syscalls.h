/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_SYSCALLS_H
#define _ASM_LKL_SYSCALLS_H

int syscalls_init(void);
void syscalls_cleanup(void);
long lkl_syscall(long no, long *params);
void wakeup_idle_host_task(void);

#define LKL_TASK_NEW_TGID 0x01

#define LKL_TASK_FLAG_MASK (LKL_TASK_NEW_TGID)

void *lkl_get_task(void);
int lkl_set_task(void *task);
int lkl_set_task_flag(unsigned long flag);

#define sys_mmap sys_mmap_pgoff
#define sys_mmap2 sys_mmap_pgoff
#define sys_clone sys_ni_syscall
#define sys_vfork sys_ni_syscall
#define sys_rt_sigreturn sys_ni_syscall

#include <asm-generic/syscalls.h>

#endif /* _ASM_LKL_SYSCALLS_H */
