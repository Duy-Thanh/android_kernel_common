// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

use kernel::tracepoint::declare_trace;

use core::ffi::c_int;

declare_trace! {
    fn rust_binder_transaction(debug_id: c_int, reply: bool);
}

#[inline]
pub(crate) fn trace_transaction(debug_id: usize, reply: bool) {
    // SAFETY: No safety requirements for this tracepoint.
    unsafe { rust_binder_transaction(debug_id as c_int, reply) }
}
