// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#include <linux/types.h>
#include <nvhe/memory.h>

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

/*
 * Do nothing for reports, we are going to hit brk anyway, we
 * can use this in the future to have more information.
 */

#define DEFINE_ASAN_REPORT_LOAD(size)				\
void __asan_report_load##size##_noabort(void *addr)		\
{								\
}

#define DEFINE_ASAN_REPORT_STORE(size)				\
void __asan_report_store##size##_noabort(void *addr)		\
{								\
}

DEFINE_ASAN_REPORT_LOAD(1);
DEFINE_ASAN_REPORT_LOAD(2);
DEFINE_ASAN_REPORT_LOAD(4);
DEFINE_ASAN_REPORT_LOAD(8);
DEFINE_ASAN_REPORT_LOAD(16);
DEFINE_ASAN_REPORT_STORE(1);
DEFINE_ASAN_REPORT_STORE(2);
DEFINE_ASAN_REPORT_STORE(4);
DEFINE_ASAN_REPORT_STORE(8);
DEFINE_ASAN_REPORT_STORE(16);

void __asan_report_load_n_noabort(void *addr, ssize_t size)
{
	WARN_ON(1);
}

void __asan_report_store_n_noabort(void *addr, ssize_t size)
{
	WARN_ON(1);
}

void __asan_handle_no_return(void) {}

void __asan_register_globals(void *ptr, ssize_t size)
{
}

void __asan_unregister_globals(void *ptr, ssize_t size)
{
}

bool alloc_check_range(const volatile void *p, u64 size);

void *__asan_memcpy(void *dest, const void *src, ssize_t len)
{
	if (!alloc_check_range(src, len) || !alloc_check_range(dest, len))
	    return NULL;

	return memcpy(dest, src, len);
}

void *__asan_memset(void *addr, int c, ssize_t len)
{
	if (!alloc_check_range(addr, len))
		return NULL;
	return memset(addr, c, len);
}

#define DEFINE_ASAN_LOAD_STORE(size)					\
	void __asan_load##size(void *addr)				\
	{								\
		WARN_ON(!alloc_check_range(addr, size));			\
	}								\
	__alias(__asan_load##size)					\
	void __asan_load##size##_noabort(void *);			\
	void __asan_store##size(void *addr)				\
	{								\
		WARN_ON(!alloc_check_range(addr, size));			\
	}								\
	__alias(__asan_store##size)					\
	void __asan_store##size##_noabort(void *);			\

DEFINE_ASAN_LOAD_STORE(1);
DEFINE_ASAN_LOAD_STORE(2);
DEFINE_ASAN_LOAD_STORE(4);
DEFINE_ASAN_LOAD_STORE(8);
DEFINE_ASAN_LOAD_STORE(16);

bool __kasan_check_read(const volatile void *p, unsigned int size)
{
	return alloc_check_range(p, size);
}

bool __kasan_check_write(const volatile void *p, unsigned int size)
{
	return alloc_check_range(p, size);
}

void __asan_loadN(void *addr, ssize_t size)
{
	WARN_ON(!alloc_check_range(addr, size));
}

void __asan_storeN(void *addr, ssize_t size)
{
	WARN_ON(!alloc_check_range(addr, size));
}


__alias(__asan_loadN)
void __asan_loadN_noabort(void *, ssize_t);

__alias(__asan_storeN)
void __asan_storeN_noabort(void *, ssize_t);
