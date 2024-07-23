// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! This module defines the global mutex.
//!
//! Since the `kernel::sync` bindings currently don't support mutexes in globals, we use a
//! temporary workaround. The idea is that we create a global for holding the mutex, but don't
//! initialize it until the module is initialized. Once proper support for global mutexes is added,
//! this code should be replaced with that. Unfortunately, the workaround will require some extra
//! unsafe code.

#![allow(unused_imports)]

use crate::AshmemLru;
use core::cell::UnsafeCell;
use core::mem::MaybeUninit;
use kernel::init::PinInit;
use kernel::list::List;
use kernel::sync::lock::mutex::{Mutex, MutexBackend};
use kernel::sync::lock::Guard;

/// A wrapper used to define a mutex in a global.
pub(crate) struct AshmemMutex {
    inner: UnsafeCell<MaybeUninit<Mutex<AshmemLru>>>,
}

impl AshmemMutex {
    /// # Safety
    ///
    /// The caller must call `init` before the first call to `lock`. The caller must only define
    /// one `AshmemMutex`.
    pub(crate) const unsafe fn new() -> Self {
        AshmemMutex {
            inner: UnsafeCell::new(MaybeUninit::uninit()),
        }
    }

    /// Called when the module is initialized.
    ///
    /// # Safety
    ///
    /// Must only be called once.
    ///
    /// The struct must not be moved after this call.
    pub(crate) unsafe fn init(&self) {
        let ptr = self.inner.get() as *mut Mutex<()>;
        let init = kernel::new_mutex!((), "AshmemMutex");

        // SAFETY: The caller guarantees that they only call this method once, and that all
        // calls to `lock` happen after this call. These are the only ways to access `inner`,
        // so there is no data race when we perform unsynchronized access to `inner` here.
        //
        // Additionally, the caller promised to not move this struct after this call to `init`,
        // so it's okay to use a pinned initializer here.
        match unsafe { init.__pinned_init(ptr) } {
            Ok(()) => {}
            Err(e) => match e {},
        }
    }

    pub(crate) fn lock(&self) -> Guard<'_, AshmemLru, MutexBackend> {
        let ptr = self.inner.get() as *const Mutex<AshmemLru>;

        // SAFETY: When constructing this type, the caller promised to call `init` before
        // calling `lock`, so the mutex has been intiailized at this point.
        unsafe { (*ptr).lock() }
    }
}

// SAFETY: This allows you to call `lock` from several threads in parallel, but that's okay as
// the mutex will correctly synchronize this access.
unsafe impl Sync for AshmemMutex {}

/// A wrapper around `Guard` for use with the custom `AshmemMutex`.
///
/// We use this instead of using `Guard` directly so that we can use it with `AshmemLockedBy`.
pub(crate) struct AshmemGuard<'a> {
    inner: Guard<'a, AshmemLru, MutexBackend>,
}

impl<'a> core::ops::Deref for AshmemGuard<'a> {
    type Target = AshmemLru;
    #[inline]
    fn deref(&self) -> &AshmemLru {
        &self.inner
    }
}

impl<'a> core::ops::DerefMut for AshmemGuard<'a> {
    #[inline]
    fn deref_mut(&mut self) -> &mut AshmemLru {
        &mut self.inner
    }
}

/// This wrapper indicates that the contents are locked by the global `AshmemMutex`.
pub(crate) struct LockedByAshmem<T> {
    inner: UnsafeCell<T>,
}

unsafe impl<T: Send> Send for LockedByAshmem<T> {}
unsafe impl<T: Send> Sync for LockedByAshmem<T> {}

impl<T> LockedByAshmem<T> {
    /// Create a new value locked by the global `AshmemMutex`.
    pub(crate) fn new(value: T) -> Self {
        Self {
            inner: UnsafeCell::new(value),
        }
    }

    /// Obtain shared access to the inner value.
    #[inline]
    pub(crate) fn get_ref<'a>(&'a self, _guard: &'a AshmemGuard<'_>) -> &'a T {
        // SAFETY: The caller provides shared access to an `AshmemGuard` for the duration of 'a.
        // Since there can only be one `AshmemGuard` and the one guard is borrowed immutably for
        // 'a, there can be no call to `get_mut` during 'a.
        unsafe { &*self.inner.get() }
    }

    /// Obtain exclusive access to the inner value.
    #[inline]
    pub(crate) fn get_mut<'a>(&'a self, _guard: &'a mut AshmemGuard<'_>) -> &'a mut T {
        // SAFETY: The caller provides exclusive access to an `AshmemGuard` for the duration of 'a.
        // Since there can only be one `AshmemGuard` and the one guard is borrowed mutably for 'a,
        // there can be no other call to `get_ref` or `get_mut` during 'a.
        unsafe { &mut *self.inner.get() }
    }
}
