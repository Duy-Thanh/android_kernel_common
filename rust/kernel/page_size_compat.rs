// SPDX-License-Identifier: GPL-2.0

//! x86_64 page size compat

use crate::bindings;

/// void __page_align(size_t addr);
///
/// Safety
///
/// ____page_shift() checks a static key to determine if the page size is
/// being emulated or not.
pub fn __page_align(addr: usize) -> usize {
    unsafe {
        // SAFETY: ____page_shift accesses a static key.
        let __page_size = 1usize << bindings::____page_shift();
        let __page_mask = !(__page_size - 1);
        // Parentheses around `__page_size - 1` to avoid triggering overflow
        // sanitizers in the wrong cases.
        (addr + (__page_size - 1)) & __page_mask
    }
}
