// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::ptr::NonNull;
use kernel::{
    bindings,
    error::{from_err_ptr, to_result},
    ffi::{c_int, c_void},
    prelude::*,
};

extern "C" {
    pub(crate) fn my_get_close_on_exec(fd: c_int) -> bool;
    pub(crate) fn my_file_inode(file: *const c_void) -> *mut c_void;
    pub(crate) fn my_i_size_write(inode: *mut c_void, i_size: bindings::loff_t);
    pub(crate) fn my_i_size_read(inode: *mut c_void) -> bindings::loff_t;
    pub(crate) fn my_i_mmap_lock_read(mapping: *mut c_void);
    pub(crate) fn my_i_mmap_unlock_read(mapping: *mut c_void);
    pub(crate) fn my_rb_empty_root(root: *const bindings::rb_root) -> bool;
    pub(crate) fn my_init_sync_kiocb(kiocb: *mut c_void, file: *mut c_void);
    pub(crate) fn my_file_count(file: *mut c_void) -> c_ulong;
}

pub(crate) struct DMABuf(NonNull<bindings::dma_buf>);
pub(crate) struct DMABufVMap<'a>(&'a DMABuf, bindings::iosys_map);
pub(crate) struct DMABufCPUAccessScope<'a>(&'a DMABuf, bindings::dma_data_direction);

impl DMABuf {
    pub(crate) fn from_fd(fd: u32) -> Result<Self> {
        let dmabuf = from_err_ptr(unsafe { bindings::dma_buf_get(fd.try_into()?) })?;
        Ok(Self(NonNull::new(dmabuf).ok_or(EINVAL)?))
    }

    pub(crate) fn size(&self) -> usize {
        unsafe { (*self.as_ptr()).size }
    }

    pub(crate) fn file(&self) -> *mut bindings::file {
        unsafe { (*self.as_ptr()).file }
    }

    pub(crate) fn is_file_writable(&self) -> bool {
        let f_mode = unsafe { (*self.file()).f_mode };
        (f_mode & bindings::MY_FMODE_WRITE) != 0
    }

    pub(crate) fn vmap<'a>(&'a self) -> Result<DMABufVMap<'a>> {
        let mut map = bindings::iosys_map::default();
        to_result(unsafe { bindings::dma_buf_vmap(self.as_ptr(), &mut map as *mut _) })?;

        Ok(DMABufVMap(self, map))
    }

    pub(crate) fn begin_cpu_access<'b>(
        &'b self,
        direction: bindings::dma_data_direction,
    ) -> Result<DMABufCPUAccessScope<'b>> {
        to_result(unsafe { bindings::dma_buf_begin_cpu_access(self.as_ptr(), direction.into()) })?;
        Ok(DMABufCPUAccessScope(self, direction))
    }

    pub(crate) fn as_ptr(&self) -> *mut bindings::dma_buf {
        self.0.as_ptr()
    }
}

impl Drop for DMABuf {
    fn drop(&mut self) {
        unsafe { bindings::dma_buf_put(self.as_ptr()) };
    }
}

unsafe impl Sync for DMABuf {}
unsafe impl Send for DMABuf {}

impl<'a> Drop for DMABufVMap<'a> {
    fn drop(&mut self) {
        unsafe { bindings::dma_buf_vunmap(self.0.as_ptr(), &mut self.1 as *mut _) }
    }
}

impl<'a> core::ops::Deref for DMABufVMap<'a> {
    type Target = bindings::iosys_map;

    fn deref(&self) -> &Self::Target {
        &self.1
    }
}

impl<'a> Drop for DMABufCPUAccessScope<'a> {
    fn drop(&mut self) {
        _ = to_result(unsafe { bindings::dma_buf_end_cpu_access(self.0.as_ptr(), self.1.into()) });
    }
}
