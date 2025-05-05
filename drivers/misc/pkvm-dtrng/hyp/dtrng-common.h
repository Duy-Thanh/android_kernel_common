// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#ifndef __DTRNG_COMMON_H__
#define __DTRNG_COMMON_H__

#include <asm/kvm_pkvm_module.h>

extern const struct pkvm_module_ops *mod_ops;

#define CALL_FROM_OPS(fn, ...) mod_ops->fn(__VA_ARGS__)

#define hyp_puts(s) CALL_FROM_OPS(puts, s)
#define hyp_putx64(x) CALL_FROM_OPS(putx64, x)
#define hyp_fixmap_map(phys) CALL_FROM_OPS(fixmap_map, phys)
#define hyp_fixmap_unmap() CALL_FROM_OPS(fixmap_unmap, )
#define __pkvm_register_guest_hvc_handler(h) \
	CALL_FROM_OPS(register_guest_hvc_handler, h)
#define _hyp_smp_processor_id() CALL_FROM_OPS(hyp_smp_processor_id, )

#define mod_debug(s) hyp_puts("[pkvm-dtrng][DEBUG]" s)
#define mod_error(s) hyp_puts("[pkvm-dtrng][ERROR]" s);

#ifdef memcpy
#undef memcpy
#endif

#ifdef memset
#undef memset
#endif

void *memcpy(void *to, const void *from, size_t count);

void *memset(void *dst, int c, size_t count);

#endif
