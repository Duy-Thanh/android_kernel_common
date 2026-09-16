use core::{marker::PhantomPinned, ptr::NonNull};

use kernel::{
    bindings,
    dma_buf::{CpuAccess, DmaBufVmap},
    ffi,
    fs::LocalFile,
    new_spinlock_irq,
    page::{is_page_aligned, offset_in_page, page_align, page_align_down, PAGE_MASK, PAGE_SIZE},
    prelude::*,
    sync::{Arc, SpinLockIrq},
    types::Opaque,
};

type FreePage = VVec<u8>;

fn get_free_page() -> Result<FreePage> {
    Ok(VVec::from_elem(0, PAGE_SIZE, GFP_KERNEL)?)
}

// 1 buffer for the start, and at most 2 at the end.
const MAX_NR_BOUNCE_BUFS: usize = 3;

// 1 per bounce page (3), and 1 for content read directly into the buffer.
const MAX_NR_KVECS: usize = MAX_NR_BOUNCE_BUFS + 1;

// These were module parameters in the plain-C driver, but rust-support for them has not yet been
// backported and apparently module parameters are being deprecated/discouraged.
const MAX_NR_LOAD_REQS: usize = 32;
const MIN_BYTES_PER_REQ: usize = 1024 * 1024;

struct DataSegment {
    offs: usize,
    len: usize,
}

impl DataSegment {
    fn new(offs: usize, len: usize) -> Self {
        Self { offs, len }
    }
}

struct IoResults {
    io_ret: ffi::c_long,
    bytes_read: usize,
    nr_reqs: usize,
}

#[pin_data]
struct SharedContext {
    #[pin]
    io_done: Opaque<bindings::completion>,

    #[pin]
    inner: SpinLockIrq<IoResults>,

    #[pin]
    _pin: PhantomPinned,
}

impl SharedContext {
    fn new(nr_reqs: usize) -> impl PinInit<Self> {
        pin_init!(Self {
            io_done <- Opaque::ffi_init(|slot| {
                // SAFETY: XXX
                unsafe { bindings::init_completion(slot) }
            }),
            inner <- new_spinlock_irq!(IoResults {
                io_ret: 0,
                bytes_read: 0,
                nr_reqs
            }),
            _pin: PhantomPinned,
        })
    }
}

#[pin_data]
struct IoRequest {
    shared_context: Arc<SharedContext>,

    #[pin]
    kiocb: Opaque<bindings::kiocb>,

    iter: bindings::iov_iter,
    iov: [bindings::kvec; MAX_NR_KVECS],
    file_seg: DataSegment,

    #[pin]
    _pin: PhantomPinned,
}

impl IoRequest {
    fn new<'a>(
        shared_context: Arc<SharedContext>,
        file: &'a LocalFile,
        req_file_offs: usize,
        req_len: usize,
        nr_segs: usize,
        iov: [bindings::kvec; MAX_NR_KVECS],
    ) -> impl PinInit<Self> + use<'a> {
        pin_init!(Self {
            shared_context,
            kiocb <- Opaque::ffi_init(|slot| {
                // SAFETY: `slot` is a valid, properly aligned pointer to uninitialized memory
                                // allocated for a `struct kiocb` provided by `Opaque::ffi_init`,
                                // and `file.as_ptr()` is a valid pointer to a live `struct file`.
                unsafe { bindings::init_sync_kiocb(slot, file.as_ptr()) };

                // SAFETY: `slot` was provided as a valid pointer to uninitialized `kiocb` memory by
                // `Opaque::ffi_init`, and was just initialized above via `init_sync_kiocb`.
                // File offset must be page aligned for direct I/O requests.
                unsafe {
                    (*slot).ki_pos = req_file_offs as i64;
                }

                // SAFETY: slot is (by now) a valid pointer to an initialized `kiocb` structure.
                unsafe {
                    (*slot).ki_flags |= bindings::IOCB_DIRECT as i32;
                    (*slot).ki_complete = Some(Self::async_io_complete);
                }
            }),
            iter: bindings::iov_iter::default(),
            iov,
            file_seg: DataSegment::new(req_file_offs, req_len),
            _pin: PhantomPinned,
        })
        .pin_chain(move |mut request: Pin<&mut Self>| {
            // SAFETY: We guarantee not to move (or drop) request through the obtained &mut Self
            let iter = &mut unsafe { request.as_mut().get_unchecked_mut() }.iter;

            // SAFETY: `request.iter` is a valid `iov_iter` reference, `request.iov` contains
            // `nr_segs` initialized `kvec` structures whose total length equals
            // `request.file_seg.len`.
            unsafe {
                bindings::iov_iter_kvec(
                    iter,
                    bindings::ITER_DEST,
                    &request.iov.as_slice()[0],
                    nr_segs,
                    request.file_seg.len,
                )
            };

            Ok(())
        })
    }

    /// # Safety
    ///
    /// - If `iocb` is non-null, it must point to a valid `struct kiocb` embedded as the `kiocb`
    ///   field of an `IoRequest` instance.
    /// - The enclosing `IoRequest` instance must remain valid and pinned in memory for the duration
    ///   of this callback.
    unsafe extern "C" fn async_io_complete(iocb: *mut bindings::kiocb, ret: ffi::c_long) {
        if iocb.is_null() {
            return;
        }

        // Opaque<T> is #[repr(transparent)], so *mut T and *mut Opaque<T> have the same
        // memory representation.
        let opaque_ptr = iocb.cast::<Opaque<bindings::kiocb>>();
        let io_params_ptr = kernel::container_of!(opaque_ptr, Self, kiocb);

        // SAFETY: Per function safety contract, `iocb` is embedded within a live `IoParams`
        // instance, so `io_params_ptr` points to a valid, initialized `IoParams` struct.
        let shared_context = unsafe { &(*io_params_ptr).shared_context };
        let mut guard = shared_context.inner.lock();

        if ret < 0 && guard.io_ret == 0 {
            guard.io_ret = ret;
        } else if ret > 0 {
            guard.bytes_read += ret as usize;
        }

        guard.nr_reqs -= 1;

        if guard.nr_reqs == 0 {
            // SAFETY: `SharedContext::io_done` is a properly initialized
            // `Opaque<bindings::completion>`.
            unsafe { bindings::complete(shared_context.io_done.get()) };
        }
    }
}

pub(crate) struct IoContext {
    bounce_bufs: KVec<FreePage>,
    dst_buf: NonNull<ffi::c_void>,
    start_bounce_len: usize,
    end_bounce_len: usize,
    buf_offs: usize,
    file_seg: DataSegment,
    dio_buf_seg: DataSegment,
    // The number of requests could be large, so use KVVec just in case.
    requests: KVVec<Pin<KBox<IoRequest>>>,
    shared_context: Option<Arc<SharedContext>>,
}

fn dio_aligned_len(file_offs: usize, len: usize) -> usize {
    page_align(file_offs + len) - page_align_down(file_offs)
}

impl IoContext {
    pub(crate) fn new(
        file_offs: usize,
        vaddr: NonNull<ffi::c_void>,
        buf_offs: usize,
        len: usize,
    ) -> Result<Self> {
        /*
         * If either the file or buffer offset are unaligned, then using direct I/O into the first
         * page of the buffer will overwrite some of the data that is already there. Allocate a
         * bounce page for that scenario, and later only copy the amount of data that belongs in the
         * first page.
         */
        let (dio_buf_offs, start_bounce_len) = if !is_page_aligned(file_offs | buf_offs) {
            /*
             * The contents of interest in this bounce page will be copied to the buffer
             * starting at buf_offs after the direct I/O request completes. The amount of data
             * copied will be everything in the bounce page after file_offset bytes.
             *
             * Therefore, the next address where data needs to be read into is buf_offs +
             * (PAGE_SIZE - offset_in_page(file_offs)). However, this address may not be
             * page aligned, and therefore not suitable for direct I/O, so page align it.
             *
             * This means that the data will need to be shifted backwards if it is read into
             * the buffer directly.
             */
            (
                page_align(buf_offs + PAGE_SIZE - offset_in_page(file_offs)),
                PAGE_SIZE,
            )
        } else {
            (buf_offs, 0)
        };

        /*
         * Read as much as possible directly into the buffer without causing any overwrites beyond
         * the range we're reading into, and since direct I/O is done in units of pages,
         * ensure that there is at least a page to read.
         */
        let buf_end = buf_offs + len;

        let dio_buf_len = if (dio_buf_offs < buf_end) && (buf_end - dio_buf_offs) >= PAGE_SIZE {
            page_align_down(buf_end) - dio_buf_offs
        } else {
            0
        };

        let file_read_len = dio_aligned_len(file_offs, len);

        /*
         * Bounce the remainder, which is capped at 2 pages, since we may have shifted the data
         * earlier, because of the buffer offset by at most one page, and then any other data
         * at the tail which may cross into another page.
         */
        let end_bounce_len = file_read_len - start_bounce_len - dio_buf_len;
        if end_bounce_len > PAGE_SIZE * 2 {
            pr_warn!("end_bounce_len > PAGE_SIZE * 2\n");
        }

        let mut bounce_bufs = KVec::new();
        let nr_bounce_pages = (start_bounce_len + end_bounce_len) / PAGE_SIZE;
        for _ in 0..nr_bounce_pages {
            bounce_bufs.push(get_free_page()?, GFP_KERNEL)?;
        }

        Ok(Self {
            bounce_bufs,
            dst_buf: vaddr,
            start_bounce_len,
            end_bounce_len,
            buf_offs,
            file_seg: DataSegment::new(file_offs, len),
            dio_buf_seg: DataSegment::new(dio_buf_offs, dio_buf_len),
            requests: KVVec::new(),
            shared_context: None,
        })
    }

    fn init_io_request(
        &mut self,
        shared_context: Arc<SharedContext>,
        file: &LocalFile,
        req_file_offs: usize,
        req_len: usize,
    ) -> Result<Pin<KBox<IoRequest>>> {
        let global_file_offset = page_align_down(self.file_seg.offs);
        let global_dio_file_offset = global_file_offset + self.start_bounce_len;

        let mut nr_segs = 0;

        let mut iov = [bindings::kvec {
            iov_base: core::ptr::null_mut(),
            iov_len: 0,
        }; MAX_NR_KVECS];

        let mut remaining_req_len = req_len;
        let mut cur_file_offs = req_file_offs;

        /*
         * If there's a start bounce buffer, it's always for the first page in the overall read
         * request.
         */
        if self.start_bounce_len != 0 && cur_file_offs == global_file_offset {
            iov[nr_segs].iov_base = self.bounce_bufs[0].as_mut_ptr().cast();
            iov[nr_segs].iov_len = self.start_bounce_len;
            remaining_req_len -= self.start_bounce_len;
            cur_file_offs += self.start_bounce_len;
            nr_segs += 1;
        }

        /*
         * Handle the case where the sub-request pertains to data copied directly from the file to
         * the destination buffer.
         */
        if remaining_req_len != 0
            && self.dio_buf_seg.len != 0
            && global_dio_file_offset <= cur_file_offs
        {
            /*
             * The offset into the buffer for this request should be the base of where we start
             * reading directly into it, plus how much we've already read into the region in
             * previous requests.
             */
            iov[nr_segs].iov_base = unsafe {
                self.dst_buf
                    .as_ptr()
                    .byte_add(self.dio_buf_seg.offs + (cur_file_offs - global_dio_file_offset))
            };

            /*
             * The amount of data to read is the smaller of the two terms:
             *
             * 1. How much data is left for this request.
             * 2. How much data there is left in the file region that gets loaded directly
             * into the buffer, which is taken as the difference between the current position
             * in the file and the end of that region.
             */
            let dio_len = remaining_req_len
                .min(global_dio_file_offset + self.dio_buf_seg.len - cur_file_offs);

            iov[nr_segs].iov_len = dio_len;
            remaining_req_len -= dio_len;
            cur_file_offs += dio_len;
            nr_segs += 1;
        }

        if remaining_req_len != 0 && self.end_bounce_len != 0 {
            /*
             * The offset into the file that is copied into the end bounce buffer(s) for the
             * overall request.
             */

            let global_end_bounce_file_offs = global_file_offset
                + dio_aligned_len(self.file_seg.offs, self.file_seg.len)
                - self.end_bounce_len;

            /*
             * The last two pages in the overall read request can be bounce pages, so we
             * calculate which pages to use here. If there's a bounce page at the beginning,
             * then start at index 1.
             *
             * Since we know the file offset that corresponds to the start of the file data
             * that will be copied into the end pages and the length, we use that and our
             * current position to track which one of the end pages to use.
             */
            let start_bounce_idx = (if self.start_bounce_len != 0 { 1 } else { 0 })
                + (cur_file_offs - global_end_bounce_file_offs) / PAGE_SIZE;

            if remaining_req_len > PAGE_SIZE * 2 {
                pr_warn!("remaining_req_len > PAGE_SIZE * 2\n");
            }

            while remaining_req_len != 0 {
                iov[nr_segs].iov_base = self.bounce_bufs[start_bounce_idx].as_mut_ptr().cast();

                iov[nr_segs].iov_len = PAGE_SIZE;
                remaining_req_len -= PAGE_SIZE;
                nr_segs += 1;
                // XXX shouldn't `start_bounce_idx` be incremented here???
            }
        }

        if remaining_req_len != 0 {
            pr_warn!("remaining_req_len != 0\n");
        }

        let request = KBox::pin_init(
            IoRequest::new(shared_context, file, req_file_offs, req_len, nr_segs, iov),
            GFP_KERNEL,
        )?;

        Ok(request)
    }

    pub(crate) fn prepare(&mut self, file: &LocalFile) -> Result<()> {
        /*
         * Try to split the I/O request evenly into MAX_NR_LOAD_REQS pieces, as long as each piece
         * is at least MIN_BYTES_PER_REQ in size, but no larger than MAX_RW_COUNT, since the VFS
         * layer cannot handle anything larger than that in one invocation.
         *
         * PAGE_ALIGN the length to ensure direct I/O requirements are upheld.
         */
        let mut remaining_len = dio_aligned_len(self.file_seg.offs, self.file_seg.len);

        const MAX_RW_COUNT: usize = (isize::MAX as usize) & PAGE_MASK;

        let len_per_req = page_align(
            (remaining_len / MAX_NR_LOAD_REQS)
                .max(MIN_BYTES_PER_REQ)
                .min(MAX_RW_COUNT),
        );

        let nr_reqs = remaining_len.div_ceil(len_per_req);

        let mut cur_file_offs = page_align_down(self.file_seg.offs);

        let shared_context = Arc::pin_init(SharedContext::new(nr_reqs), GFP_KERNEL)?;

        let mut requests = KVVec::new();
        requests.reserve(nr_reqs, GFP_KERNEL)?;

        for _ in 0..nr_reqs {
            let req_len = len_per_req.min(remaining_len);
            requests.push(
                self.init_io_request(Arc::clone(&shared_context), file, cur_file_offs, req_len)?,
                GFP_KERNEL,
            )?;

            cur_file_offs += req_len;
            remaining_len -= req_len;
        }

        self.requests = requests;
        self.shared_context = Some(shared_context);

        Ok(())
    }

    pub(crate) fn read(&mut self, file: &LocalFile) -> Result<()> {
        // `shared_context` should be Some(...) at this point if prepare has previously been
        // called.
        let shared_context = self.shared_context.as_ref().ok_or(EINVAL)?;

        for request in &mut self.requests {
            // SAFETY: `file.as_ptr()` is a valid `struct file` pointer, `request.kiocb` is a
            // a live, initialized `struct kiocb` in `request`, and `request.iter` is a valid
            // `struct iov_iter`.
            // We also promise not to move or drop the request through the mutable reference
            // obtained through get_unchecked_mut().
            let ret = unsafe {
                bindings::vfs_iocb_iter_read(
                    file.as_ptr(),
                    request.kiocb.get(),
                    &mut request.as_mut().get_unchecked_mut().iter,
                )
            };
            /*
             * ret == -EIOCBQUEUED => I/O request was queued successfully.
             * ret >= 0 => I/O request was satisfied synchronously.
             * ret < 0 => error.
             *
             * If an error is encountered, record the first one. We have to call
             * async_io_complete() if the request is not being processed asynchronously to
             * ensure that the nr_reqs context field is decremented properly so that we don't
             * block indefinitely in the wait_for_completion() call later.
             */
            if ret != -(bindings::EIOCBQUEUED as isize) {
                // SAFETY: We uphold `async_io_complete`'s contract: kiocb.get() is a non-null
                // pointer to a properly initialized `bindings::kiocb` structure.
                // The pinned request remains valid for the duration of the call.
                unsafe {
                    IoRequest::async_io_complete(request.kiocb.get(), ret);
                }
            }
        }

        // SAFETY: `shared_context.io_done` contains a valid pointer to a properly initialized
        // `struct completion` field.
        unsafe { bindings::wait_for_completion(shared_context.io_done.get()) };

        let guard = shared_context.inner.lock();

        let ret = guard.io_ret;
        if ret != 0 {
            return Err(Error::from_errno(ret as i32));
        }

        if guard.bytes_read < offset_in_page(self.file_seg.offs) + self.file_seg.len {
            return Err(EINVAL);
        }

        Ok(())
    }

    pub(crate) fn complete<'access, const READ: bool>(
        &mut self,
        map: &DmaBufVmap,
        mut access: CpuAccess<'access, READ, true>,
    ) -> Result<()> {
        // `shared_context` should be Some(...) at this point if prepare has previously been
        // called.
        let shared_context = self.shared_context.as_ref().ok_or(EINVAL)?;

        let file_offs = self.file_seg.offs;

        if shared_context.inner.lock().bytes_read < (offset_in_page(file_offs) + self.file_seg.len)
        {
            return Ok(());
        }

        let mut tot_len = self.file_seg.len;
        let mut dst_offset = self.buf_offs;

        let mut cur_bounce_page = 0;

        if self.start_bounce_len != 0 {
            let src_base = &self.bounce_bufs[cur_bounce_page];
            let src = &src_base[offset_in_page(file_offs)..];
            /* Handle the case where all of the requested data is in the first bounce page. */
            let len = tot_len.min(PAGE_SIZE - offset_in_page(file_offs));
            let src = &src[..len];
            access.memcpy_to(dst_offset, src);

            dst_offset += len;
            tot_len -= len;
            cur_bounce_page += 1;
        }

        if self.dio_buf_seg.len != 0 {
            let dio_start = self.dio_buf_seg.offs;
            let len = tot_len.min(self.dio_buf_seg.len);

            /*
             * If there's anything that was copied directly into the dmabuf, check to make sure
             * it's in the right place. Shift it back if it's not.
             */
            if dio_start != dst_offset {
                // This should never fail as we verified that the map is valid and gives us
                // a valid vaddr earlier in "load_prepare".
                let vaddr = map.vaddr().ok_or(EINVAL)?.as_ptr();

                // SAFETY: Both `dst_offset + len` and `direct_read_buf_offs + len` are verified
                // to be within the mapped dma-buf bounds (`vaddr`). `memmove` safely handles
                // potential overlapping memory regions.
                unsafe {
                    bindings::memmove(vaddr.byte_add(dst_offset), vaddr.byte_add(dio_start), len)
                };
            }

            dst_offset += len;
            tot_len -= len;
        }

        for _ in 0..(self.end_bounce_len / PAGE_SIZE) {
            let len = tot_len.min(PAGE_SIZE);
            let src = &self.bounce_bufs[cur_bounce_page][..len];

            access.memcpy_to(dst_offset, src);

            dst_offset += len;
            tot_len -= len;
            cur_bounce_page += 1;
        }

        if tot_len != 0 {
            pr_warn!("tot_len != 0\n");
        }

        Ok(())
    }
}
