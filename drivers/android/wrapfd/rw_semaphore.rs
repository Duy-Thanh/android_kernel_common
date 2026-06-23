// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};

use kernel::{bindings, pin_init, prelude::*, static_lock_class, types::Opaque};

#[pin_data]
pub(crate) struct RwSemaphore<T> {
    #[pin]
    sem: Opaque<bindings::rw_semaphore>,
    data: UnsafeCell<T>,
}

impl<T> RwSemaphore<T> {
    pub(crate) fn new(data: T) -> impl PinInit<Self> {
        pin_init!(Self {
            // ffi_init passes a *mut rw_semaphore directly to the closure,
            // and provides a concrete error type that fixes the macro's type inference.
            sem <- Opaque::ffi_init(|slot| unsafe {
                bindings::__init_rwsem(
                    slot,
                    c"RwSemaphore".as_ptr() as _,
                    static_lock_class!().as_ptr(),
                );
            }),
            data: UnsafeCell::new(data),
        })
    }

    fn lock_write(&self) {
        // SAFETY: It's always safe to call down_write on a pointer to a valid rw_semaphore.
        unsafe { bindings::down_write(self.sem.get()) };
    }

    fn try_lock_write(&self) -> bool {
        // SAFETY: It's always safe to call down_write_trylock on a pointer to a valid rw_semaphore.
        (unsafe { bindings::down_write_trylock(self.sem.get()) }) != 0
    }

    fn unlock_write(&self) {
        // SAFETY: It's always safe to call up_write on a pointer to a valid rw_semaphore.
        unsafe { bindings::up_write(self.sem.get()) };
    }

    pub(crate) fn try_write(&self) -> Result<WriteGuard<'_, T>> {
        if !self.try_lock_write() {
            return Err(EBUSY);
        }

        Ok(WriteGuard::new(self))
    }

    pub(crate) fn write(&self) -> WriteGuard<'_, T> {
        self.lock_write();
        WriteGuard::new(self)
    }

    fn lock_read(&self) {
        // SAFETY: It's always safe to call down_read on a pointer to a valid rw_semaphore.
        unsafe { bindings::down_read(self.sem.get()) };
    }

    fn try_lock_read(&self) -> bool {
        // SAFETY: It's always safe to call down_read_trylock on a pointer to a valid rw_semaphore.
        (unsafe { bindings::down_read_trylock(self.sem.get()) }) != 0
    }

    fn unlock_read(&self) {
        // SAFETY: It's always safe to call up_read on a pointer to a valid rw_semaphore.
        unsafe { bindings::up_read(self.sem.get()) };
    }

    pub(crate) fn try_read(&self) -> Result<ReadGuard<'_, T>> {
        if !self.try_lock_read() {
            return Err(EBUSY);
        }

        Ok(ReadGuard::new(self))
    }

    pub(crate) fn read(&self) -> ReadGuard<'_, T> {
        self.lock_read();
        ReadGuard::new(self)
    }
}

unsafe impl<T: Send> Send for RwSemaphore<T> {}
unsafe impl<T: Send + Sync> Sync for RwSemaphore<T> {}

pub(crate) struct WriteGuard<'a, T>(&'a RwSemaphore<T>);

impl<'a, T> WriteGuard<'a, T> {
    fn new(parent: &RwSemaphore<T>) -> WriteGuard<'_, T> {
        WriteGuard(parent)
    }
}

impl<'a, T> Drop for WriteGuard<'a, T> {
    fn drop(&mut self) {
        self.0.unlock_write();
    }
}

impl<'a, T> Deref for WriteGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: The contract for UnsafeCell is satisfied by our use of a rw_semaphore
        // to guard access to it. There can be multiple readers (shared refs) but only at
        // most one writer (mutable ref).
        unsafe { &*self.0.data.get() }
    }
}

impl<'a, T> DerefMut for WriteGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: The contract for UnsafeCell is satisfied by our use of a rw_semaphore
        // to guard access to it. There can be multiple readers (shared refs) but only at
        // most one writer (mutable ref).
        unsafe { &mut *self.0.data.get() }
    }
}

pub(crate) struct ReadGuard<'a, T>(&'a RwSemaphore<T>);

impl<'a, T> ReadGuard<'a, T> {
    fn new(parent: &RwSemaphore<T>) -> ReadGuard<'_, T> {
        ReadGuard(parent)
    }
}

impl<'a, T> Drop for ReadGuard<'a, T> {
    fn drop(&mut self) {
        self.0.unlock_read();
    }
}

impl<'a, T> Deref for ReadGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: The contract for UnsafeCell is satisfied by our use of a rw_semaphore
        // to guard access to it. There can be multiple readers (shared refs) but only at
        // most one writer (mutable ref).
        unsafe { &*self.0.data.get() }
    }
}
