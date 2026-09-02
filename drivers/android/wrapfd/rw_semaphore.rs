// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};

use kernel::{bindings, prelude::*, static_lock_class, types::Opaque};

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

            // SAFETY: `slot` is a valid, uninitialized pointer to a `struct rw_semaphore`
            // provided by `Opaque::ffi_init`. `name` and `key` point to valid static memory.
            sem <- Opaque::ffi_init(|slot| unsafe {
                bindings::__init_rwsem(
                    slot,
                    c"RwSemaphore".as_ptr().cast(),
                    static_lock_class!().as_ptr(),
                );
            }),
            data: UnsafeCell::new(data),
        })
    }

    fn lock_write(&self) {
        // SAFETY: `self.sem.get()` returns a valid, initialized `struct rw_semaphore` pointer.
        unsafe { bindings::down_write(self.sem.get()) };
    }

    fn try_lock_write(&self) -> bool {
        // SAFETY: `self.sem.get()` returns a valid, initialized `struct rw_semaphore` pointer.
        (unsafe { bindings::down_write_trylock(self.sem.get()) }) != 0
    }

    fn unlock_write(&self) {
        // SAFETY: `self.sem.get()` is valid and initialized, and the write lock is currently
        // held by this thread.
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
        // SAFETY: `self.sem.get()` returns a valid, initialized `struct rw_semaphore` pointer.
        unsafe { bindings::down_read(self.sem.get()) };
    }

    fn try_lock_read(&self) -> bool {
        // SAFETY: `self.sem.get()` returns a valid, initialized `struct rw_semaphore` pointer.
        (unsafe { bindings::down_read_trylock(self.sem.get()) }) != 0
    }

    fn unlock_read(&self) {
        // SAFETY: `self.sem.get()` is valid and initialized, and a read lock is currently
        // held by this thread.
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

// SAFETY: `RwSemaphore<T>` can be sent across threads if `T` is `Send` because access to `T`
// is synchronized by the underlying C `rw_semaphore`.
unsafe impl<T: Send> Send for RwSemaphore<T> {}

// SAFETY: `RwSemaphore<T>` can be shared across threads if `T` is `Send + Sync`, as multiple
// threads can acquire concurrent read locks (`&T`) or exclusive write locks (`&mut T`).
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
        // SAFETY: The write lock of `RwSemaphore` is held by this `WriteGuard`, guaranteeing
        // exclusive access to `data` without concurrent readers or writers.
        unsafe { &*self.0.data.get() }
    }
}

impl<'a, T> DerefMut for WriteGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: The write lock of `RwSemaphore` is held by this `WriteGuard`, guaranteeing
        // exclusive mutable access to `data` without aliasing.
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
        // SAFETY: A read lock of `RwSemaphore` is held by this `ReadGuard`, guaranteeing no
        // thread has exclusive mutable access to `data`.
        unsafe { &*self.0.data.get() }
    }
}
