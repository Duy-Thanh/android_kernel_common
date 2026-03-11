/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVHE_KASAN_KASAN_H
#define __NVHE_KASAN_KASAN_H

/*
 * Exported functions for interfaces called from assembly or from generated
 * code. Declared here to avoid warnings about missing declarations.
 */

void __asan_register_globals(void *globals, ssize_t size);
void __asan_unregister_globals(void *globals, ssize_t size);
void __asan_handle_no_return(void);

void __asan_load1(void *);
void __asan_store1(void *);
void __asan_load2(void *);
void __asan_store2(void *);
void __asan_load4(void *);
void __asan_store4(void *);
void __asan_load8(void *);
void __asan_store8(void *);
void __asan_load16(void *);
void __asan_store16(void *);
void __asan_loadN(void *, ssize_t size);
void __asan_storeN(void *, ssize_t size);

void __asan_load1_noabort(void *);
void __asan_store1_noabort(void *);
void __asan_load2_noabort(void *);
void __asan_store2_noabort(void *);
void __asan_load4_noabort(void *);
void __asan_store4_noabort(void *);
void __asan_load8_noabort(void *);
void __asan_store8_noabort(void *);
void __asan_load16_noabort(void *);
void __asan_store16_noabort(void *);
void __asan_loadN_noabort(void *, ssize_t size);
void __asan_storeN_noabort(void *, ssize_t size);

void __asan_report_load1_noabort(void *);
void __asan_report_store1_noabort(void *);
void __asan_report_load2_noabort(void *);
void __asan_report_store2_noabort(void *);
void __asan_report_load4_noabort(void *);
void __asan_report_store4_noabort(void *);
void __asan_report_load8_noabort(void *);
void __asan_report_store8_noabort(void *);
void __asan_report_load16_noabort(void *);
void __asan_report_store16_noabort(void *);
void __asan_report_load_n_noabort(void *, ssize_t size);
void __asan_report_store_n_noabort(void *, ssize_t size);

void *__asan_memset(void *addr, int c, ssize_t len);
void *__asan_memcpy(void *dest, const void *src, ssize_t len);

bool hyp_alloc_check_range(const volatile void *p, size_t size);

#endif /* __NVHE_KASAN_KASAN_H */