// SPDX-License-Identifier: GPL-2.0

//! XArray abstraction.
//!
//! C header: [`include/linux/xarray.h`](../../include/linux/xarray.h)

use crate::{
    bindings,
    error::{Error, Result},
    prelude::*,
    types::{ForeignOwnable, Opaque, ScopeGuard},
};
use core::{
    marker::{PhantomData, PhantomPinned},
    ptr::{addr_of_mut, NonNull},
};

/// Flags passed to `XArray::new` to configure the `XArray`.
type Flags = bindings::gfp_t;

/// Flag values passed to `XArray::new` to configure the `XArray`.
pub mod flags {
    /// Use IRQ-safe locking.
    pub const LOCK_IRQ: super::Flags = bindings::BINDINGS_XA_FLAGS_LOCK_IRQ;
    /// Use softirq-safe locking.
    pub const LOCK_BH: super::Flags = bindings::BINDINGS_XA_FLAGS_LOCK_BH;
    /// Track which entries are free (distinct from None).
    pub const TRACK_FREE: super::Flags = bindings::BINDINGS_XA_FLAGS_TRACK_FREE;
    /// Initialize array index 0 as busy.
    pub const ZERO_BUSY: super::Flags = bindings::BINDINGS_XA_FLAGS_ZERO_BUSY;
    /// Use GFP_ACCOUNT for internal memory allocations.
    pub const ACCOUNT: super::Flags = bindings::BINDINGS_XA_FLAGS_ACCOUNT;
    /// Create an allocating `XArray` starting at index 0.
    pub const ALLOC: super::Flags = bindings::BINDINGS_XA_FLAGS_ALLOC;
    /// Create an allocating `XArray` starting at index 1.
    pub const ALLOC1: super::Flags = bindings::BINDINGS_XA_FLAGS_ALLOC1;
}

/// An array which efficiently maps sparse integer indices to owned objects.
///
/// This is similar to a `Vec<Option<T>>`, but more efficient when there are holes in the
/// index space, and can be efficiently grown.
///
/// This structure is expected to often be used with an inner type that can be efficiently
/// cloned, such as an `Arc<T>`.
///
/// # Examples
///
/// In this example, we create a new `XArray` and demonstrate
/// rudimentary read/write operations.
///
/// ```
/// use core::{
///     borrow::Borrow,
///     ops::{ Deref, DerefMut }
/// };
/// use kernel::{
///     pin_init,
///     sync::Arc,
///     xarray::{ XArray, flags::LOCK_IRQ }
/// };
///
/// let xarr = Box::pin_init(XArray::<Arc<usize>>::new(LOCK_IRQ)).unwrap();
/// let xarr = xarr.as_ref();
///
/// // `get` and `set` are used to read/write values.
/// assert!(xarr.get(0).is_none());
/// xarr.set(0, Arc::try_new(0)?);
/// assert_eq!(*xarr.get(0).unwrap(), 0);
///
/// // `replace` is like `set`, but returns the old value.
/// let old = xarr.replace(0, Arc::try_new(1)?)?.unwrap();
/// assert_eq!(*old, 0);
/// assert_eq!(*xarr.get(0).unwrap(), 1);
///
/// // `replace` returns `None` if there was no previous value.
/// assert!(xarr.replace(1, Arc::try_new(1)?)?.is_none());
/// assert_eq!(*xarr.get(1).unwrap(), 1);
///
/// // Similarly, `remove` returns the old value, or `None` if it didn't exist.
/// assert_eq!(*xarr.remove(0).unwrap(), 1);
/// assert!(xarr.get(0).is_none());
/// assert!(xarr.remove(0).is_none());
///
/// # Ok::<(), Error>(())
/// ```
///
/// In this example, we create a new `XArray` and demonstrate
/// reading and/or mutating values that are not `T::Borrowed<'a>: Into<T>`.
///
/// ```
/// use core::{
///     borrow::Borrow,
///     ops::{ Deref, DerefMut }
/// };
/// use kernel::xarray::{XArray, flags::LOCK_IRQ};
///
/// let xarr = Box::pin_init(XArray::<Box<usize>>::new(LOCK_IRQ)).unwrap();
/// let xarr = xarr.as_ref();
///
/// // If the type is not `T::Borrowed<'a>: Into<T>`, use `get_scoped` to access a shared reference
/// // from a closure.
/// assert!(xarr.get_scoped(0, |x| x.is_none()));
/// xarr.set(0, Box::try_new(0)?);
/// xarr.get_scoped(0, |x| assert_eq!(*x.unwrap(), 0));
///
/// // `get_scoped` also gives mutable access inside the closure.
/// xarr.get_scoped(0, |x| {
///     let mut_x = x.unwrap();
///     assert_eq!(mut_x, &0);
///     *mut_x = 1;
/// });
///
/// xarr.get_scoped(0, |x| assert_eq!(*x.unwrap(), 1));
///
/// # Ok::<(), Error>(())
/// ```
///
pub struct XArray<T: ForeignOwnable> {
    xa: Opaque<bindings::xarray>,
    _p: PhantomData<T>,
    _q: PhantomPinned,
}

impl<T: ForeignOwnable> XArray<T> {
    /// Creates a new `XArray` with the given flags.
    pub fn new(flags: Flags) -> impl PinInit<Self> {
        unsafe {
            kernel::init::pin_init_from_closure(move |slot: *mut XArray<T>| {
                bindings::xa_init_flags(Opaque::raw_get(addr_of_mut!((*slot).xa)), flags);
                Ok(())
            })
        }
    }

    /// Replaces an entry with a new value, returning the old value (if any).
    pub fn replace(&self, index: u64, value: T) -> Result<Option<T>> {
        let new = value.into_foreign();
        // SAFETY: `new` just came from into_foreign(), and we dismiss this guard if
        // the xa_store operation succeeds and takes ownership of the pointer.
        let guard = ScopeGuard::new(|| unsafe {
            drop(T::from_foreign(new));
        });

        // SAFETY: `self.xa` is always valid by the type invariant, and we are storing
        // a `T::into_foreign()` result which upholds the later invariants.
        let old = unsafe {
            bindings::xa_store(self.xa.get(), index, new as *mut _, bindings::GFP_KERNEL)
        };

        let ret = unsafe { bindings::xa_err(old) };
        if ret != 0 {
            Err(Error::from_errno(ret))
        } else if old.is_null() {
            guard.dismiss();
            Ok(None)
        } else {
            guard.dismiss();
            // SAFETY: The old value must have been stored by either this function or
            // `alloc_limits_opt`, both of which ensure non-NULL entries are valid
            // ForeignOwnable pointers.
            Ok(Some(unsafe { T::from_foreign(old) }))
        }
    }

    /// Replaces an entry with a new value, dropping the old value (if any).
    pub fn set(&self, index: u64, value: T) -> Result {
        self.replace(index, value)?;
        Ok(())
    }

    /// Looks up a reference to an entry in the array, cloning it
    /// and returning the cloned value to the user.
    ///
    pub fn get(&self, index: u64) -> Option<T>
    where
        for<'a> T::BorrowedMut<'a>: Into<T>,
    {
        self.get_scoped(index, |x| x.map(|v| v.into()))
    }

    /// Looks up and a reference to an entry in the array, calling the user
    /// provided function on the resulting `Option<&T>` to return a value
    /// computed from the reference. Use this function if you need
    /// (possibly mutable) access to a `&T` that is not `T::Borrowed<'a>: Into<T>`.
    ///
    pub fn get_scoped<F, R>(&self, index: u64, f: F) -> R
    where
        F: FnOnce(Option<T::BorrowedMut<'_>>) -> R,
    {
        // SAFETY: `self.xa` is always valid by the type invariant.
        unsafe { bindings::xa_lock(self.xa.get()) };

        // SAFETY: `self.xa` is always valid by the type invariant.
        let p = unsafe { bindings::xa_load(self.xa.get(), index) };
        let t = NonNull::new(p as *mut T).map(|p| unsafe { T::borrow_mut(p.as_ptr() as _) });
        let r = f(t);

        // SAFETY: `self.xa` is always valid by the type invariant.
        unsafe { bindings::xa_unlock(self.xa.get()) };

        r
    }

    /// Looks up the provided index in the XArray, returning
    /// a match, the next higher index, or None if no larger index
    /// exists.
    pub fn find(&self, index: u64) -> Option<u64> {
        // SAFETY: `self.xa` is always valid by the type invariant.
        unsafe { bindings::xa_lock(self.xa.get()) };

        let mut new_index = index;
        // SAFETY: `self.xa` is always valud by the type invariant.
        let p = unsafe {
            bindings::xa_find(
                self.xa.get(),
                &mut new_index,
                core::ffi::c_ulong::MAX,
                bindings::BINDINGS_XA_PRESENT,
            )
        };

        // SAFETY: `self.xa` is always valid by the type invariant.
        unsafe { bindings::xa_unlock(self.xa.get()) };

        if p.is_null() {
            return None;
        }

        Some(new_index)
    }

    /// Removes and returns an entry, returning it if it existed.
    pub fn remove(&self, index: u64) -> Option<T> {
        let p = unsafe { bindings::xa_erase(self.xa.get(), index) };
        if p.is_null() {
            None
        } else {
            Some(unsafe { T::from_foreign(p) })
        }
    }
}

impl<T: ForeignOwnable> Drop for XArray<T> {
    fn drop(&mut self) {
        // SAFETY: `self.xa` is valid by the type invariant, and as we have the only reference to
        // the `XArray` we can safely iterate its contents and drop everything.
        unsafe {
            let mut index: core::ffi::c_ulong = 0;
            let mut entry = bindings::xa_find(
                self.xa.get(),
                &mut index,
                core::ffi::c_ulong::MAX,
                bindings::BINDINGS_XA_PRESENT,
            );
            while !entry.is_null() {
                T::from_foreign(entry);
                entry = bindings::xa_find_after(
                    self.xa.get(),
                    &mut index,
                    core::ffi::c_ulong::MAX,
                    bindings::BINDINGS_XA_PRESENT,
                );
            }

            // Locked locks are not safe to drop. Normally we would want to try_lock()/unlock() here
            // for safety or something similar, but in this case xa_destroy() is guaranteed to
            // acquire the lock anyway. This will deadlock if a lock guard was improperly dropped,
            // but that is not UB, so it's sufficient for soundness purposes.
            bindings::xa_destroy(self.xa.get());
        }
    }
}

// SAFETY: XArray is thread-safe and all mutation operations are internally locked.
unsafe impl<T: Send + ForeignOwnable> Send for XArray<T> {}
unsafe impl<T: Sync + ForeignOwnable> Sync for XArray<T> {}
