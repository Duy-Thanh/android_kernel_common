/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic arch dependent ring_buffer macros.
 */
#ifndef __ASM_GENERIC_RING_BUFFER_H__
#define __ASM_GENERIC_RING_BUFFER_H__

#include <linux/cacheflush.h>

<<<<<<< HEAD   (7542d32487b53ed1a59d02f013a6e6b66804c874 Merge 4c77bb95628f ("iommu: Skip PASID validation for device)
/* Flush cache on ring buffer range if needed */
#define arch_ring_buffer_flush_range(start, end)	flush_cache_vmap(start, end)
||||||| BASE   (4c77bb95628f85a9080aa24e54897c811ba2c21b iommu: Skip PASID validation for devices without PASID capab)
=======
/* Flush cache on ring buffer range if needed. Do nothing by default. */
#define arch_ring_buffer_flush_range(start, end)	do { } while (0)
>>>>>>> BRANCH (1d3a00d3bacff25652c96e1527610c69e91f7c38 Linux 6.12.93)

#endif /* __ASM_GENERIC_RING_BUFFER_H__ */
