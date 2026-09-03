// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::ptr::NonNull;

use crate::wrapped_device::{open_wrapped_device, WrappedDevice};

use kernel::{
    bindings, device,
    dma_buf::{DmaBuf, DmaBufVmap},
    ffi,
    fs::{file::FileDescriptorReservation, file::Offset, LocalFile},
    page::{is_page_aligned, offset_in_page, page_align, page_align_down, PAGE_MASK, PAGE_SIZE},
    prelude::*,
    types::ARef,
};

pub(crate) struct WrapDMAContent {
    pub(crate) dmabuf: ARef<DmaBuf>,
    writable: bool,
    pub(crate) close_on_exec: bool,
}

type FreePage = VVec<u8>;

fn get_free_page() -> Result<FreePage> {
    Ok(VVec::from_elem(0, PAGE_SIZE, GFP_KERNEL)?)
}

// 1 buffer for the start, and at most 2 at the end.
const MAX_NR_BOUNCE_BUFS: usize = 3;

// 1 per bounce page (3), and 1 for content read directly into the buffer.
const MAX_NR_KVECS: usize = MAX_NR_BOUNCE_BUFS + 1;

struct LoadParams {
    bounce_bufs: KVVec<FreePage>, // Contains exactly MAX_NR_BOUNCE_BUFS entries.

    // Would love to use an array here and initialize it using core::array::try_from_fn,
    // but that's still experimental.
    // bounce_bufs: [FreePage; MAX_NR_BOUNCE_BUFS],
    start_bounce_len: usize,
    end_bounce_len: usize,
    file_offs: usize,
    buf_offs: usize,
    len: usize,
    direct_read_buf_offs: usize,
    direct_read_len: usize,
    bytes_read: usize,
    file_read_len: usize,
    iov: [bindings::kvec; MAX_NR_KVECS],
    kiocb: bindings::kiocb,
    iter: bindings::iov_iter,
}

impl WrapDMAContent {
    fn is_dmabuf_writable(dmabuf: &DmaBuf) -> bool {
        let file = dmabuf.file();

        // SAFETY: `dmabuf.file()` returns a valid `struct file` reference whose underlying
        // C pointer is guaranteed to be non-null and valid for reads while `dmabuf` is held.
        let f_mode = unsafe { (*file.as_ptr()).f_mode };
        (f_mode & bindings::FMODE_WRITE) != 0
    }

    pub(crate) fn make_writable(&mut self, writable: bool) -> Result<()> {
        if writable && !Self::is_dmabuf_writable(&self.dmabuf) {
            return Err(EACCES);
        }

        self.writable = writable;

        Ok(())
    }

    pub(crate) fn is_writable(&self) -> bool {
        self.writable && Self::is_dmabuf_writable(&self.dmabuf)
    }

    pub(crate) fn new(_dev: ARef<device::Device>, fd: u32, prot: u32) -> Result<Self> {
        let dmabuf = DmaBuf::get(fd.try_into()?)?;
        let writable = (prot & bindings::PROT_WRITE) != 0;

        if writable && !Self::is_dmabuf_writable(&dmabuf) {
            return Err(EACCES);
        }

        // SAFETY: The Linux kernel's `get_close_on_exec` safely handles arbitrary `fd` values
        // (including invalid ones) by looking up the descriptor table under RCU lock without
        // causing undefined behavior.
        let close_on_exec = unsafe { bindings::get_close_on_exec(fd) };

        Ok(Self {
            dmabuf,
            writable,
            close_on_exec,
        })
    }

    pub(crate) fn create_wrap(
        context: Pin<KBox<WrappedDevice>>,
        content: WrapDMAContent,
    ) -> Result<u32, (Error, WrapDMAContent)> {
        let size = content.dmabuf.size();
        let size = match Offset::try_from(size) {
            Err(_err) => {
                return Err((EINVAL, content));
            }
            Ok(size) => size,
        };

        let writable = content.is_writable();
        let close_on_exec = content.close_on_exec;

        let mut flags = 0;
        if close_on_exec {
            flags |= bindings::O_CLOEXEC
        };
        if writable {
            flags |= bindings::O_RDWR
        } else {
            flags |= bindings::O_RDONLY
        };

        let reservation = match FileDescriptorReservation::get_unused_fd_flags(flags) {
            Err(err) => {
                return Err((err, content));
            }
            Ok(reservation) => reservation,
        };

        context.lock.write().content = Some(content);
        let file = open_wrapped_device::<WrappedDevice>(c"[wrapfd]", context, flags).map_err(
            |(err, context)| {
                let content = context
                    .lock
                    .write()
                    .content
                    .take()
                    .expect("We put a Some() in, we expect it still to be there");
                (err, content)
            },
        )?;

        // SAFETY: `file` is a valid `kernel::fs::File` reference, ensuring `file.as_ptr()` is
        // non-null and points to an active `struct file`.
        let inode = unsafe { bindings::file_inode(file.as_ptr()) };

        // SAFETY: `file_inode` on a valid `struct file` returns a non-null, valid pointer to its
        // `struct inode`.
        unsafe { bindings::i_size_write(inode, size) };

        let fd = reservation.reserved_fd();
        reservation.fd_install(file);

        Ok(fd)
    }

    pub(crate) fn ioctl(&self, cmd: u32, arg: usize) -> Result<isize> {
        // pr_info!("WrapDMAContent::ioctl\n");

        let file = self.dmabuf.file();
        let file_ptr = file.as_ptr();

        // SAFETY: "file" is valid as "dmabuf" holds a reference to it.
        let f_op = unsafe { (*file_ptr).f_op };

        // SAFETY: `f_op` is a valid pointer to the file's `struct file_operations`.
        let Some(unlocked_ioctl) = (unsafe { (*f_op).unlocked_ioctl }) else {
            return Err(ENOIOCTLCMD);
        };

        // SAFETY: `file_ptr` is valid, `unlocked_ioctl` is a verified non-null function pointer,
        // and `cmd`/`arg` are primitive values passed directly per C ABI.
        let res = unsafe { unlocked_ioctl(file_ptr, cmd, arg) };

        if res < 0 {
            Err(Error::from_errno(res as i32)) // XXX fishy
        } else {
            Ok(res)
        }
    }

    pub(crate) fn llseek(
        &self,
        offs: Offset,
        whence: ffi::c_int,
    ) -> Result<Offset> {
        let file = self.dmabuf.file();
        let file_ptr = file.as_ptr();

        // SAFETY: "file" is valid as "dmabuf" holds a reference to it.
        let f_op = unsafe { (*file_ptr).f_op };

        // SAFETY: `f_op` is a valid pointer to the file's `struct file_operations`.
        let Some(llseek) = (unsafe { (*f_op).llseek }) else {
            return Err(ESPIPE);
        };

        // SAFETY: `file_ptr` is valid, `llseek` is a verified non-null function pointer,
        // and `cmd`/`arg` are primitive values passed directly per C ABI.
        let res = unsafe { llseek(file_ptr, offs, whence) };

        if res < 0 {
            Err(Error::from_errno(res as i32)) // XXX fishy
        } else {
            Ok(res)
        }
    }

    fn prepare_iocb_request(
        &self,
        file: &LocalFile,
        vaddr: NonNull<ffi::c_void>,
        params: &mut LoadParams,
    ) -> Result<()> {
        let mut nr_segs = 0;
        let mut cur_bounce_page = 0;

        if params.start_bounce_len > 0 {
            params.iov[nr_segs].iov_base = params.bounce_bufs[cur_bounce_page].as_mut_ptr().cast();
            params.iov[nr_segs].iov_len = params.start_bounce_len;
            nr_segs += 1;
            cur_bounce_page += 1;
        }

        if params.direct_read_len > 0 {
            // SAFETY: `direct_read_buf_offs` was verified during `load_prepare` to be within the
            // bounds of `self.dmabuf.size()`, which is covered by the mapped virtual address range
            // `vaddr`.
            params.iov[nr_segs].iov_base =
                unsafe { vaddr.as_ptr().byte_add(params.direct_read_buf_offs) };

            params.iov[nr_segs].iov_len = params.direct_read_len;
            nr_segs += 1;
        }

        for _ in 0..(params.end_bounce_len / PAGE_SIZE) {
            params.iov[nr_segs].iov_base = params.bounce_bufs[cur_bounce_page].as_mut_ptr().cast();
            params.iov[nr_segs].iov_len = PAGE_SIZE;
            nr_segs += 1;
            cur_bounce_page += 1;
        }

        // SAFETY: It's always safe to call init_sync_kiocb on a valid kiocb structure and
        // a valid file pointer.
        unsafe { bindings::init_sync_kiocb(core::ptr::from_mut(&mut params.kiocb), file.as_ptr()) };

        // File offset must be page aligned for direct I/O requests.
        params.kiocb.ki_pos = page_align_down(params.file_offs).try_into()?;
        params.kiocb.ki_flags |= bindings::IOCB_DIRECT as i32;

        // SAFETY: `params.iter` is a valid `iov_iter` reference, `params.iov` contains `nr_segs`
        // initialized `kvec` structures whose total length equals `params.file_read_len`.
        unsafe {
            bindings::iov_iter_kvec(
                &mut params.iter,
                bindings::ITER_DEST,
                &params.iov.as_slice()[0],
                nr_segs,
                params.file_read_len,
            )
        };

        Ok(())
    }

    fn load_prepare(
        &self,
        file: &LocalFile,
        file_offs: usize,
        buf_offs: usize,
        len: usize,
    ) -> Result<(DmaBufVmap, LoadParams)> {
        /* We will only write into buf_offs + len, so no need to page-align the length here. */
        let Some(buf_end) = buf_offs.checked_add(len) else {
            return Err(EINVAL);
        };

        if buf_end > self.dmabuf.size() {
            return Err(EINVAL);
        }

        let file_read_len = page_align(file_offs + len) - page_align_down(file_offs);

        let (direct_read_buf_offs, start_bounce_len) = if !is_page_aligned(file_offs | buf_offs) {
            (
                page_align(buf_offs + PAGE_SIZE - offset_in_page(file_offs)),
                PAGE_SIZE,
            )
        } else {
            (buf_offs, 0)
        };

        let direct_read_len =
            if direct_read_buf_offs < buf_end && buf_end - direct_read_buf_offs >= PAGE_SIZE {
                page_align_down(buf_end) - direct_read_buf_offs
            } else {
                0
            };

        let end_bounce_len = file_read_len - start_bounce_len - direct_read_len;
        if end_bounce_len > PAGE_SIZE * 2 {
            pr_warn!("load_prepare: end_bounce_len > PAGE_SIZE * 2\n");
        }

        let mut bounce_bufs = KVVec::new();
        let nr_bounce_pages = (start_bounce_len + end_bounce_len) / PAGE_SIZE;
        for _ in 0..nr_bounce_pages {
            bounce_bufs.push(get_free_page()?, GFP_KERNEL)?;
        }

        let map = self.dmabuf.vmap()?;

        let Some(vaddr) = map.vaddr() else {
            return Err(EINVAL);
        };

        let mut params = LoadParams {
            bounce_bufs,
            start_bounce_len,
            end_bounce_len,
            file_offs,
            buf_offs,
            len,
            direct_read_buf_offs,
            direct_read_len,
            bytes_read: 0,
            file_read_len,
            iov: [bindings::kvec {
                iov_base: core::ptr::null_mut(),
                iov_len: 0,
            }; 4],
            kiocb: bindings::kiocb::default(),
            iter: bindings::iov_iter::default(),
        };

        self.prepare_iocb_request(file, vaddr, &mut params)?;

        Ok((map, params))
    }

    fn load_complete(&self, map: &DmaBufVmap, params: &LoadParams) -> Result<()> {
        if params.bytes_read < offset_in_page(params.file_offs) + params.len {
            return Ok(());
        }

        map.begin_cpu_access_bidirectional(|mut access| {
            let mut cur_bounce_page = 0;
            let mut tot_len = params.len;
            let mut dst_offset = params.buf_offs;

            if params.start_bounce_len > 0 {
                let src_base = &params.bounce_bufs[cur_bounce_page];
                let src = &src_base[offset_in_page(params.file_offs)..];

                let len = tot_len.min(PAGE_SIZE - offset_in_page(params.file_offs));

                let src = &src[..len];
                access.memcpy_to(dst_offset, src);

                dst_offset += len;
                tot_len -= len;
                cur_bounce_page += 1;
            }

            if params.direct_read_len > 0 {
                let len = tot_len.min(params.direct_read_len);

                if params.direct_read_buf_offs != dst_offset {
                    // This should never fail as we verified that the map is valid and gives us
                    // a valid vaddr earlier in "load_prepare".
                    let vaddr = map
                        .vaddr()
                        .expect("vaddr should be valid at this point.")
                        .as_ptr();

                    // SAFETY: Both `dst_offset + len` and `direct_read_buf_offs + len` are verified
                    // to be within the mapped dma-buf bounds (`vaddr`). `memmove` safely handles
                    // potential overlapping memory regions.
                    unsafe {
                        bindings::memmove(
                            vaddr.byte_add(dst_offset),
                            vaddr.byte_add(params.direct_read_buf_offs),
                            len,
                        )
                    };
                }

                dst_offset += len;
                tot_len -= len;
            }

            for _ in 0..(params.end_bounce_len / PAGE_SIZE) {
                let len = tot_len.min(PAGE_SIZE);
                let src = &params.bounce_bufs[cur_bounce_page][..len];

                access.memcpy_to(dst_offset, src);

                dst_offset += len;
                tot_len -= len;
                cur_bounce_page += 1;
            }

            if tot_len > 0 {
                pr_warn!("load_complete: tot_len still positive.\n");
            }
        })?;

        Ok(())
    }

    pub(crate) fn load(
        &self,
        file: ARef<LocalFile>,
        file_offs: Offset,
        buf_offs: Offset,
        len: Offset,
    ) -> Result<isize> {
        // pr_info!("WrapDMAContent::load\n");

        let file_offs: usize = file_offs.try_into()?;
        let buf_offs: usize = buf_offs.try_into()?;
        let len: usize = len.try_into()?;

        let (map, mut params) = self.load_prepare(&file, file_offs, buf_offs, len)?;

        const MAX_RW_COUNT: usize = (ffi::c_int::MAX as usize) & PAGE_MASK;

        let mut ret = 0;
        while params.bytes_read < params.file_read_len {
            let count = MAX_RW_COUNT.min(page_align(params.file_read_len - params.bytes_read));
            // SAFETY: `params.iter` was properly initialized by `iov_iter_kvec` and remains valid
            // in `params`.
            unsafe { bindings::iov_iter_truncate(&mut params.iter, count.try_into()?) };

            // SAFETY: `file.as_ptr()` is a valid `struct file` pointer, `params.kiocb` is a
            // a live, initialized `struct kiocb` in `params`, and `params.iter` is a valid
            // `struct iov_iter`.
            let bytes_read = unsafe {
                bindings::vfs_iocb_iter_read(file.as_ptr(), &mut params.kiocb, &mut params.iter)
            };

            if bytes_read <= 0 {
                ret = bytes_read;
                break;
            }

            params.bytes_read += bytes_read as usize;
        }

        self.load_complete(&map, &params)?;

        if ret < 0 {
            return Err(Error::from_errno(ret as i32));
        }

        if params.bytes_read < offset_in_page(file_offs) + len {
            return Err(EINVAL);
        }

        Ok(0)
    }
}
