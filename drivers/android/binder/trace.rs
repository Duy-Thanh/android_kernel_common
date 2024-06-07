// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

use crate::transaction::Transaction;

use kernel::bindings::rust_binder_transaction;
use kernel::tracepoint::declare_trace;

declare_trace! {
    fn rust_binder_transaction(reply: bool, t: rust_binder_transaction);
}

#[inline]
fn raw_transaction(t: &Transaction) -> rust_binder_transaction {
    t as *const Transaction as rust_binder_transaction
}

#[inline]
pub(crate) fn trace_transaction(reply: bool, t: &Transaction) {
    // SAFETY: The raw transaction is valid for the duration of this call.
    unsafe { rust_binder_transaction(reply, raw_transaction(t)) }
}
