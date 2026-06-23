// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use kernel::{bindings, ffi};

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
    pub(crate) fn my_iov_iter_truncate(i: *mut bindings::iov_iter, count: u64);
}
