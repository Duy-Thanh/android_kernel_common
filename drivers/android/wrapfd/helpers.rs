// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::ptr::NonNull;
use kernel::{
    bindings,
    error::{from_err_ptr, to_result},
    ffi,
    page::{PAGE_MASK, PAGE_SIZE},
    prelude::*,
};

extern "C" {
    pub(crate) fn my_get_close_on_exec(fd: ffi::c_int) -> bool;
    pub(crate) fn my_file_inode(file: *const ffi::c_void) -> *mut ffi::c_void;
    pub(crate) fn my_i_size_write(inode: *mut ffi::c_void, i_size: bindings::loff_t);
    pub(crate) fn my_i_size_read(inode: *mut ffi::c_void) -> bindings::loff_t;
    pub(crate) fn my_i_mmap_lock_read(mapping: *mut ffi::c_void);
    pub(crate) fn my_i_mmap_unlock_read(mapping: *mut ffi::c_void);
    pub(crate) fn my_rb_empty_root(root: *const bindings::rb_root) -> bool;
    pub(crate) fn my_init_sync_kiocb(kiocb: *mut ffi::c_void, file: *mut ffi::c_void);
    pub(crate) fn my_file_count(file: *mut ffi::c_void) -> ffi::c_ulong;
    pub(crate) fn my_iov_iter_count(i: *const bindings::iov_iter) -> usize;
    pub(crate) fn my_iov_iter_truncate(i: *mut bindings::iov_iter, count: u64);
    fn my_get_free_page(gfp_mask: ffi::c_uint) -> ffi::c_ulong;
    fn my_free_page(addr: ffi::c_ulong);
}

pub(crate) fn is_page_aligned(addr: usize) -> bool {
    (addr & !PAGE_MASK) == 0
}

pub(crate) fn page_align_down(addr: usize) -> usize {
    addr & PAGE_MASK
}

pub(crate) fn offset_in_page(addr: usize) -> usize {
    addr & !PAGE_MASK
}

pub(crate) struct FreePage(usize);

pub(crate) fn get_free_page(flags: kernel::alloc::Flags) -> Result<FreePage> {
    let page_ptr = unsafe { my_get_free_page(flags.as_raw()) };

    if page_ptr == 0 {
        return Err(ENOMEM);
    }

    Ok(FreePage(page_ptr))
}

impl FreePage {
    pub(crate) fn as_ptr(&self) -> *mut ffi::c_void {
        self.0 as _
    }
}

impl Drop for FreePage {
    fn drop(&mut self) {
        unsafe { my_free_page(self.0) };
    }
}
