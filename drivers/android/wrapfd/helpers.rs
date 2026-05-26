// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::ffi::{c_int, c_void};
use kernel::bindings;

extern "C" {
    pub(crate) fn my_get_close_on_exec(fd: c_int) -> bool;
    pub(crate) fn my_set_close_on_exec(fd: c_int, flag: bool);
    pub(crate) fn my_file_inode(file: *const c_void) -> *mut c_void;
    pub(crate) fn my_i_size_write(inode: *mut c_void, i_size: bindings::loff_t);
    pub(crate) fn my_i_size_read(inode: *mut c_void) -> bindings::loff_t;
    pub(crate) fn my_i_mmap_lock_read(mapping: *mut c_void);
    pub(crate) fn my_i_mmap_unlock_read(mapping: *mut c_void);
    pub(crate) fn my_rb_empty_root(root: *const bindings::rb_root) -> bool;
    pub(crate) fn my_init_sync_kiocb(kiocb: *mut c_void, file: *mut c_void);
}
