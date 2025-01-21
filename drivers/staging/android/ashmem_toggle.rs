// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Provides sysfs knobs for Rust ashmem.
//!
//! We don't have an abstraction for sysfs yet, so do it manually.

use crate::{ashmem_range, IGNORE_UNSET_PROT_EXEC, IGNORE_UNSET_PROT_READ, UNPIN_IMMEDIATELY};
use core::{
    mem::{self, transmute},
    ptr::{self, addr_of},
    sync::atomic::Ordering,
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
    attr_prot_read: bindings::kobj_attribute,
    attr_prot_exec: bindings::kobj_attribute,
    attr_array: [*mut bindings::attribute; 4],
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
    attr_prot_read: attribute(
        c_str!("ignore_unset_prot_read"),
        0o644,
        prot_read_show,
        prot_read_store,
    ),
    attr_prot_exec: attribute(
        c_str!("ignore_unset_prot_exec"),
        0o644,
        prot_exec_show,
        prot_exec_store,
    ),
    attr_array: [
        addr_of!(ATTRS.attr_unpin.attr).cast_mut(),
        addr_of!(ATTRS.attr_prot_read.attr).cast_mut(),
        addr_of!(ATTRS.attr_prot_exec.attr).cast_mut(),
        ptr::null_mut(),
    ],
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

    let (enabled, unpin_immediately) = match buf.trim_ascii() {
        b"shrinker" => (true, false),
        b"ignore" => (false, false),
        b"immediately" => (false, true),
        _ => return EINVAL.to_errno() as isize,
    };

    match ashmem_range::set_shrinker_enabled(enabled, unpin_immediately) {
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
    let unpin_immediately = UNPIN_IMMEDIATELY.load(Ordering::Relaxed);
    let value = match ashmem_range::get_shrinker_enabled() {
        false if unpin_immediately => c_str!("immediately\n"),
        false => c_str!("ignore\n"),
        true => c_str!("shrinker\n"),
    };

    // SAFETY: `buf` fits up to `PAGE_SIZE` bytes, so this write is not out of bounds.
    unsafe { bindings::sized_strscpy(buf.cast(), value.as_char_ptr(), PAGE_SIZE) };

    // SAFETY: strscpy always writes a nul-terminator.
    unsafe { bindings::strlen(buf.cast()) as isize }
}

#[export_name = "ashmem_prot_read_store"]
unsafe extern "C" fn prot_read_store(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *const u8,
    count: usize,
) -> isize {
    // SAFETY: The caller provides a valid buffer of size `count`.
    let buf = unsafe { core::slice::from_raw_parts(buf.cast::<u8>(), count) };

    let ignore_unset_prot_read = match buf.trim_ascii() {
        b"0" => false,
        b"1" => true,
        _ => return EINVAL.to_errno() as isize,
    };

    IGNORE_UNSET_PROT_READ.store(ignore_unset_prot_read, Ordering::Relaxed);

    count as isize
}

#[export_name = "ashmem_prot_read_show"]
unsafe extern "C" fn prot_read_show(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *mut u8,
) -> isize {
    let value = match IGNORE_UNSET_PROT_READ.load(Ordering::Relaxed) {
        true => c_str!("1\n"),
        false => c_str!("0\n"),
    };

    // SAFETY: `buf` fits up to `PAGE_SIZE` bytes, so this write is not out of bounds.
    unsafe { bindings::sized_strscpy(buf.cast(), value.as_char_ptr(), PAGE_SIZE) };

    // SAFETY: strscpy always writes a nul-terminator.
    unsafe { bindings::strlen(buf.cast()) as isize }
}

#[export_name = "ashmem_prot_exec_store"]
unsafe extern "C" fn prot_exec_store(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *const u8,
    count: usize,
) -> isize {
    // SAFETY: The caller provides a valid buffer of size `count`.
    let buf = unsafe { core::slice::from_raw_parts(buf.cast::<u8>(), count) };

    let ignore_unset_prot_exec = match buf.trim_ascii() {
        b"0" => false,
        b"1" => true,
        _ => return EINVAL.to_errno() as isize,
    };

    IGNORE_UNSET_PROT_EXEC.store(ignore_unset_prot_exec, Ordering::Relaxed);

    count as isize
}

#[export_name = "ashmem_prot_exec_show"]
unsafe extern "C" fn prot_exec_show(
    _kobj: *mut bindings::kobject,
    _attr: *mut bindings::kobj_attribute,
    buf: *mut u8,
) -> isize {
    let value = match IGNORE_UNSET_PROT_EXEC.load(Ordering::Relaxed) {
        true => c_str!("1\n"),
        false => c_str!("0\n"),
    };

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
