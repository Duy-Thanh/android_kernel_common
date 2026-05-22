// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/vmalloc.h>

__rust_helper void * __must_check __realloc_size(2)
rust_helper_vrealloc(const void *p, size_t size, gfp_t flags)
{
	return vrealloc(p, size, flags);
}

__rust_helper bool rust_helper_is_vmalloc_addr(const void *x)
{
	return is_vmalloc_addr(x);
}
