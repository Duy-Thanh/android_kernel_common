// SPDX-License-Identifier: GPL-2.0
/*
 * A hack to export Rust symbols for loadable modules without having to redo
 * the entire `include/linux/export.h` logic in Rust.
 *
 * This requires the Rust's new/future `v0` mangling scheme because the default
 * one ("legacy") uses invalid characters for C identifiers (thus we cannot use
 * the `EXPORT_SYMBOL_*` macros).
 *
 * All symbols are exported as GPL-only to guarantee no GPL-only feature is
 * accidentally exposed.
 */

#include <linux/export.h>

#define EXPORT_SYMBOL_RUST_GPL(sym) extern int sym; EXPORT_SYMBOL_GPL(sym)

#include "exports_core_generated.h"
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#include "exports_bindings_generated.h"
#include "exports_kernel_generated.h"

#ifndef CONFIG_RUST_INLINE_HELPERS
#include "exports_helpers_generated.h"
#endif
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
#include "exports_helpers_generated.h"
#include "exports_bindings_generated.h"
#include "exports_kernel_generated.h"
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

// For modules using `rust/build_error.rs`.
#ifdef CONFIG_RUST_BUILD_ASSERT_ALLOW
EXPORT_SYMBOL_RUST_GPL(rust_build_error);
#endif
