// SPDX-License-Identifier: GPL-2.0

//! Binder -- the Android IPC mechanism.

use kernel::{
    bindings::{self, seq_file},
    file::{File, Operations, PollTable},
    io_buffer::IoBufferWriter,
    linked_list::{GetLinks, GetLinksWrapped, Links},
    prelude::*,
    sync::Arc,
    types::ForeignOwnable,
    user_ptr::UserSlicePtrWriter,
};

use crate::{context::Context, process::Process, thread::Thread};

use core::sync::atomic::{AtomicBool, Ordering};

mod allocation;
mod context;
mod defs;
mod error;
mod node;
mod process;
mod range_alloc;
mod thread;
mod transaction;

module! {
    type: BinderModule,
    name: "rust_binder",
    author: "Wedson Almeida Filho, Alice Ryhl",
    description: "Android Binder",
    license: "GPL",
}

/// Specifies how a type should be delivered to the read part of a BINDER_WRITE_READ ioctl.
///
/// When a value is pushed to the todo list for a process or thread, it is stored as a trait object
/// with the type `Arc<dyn DeliverToRead>`. Trait objects are a Rust feature that lets you
/// implement dynamic dispatch over many different types. This lets us store many different types
/// in the todo list.
trait DeliverToRead {
    /// Performs work. Returns true if remaining work items in the queue should be processed
    /// immediately, or false if it should return to caller before processing additional work
    /// items.
    fn do_work(self: Arc<Self>, thread: &Thread, writer: &mut UserSlicePtrWriter) -> Result<bool>;

    /// Cancels the given work item. This is called instead of [`DeliverToRead::do_work`] when work
    /// won't be delivered.
    fn cancel(self: Arc<Self>) {}

    /// Returns the linked list links for the work item.
    fn get_links(&self) -> &Links<dyn DeliverToRead>;

    /// Should we use `wake_up_interruptible_sync` or `wake_up_interruptible` when scheduling this
    /// work item?
    ///
    /// Generally only set to true for non-oneway transactions.
    fn should_sync_wakeup(&self) -> bool;

    /// Get the debug name of this type.
    fn debug_name(&self) -> &'static str {
        core::any::type_name::<Self>()
    }
}

struct DeliverToReadListAdapter {}

impl GetLinks for DeliverToReadListAdapter {
    type EntryType = dyn DeliverToRead;
    fn get_links(data: &Self::EntryType) -> &Links<Self::EntryType> {
        data.get_links()
    }
}

impl GetLinksWrapped for DeliverToReadListAdapter {
    type Wrapped = Arc<dyn DeliverToRead>;
}

struct DeliverCode {
    code: u32,
    skip: AtomicBool,
    links: Links<dyn DeliverToRead>,
}

impl DeliverCode {
    fn new(code: u32) -> Self {
        Self {
            code,
            skip: AtomicBool::new(false),
            links: Links::new(),
        }
    }

    /// Disable this DeliverCode and make it do nothing.
    ///
    /// This is used instead of removing it from the work list, since `LinkedList::remove` is
    /// unsafe, whereas this method is not.
    fn skip(&self) {
        self.skip.store(true, Ordering::Relaxed);
    }
}

impl DeliverToRead for DeliverCode {
    fn do_work(self: Arc<Self>, _thread: &Thread, writer: &mut UserSlicePtrWriter) -> Result<bool> {
        if !self.skip.load(Ordering::Relaxed) {
            writer.write(&self.code)?;
        }
        Ok(true)
    }
    fn get_links(&self) -> &Links<dyn DeliverToRead> {
        &self.links
    }
    fn should_sync_wakeup(&self) -> bool {
        false
    }
}

const fn ptr_align(value: usize) -> usize {
    let size = core::mem::size_of::<usize>() - 1;
    (value + size) & !size
}

struct BinderModule {}

impl kernel::Module for BinderModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        crate::context::CONTEXTS.init();

        #[cfg(CONFIG_ANDROID_BINDERFS_RUST)]
        unsafe {
            kernel::error::to_result(bindings::init_rust_binderfs())?;
        }

        Ok(Self {})
    }
}

/// Makes the inner type Sync.
#[repr(transparent)]
pub struct AssertSync<T>(T);
unsafe impl<T> Sync for AssertSync<T> {}

/// File operations that rust_binderfs.c can use.
#[no_mangle]
#[used]
pub static rust_binder_fops: AssertSync<kernel::bindings::file_operations> = {
    let ops = kernel::bindings::file_operations {
        owner: THIS_MODULE.as_ptr(),
        poll: Some(rust_binder_poll),
        unlocked_ioctl: Some(rust_binder_unlocked_ioctl),
        compat_ioctl: Some(rust_binder_compat_ioctl),
        mmap: Some(rust_binder_mmap),
        open: Some(rust_binder_open),
        release: Some(rust_binder_release),
        llseek: None,
        read: None,
        write: None,
        read_iter: None,
        write_iter: None,
        iopoll: None,
        iterate: None,
        iterate_shared: None,
        mmap_supported_flags: 0,
        flush: Some(rust_binder_flush),
        fsync: None,
        fasync: None,
        lock: None,
        sendpage: None,
        get_unmapped_area: None,
        check_flags: None,
        flock: None,
        splice_write: None,
        splice_read: None,
        setlease: None,
        fallocate: None,
        show_fdinfo: None,
        copy_file_range: None,
        remap_file_range: None,
        fadvise: None,
        uring_cmd: None,
        uring_cmd_iopoll: None,
    };
    AssertSync(ops)
};

#[no_mangle]
unsafe extern "C" fn rust_binder_new_device(
    name: *const core::ffi::c_char,
) -> *mut core::ffi::c_void {
    // SAFETY: The caller will always provide a valid c string here.
    let name = unsafe { kernel::str::CStr::from_char_ptr(name) };
    match Context::new(name) {
        Ok(ctx) => Arc::into_foreign(ctx).cast_mut(),
        Err(_err) => return core::ptr::null_mut(),
    }
}

#[no_mangle]
unsafe extern "C" fn rust_binder_remove_device(device: *mut core::ffi::c_void) {
    if !device.is_null() {
        // SAFETY: The caller ensures that the `device` pointer came from a previous call to
        // `rust_binder_new_device`.
        let ctx = unsafe { Arc::<Context>::from_foreign(device) };
        ctx.deregister();
        drop(ctx);
    }
}

unsafe extern "C" fn rust_binder_open(
    inode: *mut bindings::inode,
    file_ptr: *mut bindings::file,
) -> core::ffi::c_int {
    // SAFETY: The `rust_binderfs.c` file ensures that `i_private` is set to the return value of a
    // successful call to `rust_binder_new_device`.
    let ctx = unsafe { Arc::<Context>::borrow((*inode).i_private) };

    // SAFETY: The caller provides a valid file pointer to a new `struct file`.
    let file = unsafe { File::from_ptr(file_ptr) };
    let process = match Process::open(&ctx.into(), file) {
        Ok(process) => process,
        Err(err) => return err.to_errno(),
    };
    // SAFETY: This file is associated with Rust binder, so we own the `private_data` field.
    unsafe {
        (*file_ptr).private_data = process.into_foreign().cast_mut();
    }
    0
}

unsafe extern "C" fn rust_binder_release(
    _inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> core::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let process = unsafe { Arc::<Process>::from_foreign((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    let file = unsafe { File::from_ptr(file) };
    Process::release(process, file);
    0
}

unsafe extern "C" fn rust_binder_compat_ioctl(
    file: *mut bindings::file,
    cmd: core::ffi::c_uint,
    arg: core::ffi::c_ulong,
) -> core::ffi::c_long {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    let mut cmd = kernel::file::IoctlCommand::new(cmd as _, arg as _);
    // SAFETY: The caller ensures that the file is valid.
    match Process::compat_ioctl(f, unsafe { File::from_ptr(file) }, &mut cmd) {
        Ok(ret) => ret.into(),
        Err(err) => err.to_errno().into(),
    }
}

unsafe extern "C" fn rust_binder_unlocked_ioctl(
    file: *mut bindings::file,
    cmd: core::ffi::c_uint,
    arg: core::ffi::c_ulong,
) -> core::ffi::c_long {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    let mut cmd = kernel::file::IoctlCommand::new(cmd as _, arg as _);
    // SAFETY: The caller ensures that the file is valid.
    match Process::ioctl(f, unsafe { File::from_ptr(file) }, &mut cmd) {
        Ok(ret) => ret.into(),
        Err(err) => err.to_errno().into(),
    }
}

unsafe extern "C" fn rust_binder_mmap(
    file: *mut bindings::file,
    vma: *mut bindings::vm_area_struct,
) -> core::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the vma is valid.
    let mut area = unsafe { kernel::mm::virt::Area::from_ptr(vma) };
    // SAFETY: The caller ensures that the file is valid.
    match Process::mmap(f, unsafe { File::from_ptr(file) }, &mut area) {
        Ok(()) => 0,
        Err(err) => err.to_errno().into(),
    }
}

unsafe extern "C" fn rust_binder_poll(
    file: *mut bindings::file,
    wait: *mut bindings::poll_table_struct,
) -> bindings::__poll_t {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    // SAFETY: The caller ensures that the file is valid.
    let fileref = unsafe { File::from_ptr(file) };
    // SAFETY: The caller ensures that the `PollTable` is valid.
    match Process::poll(f, fileref, unsafe { &PollTable::from_ptr(wait) }) {
        Ok(v) => v,
        Err(_) => bindings::POLLERR,
    }
}

unsafe extern "C" fn rust_binder_flush(
    file: *mut bindings::file,
    _id: bindings::fl_owner_t,
) -> core::ffi::c_int {
    // SAFETY: We previously set `private_data` in `rust_binder_open`.
    let f = unsafe { Arc::<Process>::borrow((*file).private_data) };
    match Process::flush(f) {
        Ok(()) => 0,
        Err(err) => err.to_errno().into(),
    }
}

#[no_mangle]
unsafe extern "C" fn rust_binder_stats_show(_: *mut seq_file) -> core::ffi::c_int {
    0
}

#[no_mangle]
unsafe extern "C" fn rust_binder_state_show(_: *mut seq_file) -> core::ffi::c_int {
    0
}

#[no_mangle]
unsafe extern "C" fn rust_binder_transactions_show(_: *mut seq_file) -> core::ffi::c_int {
    0
}

#[no_mangle]
unsafe extern "C" fn rust_binder_transaction_log_show(_: *mut seq_file) -> core::ffi::c_int {
    0
}
