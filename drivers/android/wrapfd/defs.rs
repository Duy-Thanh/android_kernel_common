// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::mem::MaybeUninit;
use core::ops::{Deref, DerefMut};

use kernel::{
    transmute::{AsBytes, FromBytes},
    uapi,
};

// Shamelessly stolen from binder driver.
macro_rules! decl_wrapper {
    ($newname:ident, $wrapped:ty) => {
        // Define a wrapper around the C type. Use `MaybeUninit` to enforce that the value of
        // padding bytes must be preserved.
        #[derive(Copy, Clone)]
        #[repr(transparent)]
        pub(crate) struct $newname(MaybeUninit<$wrapped>);

        // SAFETY: This macro is only used with types where this is ok.
        unsafe impl FromBytes for $newname {}
        // SAFETY: This macro is only used with types where this is ok.
        unsafe impl AsBytes for $newname {}

        impl Deref for $newname {
            type Target = $wrapped;
            fn deref(&self) -> &Self::Target {
                // SAFETY: We use `MaybeUninit` only to preserve padding. The value must still
                // always be valid.
                unsafe { self.0.assume_init_ref() }
            }
        }

        impl DerefMut for $newname {
            fn deref_mut(&mut self) -> &mut Self::Target {
                // SAFETY: We use `MaybeUninit` only to preserve padding. The value must still
                // always be valid.
                unsafe { self.0.assume_init_mut() }
            }
        }

        impl Default for $newname {
            fn default() -> Self {
                // Create a new value of this type where all bytes (including padding) are zeroed.
                Self(MaybeUninit::zeroed())
            }
        }
    };
}

decl_wrapper!(WrapFdWrap, uapi::wrapfd_wrap);
decl_wrapper!(WrapFdGetState, uapi::wrapfd_get_state);
decl_wrapper!(WrapFdLoad, uapi::wrapfd_load);
decl_wrapper!(WrapFdRewrap, uapi::wrapfd_rewrap);

impl core::fmt::Debug for WrapFdWrap {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "WrapFdWrap {{ fd: {}, prot: {} }}", self.fd, self.prot)
    }
}

impl core::fmt::Debug for WrapFdGetState {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "WrapFdGetState {{ state: {} }}", self.state)
    }
}
