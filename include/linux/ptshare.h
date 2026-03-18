/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTSHARE_H
#define _LINUX_PTSHARE_H

#include <linux/refcount.h>

struct mm_struct;

struct ptshare_desc {
	/* The headless shadow MM for this domain */
	struct mm_struct *ptshare_mm;
	refcount_t refcount;
};

#endif /* _LINUX_PTSHARE_H */
