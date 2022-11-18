/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <malloc.h>
#include <stdlib.h>

#include "lkl_kasan.h"

#define ULONG_MAX (~0UL)

// Store original size of the malloc request in the beginning of the allocated
// buffer. Reserve 16 bytes for storing the size to preserve malloc alignment
// guarantees (8 or 16 byte alignement).
#define LKL_MALLOC_PREFIX_SIZE 16

// TODO(b/171755798): allocate extra space for red zones in the beginning and at
// the end of the buffer for KASan implementation in LKL.

void *lkl_malloc(unsigned long size)
{
	void *mem_buf = NULL;
	if (size > (ULONG_MAX - LKL_MALLOC_PREFIX_SIZE))
		return NULL;
	mem_buf = malloc(size + LKL_MALLOC_PREFIX_SIZE);
#ifdef LKL_CONFIG_KASAN
	if (mem_buf != NULL) {
		*(unsigned long *)mem_buf = size;
		mem_buf = (char *)mem_buf + LKL_MALLOC_PREFIX_SIZE;
		assert(kasan_lkl_map_shadow_memory(mem_buf, size) == 0);
	}
#endif
	return mem_buf;
}

void lkl_free(void *ptr)
{
	size_t size = 0;
	unsigned long original_size = 0;
#ifdef LKL_CONFIG_KASAN
	if (ptr != NULL) {
		assert((unsigned long)ptr > LKL_MALLOC_PREFIX_SIZE);
		ptr = (char *)ptr - LKL_MALLOC_PREFIX_SIZE;
		original_size = *(unsigned long *)ptr;
		size = malloc_usable_size(ptr) - LKL_MALLOC_PREFIX_SIZE;
		// malloc_usable_size can return value which is bigger than
		// the original value of malloc `size` argument.
		assert(size >= original_size);
		// poison the freed memory
		kasan_lkl_poison_shadow_memory(
			(char *)ptr + LKL_MALLOC_PREFIX_SIZE, original_size,
			KASAN_KMALLOC_FREE);
	}
#endif
	free(ptr);
}
