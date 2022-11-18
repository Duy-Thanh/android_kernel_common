/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_LIB_LKL_KASAN_H
#define _LKL_LIB_LKL_KASAN_H

#include <lkl.h>
#include <lkl/asm/config.h>

#ifdef LKL_CONFIG_KASAN

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_SCALE_SIZE (1UL << KASAN_SHADOW_SCALE_SHIFT)
#define KASAN_SHADOW_MASK (KASAN_SHADOW_SCALE_SIZE - 1)

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

#ifndef PAGE_MASK
#define PAGE_MASK 0xfffffffffffff000L
#endif

#ifndef KASAN_KMALLOC_FREE
#define KASAN_KMALLOC_FREE 0xFB /* object was freed (kmem_cache_free/kfree) */
#endif

// TODO(b/168521290): remove this temporary workaround once issue b/168521290 is
// resolved.
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define KASAN_POISON_SHADOW_MEMORY 0x00
#define KASAN_UNPOISON_SHADOW_MEMORY 0x01

// Reserve a memory range in the virtual address space of a the process at given
// address for KASan shadow memory without committing (i.e. no actual pages are
// allocated for the reserved memory).
void *kasan_lkl_init_shadow_memory(void *addr, unsigned long size);

// Release the previously reserved KASan shadow memory.
int kasan_lkl_release_shadow_memory(void *addr, unsigned long size);

// Map a given memory range in the reserved KASan memory region.
int kasan_lkl_map_shadow_memory(void *addr, unsigned long size);

// Unpoison a previously mapped KASan shadow memory.
void kasan_lkl_unpoison_shadow_memory(void *addr, unsigned long size);

// Poison a previously mapped KASan shadow memory.
void kasan_lkl_poison_shadow_memory(void *addr, unsigned long size,
                                       unsigned char value);

#endif // LKL_CONFIG_KASAN

#endif /* _LKL_LIB_LKL_KASAN_H */
