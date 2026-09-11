/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic arch dependent ring_buffer macros.
 */
#ifndef __ASM_GENERIC_RING_BUFFER_H__
#define __ASM_GENERIC_RING_BUFFER_H__

#include <linux/cacheflush.h>

<<<<<<< HEAD   (784d24787d06d4e031849c40aa060b83a4e2273d Revert "serdev: Provide a bustype shutdown function")
/* Flush cache on ring buffer range if needed */
#define arch_ring_buffer_flush_range(start, end)	flush_cache_vmap(start, end)
||||||| BASE   (6da213b4fa93b49859789c70cf793c21cf66082c ice: fix VF queue configuration with low MTU values)
=======
/* Flush cache on ring buffer range if needed. Do nothing by default. */
#define arch_ring_buffer_flush_range(start, end)	do { } while (0)
>>>>>>> BRANCH (af72721664886d48fdd72735f80f8ffbb3935cef ethtool: cmis_cdb: Fix incorrect read / write length extensi)

#endif /* __ASM_GENERIC_RING_BUFFER_H__ */
