/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic arch dependent ring_buffer macros.
 */
#ifndef __ASM_GENERIC_RING_BUFFER_H__
#define __ASM_GENERIC_RING_BUFFER_H__

#include <linux/cacheflush.h>

<<<<<<< HEAD   (c7efc721bef51031a89668b27d8952fc3ac324d7 Merge 6da213b4fa93 ("ice: fix VF queue configuration with lo)
/* Flush cache on ring buffer range if needed */
#define arch_ring_buffer_flush_range(start, end)	flush_cache_vmap(start, end)
||||||| BASE   (6da213b4fa93b49859789c70cf793c21cf66082c ice: fix VF queue configuration with low MTU values)
=======
/* Flush cache on ring buffer range if needed. Do nothing by default. */
#define arch_ring_buffer_flush_range(start, end)	do { } while (0)
>>>>>>> BRANCH (dc518afa8eb71ffb7bcf8aef0ce2d9338df87d07 ring-buffer: Flush and stop persistent ring buffer on panic)

#endif /* __ASM_GENERIC_RING_BUFFER_H__ */
