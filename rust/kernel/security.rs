// SPDX-License-Identifier: GPL-2.0

//! Linux Security Modules (LSM).
//!
//! C header: [`include/linux/security.h`](../../../../include/linux/security.h).
use crate::{
    bindings,
    cred::Credential,
    error::{to_result, Result},
};

/// Calls the security modules to determine if the given task can become the manager of a binder
/// context.
pub fn binder_set_context_mgr(mgr: &Credential) -> Result {
    // SAFETY: `mrg.0` is valid because the shared reference guarantees a nonzero refcount.
    to_result(unsafe { bindings::security_binder_set_context_mgr(mgr.0.get()) })
}
