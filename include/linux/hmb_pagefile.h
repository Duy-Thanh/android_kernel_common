/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Haven Memory Bus (HMB) - Native In-Tree Paging Engine
 * Implementation of HMB Spec 2.0 for Android Common Kernel (ACK) 6.6 VMM
 *
 * Copyright (C) 2026 Daniel Elliott / Barrer Software Org.
 */

#ifndef _LINUX_HMB_PAGEFILE_H
#define _LINUX_HMB_PAGEFILE_H

#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/swap.h>

#define HMB_PLANE_SIZE_MB 4096
#define HMB_PLANE_SIZE_BYTES ((u64)HMB_PLANE_SIZE_MB << 20)

#ifdef CONFIG_HMB_PAGEFILE

bool hmb_pagefile_store(struct folio *folio);
bool hmb_pagefile_load(struct folio *folio);
void hmb_pagefile_free(swp_entry_t entry);
int hmb_pagefile_init(void);

#else /* !CONFIG_HMB_PAGEFILE */

static inline bool hmb_pagefile_store(struct folio *folio) { return false; }
static inline bool hmb_pagefile_load(struct folio *folio) { return false; }
static inline void hmb_pagefile_free(swp_entry_t entry) {}
static inline int hmb_pagefile_init(void) { return 0; }

#endif /* CONFIG_HMB_PAGEFILE */

#endif /* _LINUX_HMB_PAGEFILE_H */
