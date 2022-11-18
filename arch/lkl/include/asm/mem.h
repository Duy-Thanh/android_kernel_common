/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LKL_MEM_H__
#define __LKL_MEM_H__

extern unsigned long memory_start;

static inline unsigned long to_phys(void *virt)
{
	return(((unsigned long) virt - memory_start));
}

static inline void *to_virt(unsigned long phys)
{
	return((void *)memory_start + phys);
}

#endif // __LKL_MEM_H__
