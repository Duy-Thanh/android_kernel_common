// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::ptr::NonNull;

use crate::{
    helpers,
    helpers::{DMABuf, DMABufVMap},
    wrapped_device::{WrappedDevice, open_wrapped_device},
};

use kernel::{
    bindings, device,
    error::{from_err_ptr, to_result},
    ffi,
    fs::{File, LocalFile, file::FileDescriptorReservation},
    mm::virt::VmaNew,
    page::{PAGE_MASK, page_align},
    prelude::*,
    types::ARef,
};

pub(crate) struct WrapDMAContent {
    dev: ARef<device::Device>,
    pub(crate) dmabuf: DMABuf,
    pub(crate) writable: bool,
    pub(crate) close_on_exec: bool,
}

impl WrapDMAContent {
    pub(crate) fn new(dev: ARef<device::Device>, fd: u32, prot: u32) -> Result<Self> {
        let dmabuf = DMABuf::from_fd(fd)?;
        let writable = (prot & bindings::PROT_WRITE) != 0;

        if writable && !dmabuf.is_file_writable() {
            return Err(EACCES);
        }

        let close_on_exec = unsafe { helpers::my_get_close_on_exec(fd.try_into()?) };

        Ok(Self {
            dev,
            dmabuf,
            writable,
            close_on_exec,
        })
    }

    pub(crate) fn create_wrap(
        context: Pin<KBox<WrappedDevice>>,
        writable: bool,
        close_on_exec: bool,
        size: usize,
    ) -> Result<(u32, ARef<File>)> {
        let size = bindings::loff_t::try_from(size)?;

        let mut flags = 0;
        if close_on_exec { flags |= bindings::O_CLOEXEC };
        if writable { flags |= bindings::O_RDWR } else { flags |= bindings::O_RDONLY };

        let reservation = FileDescriptorReservation::get_unused_fd_flags(flags)?;

        let file = open_wrapped_device::<WrappedDevice>(
            b"[wrapfd]\0".as_ptr() as *const ffi::c_char,
            context,
            flags
        )?;

        let inode = unsafe { helpers::my_file_inode(file.as_ptr() as *mut ffi::c_void) };
        unsafe { helpers::my_i_size_write(inode, size) };

        let fd = reservation.reserved_fd();

        let cloned_file = file.clone();
        reservation.fd_install(file);

        Ok((fd, cloned_file))
    }

    pub(crate) fn ioctl(&self, cmd: u32, arg: usize) -> Result<isize> {
        pr_info!("WrapDMAContent::ioctl\n");

        let file = self.dmabuf.file();
        let f_op = unsafe { (*file).f_op };
        let Some(unlocked_ioctl) = (unsafe { (*f_op).unlocked_ioctl }) else {
            return Err(ENOIOCTLCMD);
        };

        let res = unsafe { unlocked_ioctl(file, cmd, arg) };

        if res < 0 {
            Err(Error::from_errno(res as i32)) // XXX fishy
        } else {
            Ok(res)
        }
    }

    // XXX range arithmetic and random casts below need work!
    pub(crate) fn load(
        &self,
        file: ARef<LocalFile>,
        file_offset: bindings::loff_t,
        buf_offset: bindings::loff_t,
        mut len: bindings::loff_t,
    ) -> Result<isize> {
        pr_info!("WrapDMAContent::load\n");

        let Some(end) = buf_offset.checked_add(page_align(len.try_into()?).try_into()?) else {
            return Err(EINVAL);
        };

        if end > self.dmabuf.size().try_into()? {
            return Err(EINVAL);
        }

        let scope = self
            .dmabuf
            .begin_cpu_access(bindings::dma_data_direction_DMA_BIDIRECTIONAL)?;
        let map = self.dmabuf.vmap()?;

        if map.is_iomem {
            return Err(EINVAL);
        }

        let mut iov = bindings::kvec {
            iov_base: unsafe {
                map.__bindgen_anon_1
                    .vaddr
                    .byte_offset(buf_offset.try_into()?)
            },
            iov_len: 0,
        };

        let mut kiocb = bindings::kiocb::default();
        unsafe {
            helpers::my_init_sync_kiocb(
                &mut kiocb as *mut _ as *mut ffi::c_void,
                file.as_ptr() as *mut ffi::c_void,
            )
        };
        kiocb.ki_pos = file_offset;
        kiocb.ki_flags |= bindings::IOCB_DIRECT as i32;

        const MAX_RAW_COUNT: bindings::loff_t =
            bindings::loff_t::MAX & (PAGE_MASK as bindings::loff_t);

        let mut iter = bindings::iov_iter::default();
        while len > 0 {
            let count = MAX_RAW_COUNT.min(page_align(len.try_into()?).try_into()?);
            let count: usize = count.try_into()?;

            iov.iov_len = count;

            unsafe {
                bindings::iov_iter_kvec(&mut iter, bindings::ITER_DEST, &iov, 1, iov.iov_len)
            };

            let mut bytes_read = 0;
            while len > 0 && bytes_read < count {
                let sz =
                    unsafe { bindings::vfs_iocb_iter_read(file.as_ptr(), &mut kiocb, &mut iter) };

                if sz < 0 {
                    return Err(Error::from_errno(sz.try_into()?));
                } else if sz == 0 {
                    // Short read, early EOF.
                    return Err(EINVAL);
                }

                let sz = sz as usize;

                bytes_read += sz;
                len -= sz as i64;
            }

            iov.iov_base = iov.iov_base.wrapping_byte_add(count);
        }

        Ok(0)
    }
}
