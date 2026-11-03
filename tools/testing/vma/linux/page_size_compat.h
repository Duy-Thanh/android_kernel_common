/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VMA_TEST_PAGE_SIZE_COMPAT_H
#define _VMA_TEST_PAGE_SIZE_COMPAT_H

/* The userspace VMA harness uses native pages, without page-size emulation. */
#include <linux/mm.h>

#define __PAGE_SIZE PAGE_SIZE
#define __PAGE_MASK PAGE_MASK
#define __PAGE_ALIGN(addr) PAGE_ALIGN(addr)
#define __PAGE_SIZE_ROUND_UP_ADJ(size) (size)

#endif
