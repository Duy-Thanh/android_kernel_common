// SPDX-License-Identifier: GPL-2.0

//! Binder -- the Android IPC mechanism.

use kernel::{
    bindings::{self, seq_file},
    file::{File, Operations, PollTable},
    prelude::*,
    sync::Arc,
    types::ForeignOwnable,
};

use crate::{context::Context, process::Process};

use core::mem::ManuallyDrop;

mod context;
mod defs;
mod error;
mod process;
mod thread;

module! {
    type: BinderModule,
    name: "rust_binder",
    author: "Wedson Almeida Filho, Alice Ryhl",
    description: "Android Binder",
    license: "GPL",
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
    let name = unsafe { kernel::str::CStr::from_char_ptr(name) };
    match Context::new(name) {
        Ok(ctx) => Arc::into_raw(ctx) as *mut core::ffi::c_void,
        Err(_err) => return core::ptr::null_mut(),
    }
}

#[no_mangle]
unsafe extern "C" fn rust_binder_remove_device(device: *mut core::ffi::c_void) {
    if !device.is_null() {
        // SAFETY: The caller ensures that the pointer is valid.
        unsafe {
            let ctx: Arc<Context> = Arc::from_raw(device.cast());
            ctx.deregister();
            drop(ctx);
        }
    }
}

unsafe extern "C" fn rust_binder_open(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> core::ffi::c_int {
    // SAFETY: The `rust_binderfs.c` file ensures that `i_private` is set to the return value of a
    // successful call to `rust_binder_new_device`. Here we use `ManuallyDrop` to avoid dropping
    // the ref-count when `ctx` goes out of scope, as we are only borrowing the context here.
    let ctx: ManuallyDrop<Arc<Context>> =
        unsafe { ManuallyDrop::new(Arc::from_raw((*inode).i_private.cast())) };
    let process = match Process::open(&*ctx, unsafe { File::from_ptr(file) }) {
        Ok(process) => process,
        Err(err) => return err.to_errno(),
    };
    unsafe {
        (*file).private_data = process.into_foreign() as *mut core::ffi::c_void;
    }
    0
}

unsafe extern "C" fn rust_binder_release(
    _inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> core::ffi::c_int {
    let process: Arc<Process> = unsafe { Arc::from_foreign((*file).private_data) };
    let file = unsafe { File::from_ptr(file) };
    Process::release(process, file);
    0
}

unsafe extern "C" fn rust_binder_compat_ioctl(
    file: *mut bindings::file,
    cmd: core::ffi::c_uint,
    arg: core::ffi::c_ulong,
) -> core::ffi::c_long {
    let f = unsafe { Arc::borrow((*file).private_data) };
    let mut cmd = kernel::file::IoctlCommand::new(cmd as _, arg as _);
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
    let f = unsafe { Arc::borrow((*file).private_data) };
    let mut cmd = kernel::file::IoctlCommand::new(cmd as _, arg as _);
    match Process::ioctl(f, unsafe { File::from_ptr(file) }, &mut cmd) {
        Ok(ret) => ret.into(),
        Err(err) => err.to_errno().into(),
    }
}

unsafe extern "C" fn rust_binder_mmap(
    file: *mut bindings::file,
    vma: *mut bindings::vm_area_struct,
) -> core::ffi::c_int {
    let f = unsafe { Arc::borrow((*file).private_data) };
    let mut area = unsafe { kernel::mm::virt::Area::from_ptr(vma) };
    match Process::mmap(f, unsafe { File::from_ptr(file) }, &mut area) {
        Ok(()) => 0,
        Err(err) => err.to_errno().into(),
    }
}

unsafe extern "C" fn rust_binder_poll(
    file: *mut bindings::file,
    wait: *mut bindings::poll_table_struct,
) -> bindings::__poll_t {
    let f = unsafe { Arc::borrow((*file).private_data) };
    let fileref = unsafe { File::from_ptr(file) };
    match Process::poll(f, fileref, unsafe { &PollTable::from_ptr(wait) }) {
        Ok(v) => v,
        Err(_) => bindings::POLLERR,
    }
}

unsafe extern "C" fn rust_binder_flush(
    file: *mut bindings::file,
    _id: bindings::fl_owner_t,
) -> core::ffi::c_int {
    let f = unsafe { Arc::borrow((*file).private_data) };
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
