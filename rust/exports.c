// SPDX-License-Identifier: GPL-2.0
/*
 * Export Rust symbols for loadable modules using similar logic to that in
 * `include/linux/export.h`.
 *
 * This requires Rust's new/future `v0` mangling scheme because the default one
 * ("legacy") uses invalid characters for C identifiers (thus we cannot use the
 * `EXPORT_SYMBOL_*` macros).
 *
 * All symbols are exported as GPL-only to guarantee no GPL-only feature is
 * accidentally exposed. There is no support for DEFAULT_SYMBOL_NAMESPACE.
 */

#include <linux/export.h>

#define EXPORT_SYMBOL_RUST_GPL(sym)				\
	asm(__stringify(___EXPORT_SYMBOL(sym, "GPL", "")))

#include "exports_core_generated.h"
#include "exports_bindings_generated.h"
#include "exports_kernel_generated.h"

#ifndef CONFIG_RUST_INLINE_HELPERS
#include "exports_helpers_generated.h"
#endif

// For modules using `rust/build_error.rs`.
#ifdef CONFIG_RUST_BUILD_ASSERT_ALLOW
EXPORT_SYMBOL_RUST_GPL(rust_build_error);
#endif
