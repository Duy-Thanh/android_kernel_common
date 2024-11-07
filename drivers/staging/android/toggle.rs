// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Provides sysfs knobs for Rust ashmem.
//!
//! We don't have an abstraction for sysfs yet, so do it manually.

use core::{
    mem::{self, transmute},
    ptr::{self, addr_of},
};
use kernel::{bindings, c_str, error::to_result, page::PAGE_SIZE, prelude::*};

/// Abstraction for the ashmem driver's kobject.
pub(crate) struct AshmemObj {
    kobj: *mut bindings::kobject,
}

// SAFETY: Moving the kobject to another thread is safe.
unsafe impl Send for AshmemObj {}
// SAFETY: Accessing the kobject from several threads is safe.
unsafe impl Sync for AshmemObj {}

impl AshmemObj {
    pub(crate) fn new() -> Result<Self> {
        // SAFETY: Creating a new kobject under the kernel kobj is always okay.
        let kobj = unsafe {
            bindings::kobject_create_and_add(c_str!("ashmem").as_char_ptr(), bindings::kernel_kobj)
        };
        if kobj.is_null() {
            return Err(ENOMEM);
        }
        // INVARIANT: The kobject is valid, so we can transfer ownership to the `AshmemObj`.
        let me = AshmemObj { kobj };

        // If this fails, the destructor of AshmemObj cleans up kobj.
        // SAFETY: The `ATTRS` global represents a valid attribute group.
        to_result(unsafe { bindings::sysfs_create_group(kobj, addr_of!(ATTRS.group)) })?;

        Ok(me)
    }
}

impl Drop for AshmemObj {
    fn drop(&mut self) {
        // SAFETY: This `AshmemObj` holds a valid kobject.
        unsafe { bindings::kobject_put(self.kobj) };
    }
}

struct AshmemSysfsAttrs {
    group: bindings::attribute_group,
    attr_unpin: bindings::kobj_attribute,
    attr_array: [*mut bindings::attribute; 2],
}

// SAFETY: This lets us put this struct in the `ATTRS` global, which is only used in ways that are
// okay.
unsafe impl Sync for AshmemSysfsAttrs {}

static ATTRS: AshmemSysfsAttrs = AshmemSysfsAttrs {
    group: bindings::attribute_group {
        attrs: addr_of!(ATTRS.attr_array[0]).cast_mut(),
        ..unsafe { mem::zeroed() }
    },
    attr_unpin: attribute(c_str!("unpin"), 0o644, unpin_show, unpin_store),
    attr_array: [addr_of!(ATTRS.attr_unpin.attr).cast_mut(), ptr::null_mut()],
};

// export names make CFI failures easier to read.
#[export_name = "ashmem_unpin_store"]
unsafe extern "C" fn unpin_store(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *const u8,
    count: usize,
) -> isize {
    // SAFETY: The caller provides a valid buffer of size `count`.
    let buf = unsafe { core::slice::from_raw_parts(buf.cast::<u8>(), count) };

    match crate::ashmem_range::unpin_set(buf) {
        Ok(()) => count as isize,
        Err(err) => err.to_errno() as isize,
    }
}

#[export_name = "ashmem_unpin_show"]
unsafe extern "C" fn unpin_show(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *mut u8,
) -> isize {
    let value = crate::ashmem_range::unpin_get();

    // SAFETY: `buf` fits up to `PAGE_SIZE` bytes, so this write is not out of bounds.
    unsafe { bindings::sized_strscpy(buf.cast(), value.as_char_ptr(), PAGE_SIZE) };

    // SAFETY: strscpy always writes a nul-terminator.
    unsafe { bindings::strlen(buf.cast()) as isize }
}

// Workaround due to -funsigned-char triggering CFI failures.
type StoreFnC = unsafe extern "C" fn(
    *mut bindings::kobject,
    *mut bindings::kobj_attribute,
    *const core::ffi::c_char,
    usize,
) -> isize;
type StoreFnRust = unsafe extern "C" fn(
    *mut bindings::kobject,
    *mut bindings::kobj_attribute,
    *const u8,
    usize,
) -> isize;
type ShowFnC = unsafe extern "C" fn(
    *mut bindings::kobject,
    *mut bindings::kobj_attribute,
    *mut core::ffi::c_char,
) -> isize;
type ShowFnRust =
    unsafe extern "C" fn(*mut bindings::kobject, *mut bindings::kobj_attribute, *mut u8) -> isize;

const fn attribute(
    name: &'static CStr,
    mode: u32,
    show: ShowFnRust,
    store: StoreFnRust,
) -> bindings::kobj_attribute {
    bindings::kobj_attribute {
        attr: bindings::attribute {
            name: name.as_char_ptr(),
            mode: mode as _,
        },
        // SAFETY: These function signatures are identical except for i8 vs u8.
        show: Some(unsafe { transmute::<ShowFnRust, ShowFnC>(show) }),
        // SAFETY: These function signatures are identical except for i8 vs u8.
        store: Some(unsafe { transmute::<StoreFnRust, StoreFnC>(store) }),
    }
}
