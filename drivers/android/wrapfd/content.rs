// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use crate::{
    helpers,
    wrapped_device::{open_wrapped_device, WrappedDevice},
};

use kernel::{
    bindings, device,
    error::{from_err_ptr, to_result},
    fs::{file::FileDescriptorReservation, File, LocalFile},
    mm::virt::VmaNew,
    page::{page_align, PAGE_MASK},
    prelude::*,
    types::ARef,
};

struct DMABufCPUAccessScope(*mut bindings::dma_buf, kernel::dma::DataDirection);

impl DMABufCPUAccessScope {
    fn new(dmabuf: *mut bindings::dma_buf, direction: kernel::dma::DataDirection) -> Result<Self> {
        to_result(unsafe { bindings::dma_buf_begin_cpu_access(dmabuf, direction.into()) })?;

        Ok(Self(dmabuf, direction))
    }
}

impl Drop for DMABufCPUAccessScope {
    fn drop(&mut self) {
        _ = to_result(unsafe { bindings::dma_buf_end_cpu_access(self.0, self.1.into()) });
    }
}

struct DMABufVMap {
    dmabuf: *mut bindings::dma_buf,
    map: bindings::iosys_map,
}

impl DMABufVMap {
    fn new(dmabuf: *mut bindings::dma_buf) -> Result<Self> {
        let mut map = bindings::iosys_map::default();
        to_result(unsafe { bindings::dma_buf_vmap(dmabuf, &mut map as *mut _) })?;

        Ok(Self { dmabuf, map })
    }
}

impl core::ops::Deref for DMABufVMap {
    type Target = bindings::iosys_map;

    fn deref(&self) -> &Self::Target {
        &self.map
    }
}

impl Drop for DMABufVMap {
    fn drop(&mut self) {
        unsafe { bindings::dma_buf_vunmap(self.dmabuf, &mut self.map as *mut _) }
    }
}

pub(crate) struct WrapDMAContent {
    dev: ARef<device::Device>,
    pub(crate) dmabuf: *mut bindings::dma_buf,
    pub(crate) writable: bool,
    pub(crate) close_on_exec: bool,
}

impl WrapDMAContent {
    pub(crate) fn new(dev: ARef<device::Device>, fd: c_int, prot: u32) -> Result<Self> {
        let dmabuf = from_err_ptr(unsafe { bindings::dma_buf_get(fd) })?;
        let writable = (prot & bindings::PROT_WRITE) != 0;

        let is_file_writable =
            (unsafe { (*(*dmabuf).file).f_mode } & bindings::MY_FMODE_WRITE) != 0;

        if writable && !is_file_writable {
            return Err(EACCES);
        }

        let close_on_exec = unsafe { helpers::my_get_close_on_exec(fd) };

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
        size: usize,
    ) -> Result<(u32, ARef<File>)> {
        let size = bindings::loff_t::try_from(size)?;

        let reservation = FileDescriptorReservation::get_unused_fd_flags(0)?;

        let file = open_wrapped_device::<WrappedDevice>(
            b"[wrapfd]\0".as_ptr() as *const c_char,
            context,
            if writable {
                bindings::O_RDWR
            } else {
                bindings::O_RDONLY
            },
        )?;

        let inode = unsafe { helpers::my_file_inode(file.as_ptr() as *mut c_void) };
        unsafe { helpers::my_i_size_write(inode, size) };

        let fd = reservation.reserved_fd();

        let cloned_file = file.clone();
        reservation.fd_install(file);

        Ok((fd, cloned_file))
    }

    pub(crate) fn ioctl(&self, cmd: u32, arg: usize) -> Result<isize> {
        pr_info!("WrapDMAContent::ioctl\n");

        let file = unsafe { (*self.dmabuf).file };
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

        if end > unsafe { (*self.dmabuf).size }.try_into()? {
            return Err(EINVAL);
        }

        let scope =
            DMABufCPUAccessScope::new(self.dmabuf, kernel::dma::DataDirection::Bidirectional)?;

        let map = DMABufVMap::new(self.dmabuf)?;

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
                &mut kiocb as *mut _ as *mut c_void,
                file.as_ptr() as *mut c_void,
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

unsafe impl Sync for WrapDMAContent {}
unsafe impl Send for WrapDMAContent {}

impl Drop for WrapDMAContent {
    fn drop(&mut self) {
        unsafe { bindings::dma_buf_put(self.dmabuf) };
    }
}
