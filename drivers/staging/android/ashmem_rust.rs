// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Anonymous Shared Memory Subsystem for Android.
#![allow(warnings)]

use core::pin::Pin;
use kernel::{
    c_str,
    error::Result,
    fs::File,
    list::{List, ListArc},
    miscdevice::{declare_static_miscdev, MiscDevice, MiscDeviceOptions},
    prelude::*,
};

mod ashmem_range;

mod ashmem_mutex;
use ashmem_mutex::AshmemGuard;

struct AshmemLru {
    lru_count: usize,
    lru_list: List<ashmem_range::Range, 0>,
}

impl AshmemGuard<'_> {
    fn shrink_range(&mut self, range: &ashmem_range::Range, pgstart: usize, pgend: usize) {
        let old_size = range.size(self);
        {
            let inner = range.inner.get_mut(self);
            inner.pgstart = pgstart;
            inner.pgend = pgend;
        }
        let new_size = range.size(self);

        // Only change the counter if the range is on the lru list.
        if !range.purged(self) {
            self.lru_count += new_size;
            self.lru_count -= old_size;
        }
    }

    fn insert_lru(&mut self, range: ListArc<ashmem_range::Range>) {
        // Don't insert the range if it's already purged.
        if !range.purged(self) {
            self.lru_count += range.size(self);
            self.lru_list.push_front(range);
        }
    }

    fn remove_lru(&mut self, range: &ashmem_range::Range) -> Option<ListArc<ashmem_range::Range>> {
        // SAFETY: The only list with ID 0 is this list, so the range can't be in some other list
        // with the same ID.
        let ret = unsafe { self.lru_list.remove(range) };

        // Only decrement lru_count if the range was actually in the list.
        if ret.is_some() {
            self.lru_count -= range.size(self);
        }

        ret
    }
}

// SAFETY: We call `init` as the very first thing in the initialization of this module, so there
// are no calls to `lock` before `init` is called.
pub(crate) static ASHMEM_MUTEX: ashmem_mutex::AshmemMutex =
    unsafe { ashmem_mutex::AshmemMutex::new() };

module! {
    type: AshmemModule,
    name: "ashmem_rust",
    author: "Alice Ryhl",
    description: "Anonymous Shared Memory Subsystem",
    license: "GPL",
}

struct AshmemModule {
    _reg: AshmemMiscdevRegistration,
}

impl kernel::Module for AshmemModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        // SAFETY: Called once since this is the module initializer. The value is not moved after
        // this call as it's stored in a global.
        unsafe { ASHMEM_MUTEX.init() };

        pr_warn!("Ashmem Rust initialized.");

        Ok(Self {
            // SAFETY: There is no previous call to `register` on `ASHMEM_MISCDEV` since this is
            // the module initializer.
            _reg: unsafe { ASHMEM_MISCDEV.register() }?,
        })
    }
}

declare_static_miscdev! {
    static ASHMEM_MISCDEV: AshmemMiscdev;
    registration: AshmemMiscdevRegistration;
    device: Ashmem;
    options: MiscDeviceOptions {
        name: c_str!("ashmem_rust")
    };
}

/// Represents an open ashmem file.
struct Ashmem {}

#[vtable]
impl MiscDevice for Ashmem {
    type Ptr = Pin<Box<Self>>;

    fn open(_: &File) -> Result<Pin<Box<Self>>> {
        Ok(Pin::from(Box::new(Ashmem {}, GFP_KERNEL)?))
    }
}
