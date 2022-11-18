/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "lkl_kasan.h"

#ifdef LKL_CONFIG_KASAN

static inline void *kasan_mem_to_shadow(const void *addr)
{
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT) +
	       LKL_CONFIG_KASAN_SHADOW_OFFSET;
}

static inline void *kasan_mem_to_shadow_page_align(const void *addr,
						   unsigned long size,
						   unsigned long *shadow_size)
{
	void *shadow_start =
		(void *)((unsigned long)kasan_mem_to_shadow(addr) & PAGE_MASK);
	void *shadow_end = kasan_mem_to_shadow((const char *)addr + size);
	if (((unsigned long)shadow_end & (PAGE_SIZE - 1)) != 0)
		shadow_end =
			(void *)(((unsigned long)shadow_end + PAGE_SIZE - 1) &
				 PAGE_MASK);
	else
		shadow_end = (void *)((unsigned long)shadow_end + PAGE_SIZE);
	*shadow_size = (unsigned long)shadow_end - (unsigned long)shadow_start;
	return shadow_start;
}

// it is assumed that addr is 8-bytes aligned
static void kasan_update_shadow_memory(void *addr, unsigned long size,
				       int unpoison, unsigned char value)
{
	void *shadow_memory = kasan_mem_to_shadow(addr);
	memset(shadow_memory, value, size >> KASAN_SHADOW_SCALE_SHIFT);
	if (size & KASAN_SHADOW_MASK) {
		unsigned char *shadow =
			(unsigned char *)kasan_mem_to_shadow(addr + size);
		if (unpoison) {
			*shadow = (unsigned char)(size & KASAN_SHADOW_MASK);
		} else {
			*shadow = value;
		}
	}
}

void kasan_lkl_unpoison_shadow_memory(void *addr, unsigned long size)
{
	kasan_update_shadow_memory(addr, size, KASAN_UNPOISON_SHADOW_MEMORY,
				   0x00);
}

void kasan_lkl_poison_shadow_memory(void *addr, unsigned long size,
				       unsigned char value)
{
	kasan_update_shadow_memory(addr, size, KASAN_POISON_SHADOW_MEMORY,
				   value);
}

void *kasan_lkl_init_shadow_memory(void *addr, unsigned long size)
{
	void *result;
	result = mmap(addr, size, PROT_NONE,
		      MAP_ANON | MAP_PRIVATE | MAP_FIXED_NOREPLACE, 0, 0);
	if (result == MAP_FAILED)
		return NULL;
	return result;
}

int kasan_lkl_release_shadow_memory(void *addr, unsigned long size)
{
	return munmap(addr, size);
}

int kasan_lkl_map_shadow_memory(void *addr, unsigned long size)
{
	void *aligned_shadow = NULL;
	unsigned long aligned_shadow_size = 0;

	aligned_shadow = kasan_mem_to_shadow_page_align(addr, size,
							&aligned_shadow_size);
	if (mprotect(aligned_shadow, aligned_shadow_size,
		     PROT_READ | PROT_WRITE) != 0)
		return -1;

	// make sure shadow memory is unpoisoned
	kasan_lkl_unpoison_shadow_memory(addr, size);
	return 0;
}

#endif // LKL_CONFIG_KASAN
