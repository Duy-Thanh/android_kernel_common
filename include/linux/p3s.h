/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_H
#define _LINUX_P3S_H

#include <linux/p3s/const.h>
#include <linux/p3s/mm.h>
#include <linux/p3s/vma.h>
#include <linux/p3s/mremap.h>

#ifdef CONFIG_ARM64
#include <asm/p3s/pgtable.h>
#include <asm/p3s/mmu.h>
#endif

#endif /* _LINUX_P3S_H */
