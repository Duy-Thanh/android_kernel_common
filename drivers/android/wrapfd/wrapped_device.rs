// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::marker::PhantomData;
use core::mem::MaybeUninit;

use crate::content::WrapDMAContent;
use crate::defs::{WrapFdGetState, WrapFdLoad, WrapFdRewrap};
use crate::helpers;

use kernel::{
    bindings, build_error, device,
    error::{from_err_ptr, to_result, VTABLE_DEFAULT_ERROR},
    ffi::{c_char, c_int, c_long, c_uint, c_ulong, c_void},
    fs::{File, LocalFile},
    mm::virt::VmaNew,
    new_mutex, new_spinlock,
    prelude::*,
    seq_file::SeqFile,
    sync::{Arc, Mutex, SpinLock},
    task::Task,
    types::{ARef, ForeignOwnable, ScopeGuard},
    uaccess::{UserPtr, UserSlice},
    uapi,
};

/// Trait implemented by the private data of an open wrapped device.
#[vtable]
pub(crate) trait WrappedDeviceInterface: Sized {
    /// What kind of pointer should `Self` be wrapped in.
    type Ptr: ForeignOwnable + Send + Sync;

    /// Called when the device is released.
    fn release(device: Self::Ptr, _file: &File) {
        pr_info!("WrappedDevice::release");
        drop(device);
    }

    /// Handle for mmap.
    ///
    /// This function is invoked when a user space process invokes the `mmap` system call on
    /// `file`. The function is a callback that is part of the VMA initializer. The kernel will do
    /// initial setup of the VMA before calling this function. The function can then interact with
    /// the VMA initialization by calling methods of `vma`. If the function does not return an
    /// error, the kernel will complete initialization of the VMA according to the properties of
    /// `vma`.
    fn mmap(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _vma: &VmaNew,
    ) -> Result {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Handler for ioctls.
    ///
    /// The `cmd` argument is usually manipulated using the utilities in [`kernel::ioctl`].
    ///
    /// [`kernel::ioctl`]: mod@crate::ioctl
    fn ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Handler for ioctls.
    ///
    /// Used for 32-bit userspace on 64-bit platforms.
    ///
    /// This method is optional and only needs to be provided if the ioctl relies on structures
    /// that have different layout on 32-bit and 64-bit userspace. If no implementation is
    /// provided, then `compat_ptr_ioctl` will be used instead.
    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Show info for this fd.
    fn show_fdinfo(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _m: &SeqFile,
        _file: &File,
    ) {
        build_error!(VTABLE_DEFAULT_ERROR)
    }
}

/// A vtable for the file operations of a Rust WrappedDevice.
pub(crate) struct WrappedDeviceVTable<T: WrappedDeviceInterface>(PhantomData<T>);

impl<T: WrappedDeviceInterface> WrappedDeviceVTable<T> {
    /// # Safety
    ///
    /// `file` and `inode` must be the file and inode for a file that is being released. The file
    /// must be associated with a `WrappedDeviceRegistration<T>`.
    unsafe extern "C" fn release(_inode: *mut bindings::inode, file: *mut bindings::file) -> c_int {
        // SAFETY: The release call of a file owns the private data.
        let private = unsafe { (*file).private_data };

        if private.is_null() {
            pr_warn!("release called with null private_data\n");
            return 0;
        }

        // SAFETY: The release call of a file owns the private data.
        let device = unsafe { <T::Ptr as ForeignOwnable>::from_foreign(private) };

        // XXX unlike the code that inspired this (kernel/miscdevice.rs) we do NOT construct
        // a file object for the duration of the release call because that would (temporarily)
        // increase the reference count of the file object already in the process of being released.

        // SAFETY:
        // * The file_ref is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file_ref = unsafe { &*file.cast::<File>() };
        T::release(device, file_ref);

        0
    }

    /// # Safety
    ///
    /// `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    /// `vma` must be a vma that is currently being mmap'ed with this file.
    unsafe extern "C" fn mmap(
        file: *mut bindings::file,
        vma: *mut bindings::vm_area_struct,
    ) -> c_int {
        // SAFETY: The mmap call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: This is a Rust WrappedDevice, so we call `into_foreign` in `open` and
        // `from_foreign` in `release`, and `fops_mmap` is guaranteed to be called between those
        // two operations.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };
        // SAFETY: The caller provides a vma that is undergoing initial VMA setup.
        let area = unsafe { VmaNew::from_raw(vma) };
        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };

        match T::mmap(device, file, area) {
            Ok(()) => 0,
            Err(err) => err.to_errno(),
        }
    }

    /// # Safety
    ///
    /// `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    unsafe extern "C" fn ioctl(file: *mut bindings::file, cmd: c_uint, arg: c_ulong) -> c_long {
        // SAFETY: The ioctl call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: Ioctl calls can borrow the private data of the file.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };

        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };

        match T::ioctl(device, file, cmd, arg) {
            Ok(ret) => ret as c_long,
            Err(err) => err.to_errno() as c_long,
        }
    }

    /// # Safety
    ///
    /// `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    #[cfg(CONFIG_COMPAT)]
    unsafe extern "C" fn compat_ioctl(
        file: *mut bindings::file,
        cmd: c_uint,
        arg: c_ulong,
    ) -> c_long {
        // SAFETY: The compat ioctl call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: Ioctl calls can borrow the private data of the file.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };

        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };

        match T::compat_ioctl(device, file, cmd, arg) {
            Ok(ret) => ret as c_long,
            Err(err) => err.to_errno() as c_long,
        }
    }

    /// # Safety
    ///
    /// - `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    /// - `seq_file` must be a valid `struct seq_file` that we can write to.
    unsafe extern "C" fn show_fdinfo(seq_file: *mut bindings::seq_file, file: *mut bindings::file) {
        // SAFETY: The show_fdinfo call of a file owns the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: show_fdinfo calls can borrow the private data of the file.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };

        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };
        // SAFETY: The caller ensures that the pointer is valid and exclusive for the duration in
        // which this method is called.
        let m = unsafe { SeqFile::from_raw(seq_file) };

        T::show_fdinfo(device, m, file);
    }

    const VTABLE: bindings::file_operations = bindings::file_operations {
        release: Some(Self::release),
        mmap: Some(Self::mmap),
        unlocked_ioctl: Some(Self::ioctl),
        #[cfg(CONFIG_COMPAT)]
        compat_ioctl: Some(Self::compat_ioctl),
        show_fdinfo: Some(Self::show_fdinfo),
        // SAFETY: All zeros is a valid value for `bindings::file_operations`.
        ..unsafe { MaybeUninit::zeroed().assume_init() }
    };

    pub(crate) const fn build() -> &'static bindings::file_operations {
        &Self::VTABLE
    }
}

pub(crate) fn open_wrapped_device<T: WrappedDeviceInterface>(
    name: *const c_char,
    ptr: T::Ptr,
    flags: u32,
) -> Result<ARef<File>> {
    let priv_data = ptr.into_foreign().cast_mut();

    let raw_file_or_err = from_err_ptr(unsafe {
        bindings::anon_inode_create_getfile(
            name,
            WrappedDeviceVTable::<T>::build(),
            priv_data,
            flags as i32,
            core::ptr::null(), // context_inode
        )
    });

    match raw_file_or_err {
        Err(err) => {
            // Convert foreign-owned pointer back to a rust-owned one (and drop it).
            _ = unsafe { <T::Ptr as ForeignOwnable>::from_foreign(priv_data) };
            return Err(err);
        }
        Ok(raw_file) => {
            let file: ARef<File> = unsafe { File::from_raw_file(raw_file) }.into();

            // Creating an ARef<File> from &File above added an addition reference we don't want.
            unsafe { bindings::fput(raw_file) };

            Ok(file)
        }
    }
}

enum WrapOwner {
    Task(kernel::task::Pid),
    Device(ARef<device::Device>),
    None,
}

impl WrapOwner {
    fn owned_by_current_task(&self) -> bool {
        match self {
            Self::Task(task_pid) => *task_pid == current!().pid(),
            _ => false,
        }
    }
}

pub(crate) struct WrapInfo {
    pub(crate) content: Option<Arc<WrapDMAContent>>,
    owner: WrapOwner,
    allow_guests: bool,
    use_count: usize,
    vma_file: Option<ARef<File>>,
}

impl WrapInfo {
    fn new() -> Self {
        Self {
            content: None,
            owner: WrapOwner::None,
            allow_guests: false,
            use_count: 0,
            vma_file: None,
        }
    }

    fn is_mapped(&self) -> bool {
        let Some(ref vma_file) = self.vma_file else {
            return false;
        };

        let mapping = unsafe { (*vma_file.as_ptr()).f_mapping };

        let mut is_mapped = false;
        unsafe { helpers::my_i_mmap_lock_read(mapping as *mut c_void) };
        let rb_root_ref = unsafe { &(*mapping).i_mmap.rb_root };
        is_mapped = !unsafe { helpers::my_rb_empty_root(rb_root_ref) };
        unsafe { helpers::my_i_mmap_unlock_read(mapping as *mut c_void) };

        is_mapped
    }

    fn check_modifiable(&self) -> Result<()> {
        if !self.owner.owned_by_current_task() {
            return Err(EBUSY);
        }

        if self.use_count > 0 || self.is_mapped() {
            return Err(EINVAL);
        }

        Ok(())
    }

    fn check_modifiable_and_has_content(&self) -> Result<()> {
        self.check_modifiable()?;

        if self.content.is_none() {
            return Err(ENOENT);
        }

        Ok(())
    }
}

fn is_page_aligned(addr: usize) -> bool {
    (addr & !kernel::page::PAGE_MASK) == 0
}

#[pin_data]
pub(crate) struct WrappedDevice {
    #[pin]
    pub(crate) lock: SpinLock<WrapInfo>,
    #[pin]
    usage_lock: Mutex<()>,
    #[pin]
    modifications_lock: Mutex<()>,
}

impl WrappedDevice {
    pub(crate) fn new() -> impl PinInit<Self> {
        pin_init!(Self {
            lock <- new_spinlock!(WrapInfo::new()),
            usage_lock <- new_mutex!(()),
            modifications_lock <- new_mutex!(()),
        })
    }

    fn file_ioctl(device: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
        let content = {
            let mut guard = device.lock.lock();

            if !guard.allow_guests
                && !matches!(guard.owner, WrapOwner::None)
                && !guard.owner.owned_by_current_task()
            {
                return Err(EBUSY);
            }

            // If usage is blocked, the content is being rewrapped or emptied. Treat this
            // as if the wrap was already empty.
            if device.usage_lock.try_lock().is_none() {
                return Err(ENOENT);
            }

            let Some(ref content) = guard.content else {
                return Err(ENOENT);
            };

            // This somewhat odd sequence keeps the borrow checker happy.
            let content = content.clone();

            // Increased use_count prevent changes in ownership, rewrapping or emptying
            // the content. Content is stable.
            guard.use_count += 1;

            content
        };

        let result = content.ioctl(cmd, arg);

        {
            let mut guard = device.lock.lock();
            guard.use_count -= 1;
        }

        result
    }

    fn get_wrap_state(device: Pin<&Self>, user_slice: UserSlice) -> Result<isize> {
        let (mut reader, mut writer) = user_slice.reader_writer();
        let mut get_state_args = reader.read::<WrapFdGetState>()?;

        if get_state_args.reserved != 0 || get_state_args.pad != 0 {
            return Err(EINVAL);
        }

        let mut guard = device.lock.lock();

        // Let-chains would make this a bit cleaner.
        get_state_args.state = if let Some(ref content) = guard.content {
            if device.usage_lock.try_lock().is_some() {
                if content.writable {
                    uapi::WRAPFD_CONTENT_RDWR
                } else {
                    uapi::WRAPFD_CONTENT_RDONLY
                }
            } else {
                uapi::WRAPFD_CONTENT_EMPTY
            }
        } else {
            uapi::WRAPFD_CONTENT_EMPTY
        };

        writer.write(&get_state_args)?;

        Ok(0)
    }

    fn acquire_ownership(device: Pin<&Self>) -> Result<isize> {
        let mut guard = device.lock.lock();

        if guard.owner.owned_by_current_task() {
            return Ok(0);
        }

        if !matches!(guard.owner, WrapOwner::None) {
            return Err(EBUSY);
        }

        if guard.use_count > 0 || guard.is_mapped() {
            return Err(EINVAL);
        }

        if guard.content.is_none() {
            return Err(ENOENT);
        }

        guard.owner = WrapOwner::Task(current!().pid());

        Ok(0)
    }

    fn release_ownership(device: Pin<&Self>) -> Result<isize> {
        let mut guard = device.lock.lock();

        guard.check_modifiable()?;

        guard.owner = WrapOwner::None;
        guard.allow_guests = false;

        Ok(0)
    }

    fn load(device: Pin<&Self>, user_slice: UserSlice) -> Result<isize> {
        let mut reader = user_slice.reader();
        let load_args = reader.read::<WrapFdLoad>()?;

        let file_offset: bindings::loff_t = load_args.file_offs.try_into()?;
        let buf_offset: bindings::loff_t = load_args.buf_offs.try_into()?;
        let len: bindings::loff_t = load_args.len.try_into()?;

        if !is_page_aligned(file_offset.try_into()?) || !is_page_aligned(buf_offset.try_into()?) {
            return Err(EINVAL);
        }

        if load_args.reserved != 0 || load_args.pad != 0 {
            return Err(EINVAL);
        }

        let localfile = LocalFile::fget(load_args.fd)?;

        let f_mode = unsafe { (*localfile.as_ptr()).f_mode };

        if (f_mode & bindings::MY_FMODE_READ) == 0 {
            return Err(EBADF);
        }

        if unsafe { (*(*localfile.as_ptr()).f_op).read_iter }.is_none() {
            return Err(EINVAL);
        }

        if (f_mode & bindings::MY_FMODE_CAN_READ) == 0 {
            return Err(EBADF);
        }

        if (f_mode & bindings::MY_FMODE_CAN_ODIRECT) == 0 {
            return Err(EBADF);
        }

        let Some(end) = file_offset.checked_add(len) else {
            return Err(EINVAL);
        };

        if end
            > unsafe {
                helpers::my_i_size_read(helpers::my_file_inode(localfile.as_ptr() as *const c_void))
            }
        {
            return Err(EINVAL);
        }

        let (_mod_guard, content) = {
            let mut guard = device.lock.lock();

            guard.check_modifiable()?;
            let Some(mod_guard) = device.modifications_lock.try_lock() else {
                return Err(EAGAIN);
            };

            let Some(ref content) = guard.content else {
                return Err(ENOENT);
            };

            (mod_guard, content.clone())
        };

        content.load(localfile, file_offset, buf_offset, len)
    }

    fn rewrap(device: Pin<&Self>, user_slice: UserSlice) -> Result<u32> {
        let mut reader = user_slice.reader();
        let rewrap_args = reader.read::<WrapFdRewrap>()?;

        if (rewrap_args.prot & !(bindings::PROT_WRITE | bindings::PROT_READ)) != 0 {
            return Err(EINVAL);
        }

        if rewrap_args.reserved != 0 || rewrap_args.pad != 0 {
            return Err(EINVAL);
        }

        let (_mod_guard, _usage_guard, content) = {
            let mut guard = device.lock.lock();

            guard.check_modifiable()?;

            let Some(usage_guard) = device.usage_lock.try_lock() else {
                return Err(EAGAIN);
            };

            let Some(mod_guard) = device.modifications_lock.try_lock() else {
                return Err(EAGAIN);
            };

            let Some(content) = guard.content.take() else {
                return Err(ENOENT);
            };

            let Some(mut content) = Arc::into_unique_or_drop(content) else {
                // XXX This really shouldn't happen, somehow there's another user of this
                // Arc even though we're supposed to have exclusive access by virtue of
                // having successfully locked both usage and modifications lock above.
                return Err(ENOENT);
            };

            // SAFETY: Safe because modifying the "writable" field does not violate the
            // Pin contract.
            unsafe {
                *content
                    .as_mut()
                    .map_unchecked_mut(|content| &mut content.writable) =
                    (rewrap_args.prot & bindings::PROT_WRITE) != 0;
            }

            // Convert the Pin<UniqueArc<_>> back into an Arc<_> with guaranteed refcount 1.
            (mod_guard, usage_guard, content.into())
        };

        let result = match KBox::pin_init(WrappedDevice::new(), GFP_KERNEL) {
            Ok(new_device) => crate::WrappingDevice::publish_wrap(new_device, content),
            Err(err) => Err((content, err)),
        };

        let result = match result {
            Err((content, err)) => {
                // Restore original wrap. XXX hasn't writeability potentially changed?
                let mut guard = device.lock.lock();
                guard.content = Some(content);

                Err(err)
            }
            Ok(fd) => Ok(fd),
        };

        result
    }

    fn empty(device: Pin<&Self>) -> Result<isize> {
        let mut guard = device.lock.lock();
        guard.check_modifiable()?;

        let Some(_mod_guard) = device.modifications_lock.try_lock() else {
            return Err(EAGAIN);
        };

        let Some(_usage_guard) = device.usage_lock.try_lock() else {
            return Err(EAGAIN);
        };

        let result = if let Some(_content) = guard.content.take() {
            Ok(0)
        } else {
            Err(ENOENT)
        };

        result
    }

    fn allow_guests(device: Pin<&Self>, allow: bool) -> Result<isize> {
        let mut guard = device.lock.lock();

        guard.check_modifiable_and_has_content()?;

        guard.allow_guests = allow;

        Ok(0)
    }
}

#[vtable]
impl WrappedDeviceInterface for WrappedDevice {
    type Ptr = Pin<KBox<Self>>;

    fn mmap(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        file: &File,
        vma: &VmaNew,
    ) -> Result {
        pr_info!("WrappedDevice::mmap\n");

        let content = {
            let mut guard = device.lock.lock();

            if !guard.allow_guests
                && !matches!(guard.owner, WrapOwner::None)
                && !guard.owner.owned_by_current_task()
            {
                return Err(EBUSY);
            }

            if device.usage_lock.try_lock().is_none() {
                return Err(ENOENT);
            }

            let Some(ref content) = guard.content else {
                return Err(ENOENT);
            };

            let content = content.clone();

            guard.use_count += 1;

            content
        };

        let _auto_unlock = ScopeGuard::new(|| {
            device.lock.lock().use_count -= 1;
        });

        let mut make_rdonly = false;

        if !content.writable {
            if vma.writable() {
                return Err(EACCES);
            }

            make_rdonly = (vma.flags() & kernel::mm::virt::flags::MAYWRITE) != 0;
        }

        to_result(unsafe {
            bindings::dma_buf_mmap(content.dmabuf.as_ptr(), vma.as_ptr(), 0 /* pgoff */)
        })?;

        if make_rdonly {
            if vma.writable() {
                pr_warn!("wrapfd read-only content was mapped as writable\n");
                return Err(EACCES);
            }

            vma.try_clear_maywrite()?;
        }

        let vma_file_raw = unsafe { (*vma.as_ptr()).vm_file };
        // SAFETY: There's no pending fdget_pos and vma_file_raw is (presumably) a pointer
        // to a valid file structure.
        let vma_file: ARef<File> = unsafe { File::from_raw_file(vma_file_raw) }.into();

        {
            let mut guard = device.lock.lock();

            match guard.vma_file {
                Some(ref old_vma_file) => {
                    // Let's double check that the vma_file has not changed if we're already mapped.
                    if old_vma_file.as_ptr() != vma_file.as_ptr() {
                        return Err(EINVAL);
                    }
                }
                None => {
                    guard.vma_file = Some(vma_file);
                }
            }
        }

        Ok(())
    }

    fn ioctl(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        cmd: u32,
        arg: usize,
    ) -> Result<isize> {
        pr_info!("WrappedDevice::ioctl cmd {cmd:#x}\n");

        use kernel::ioctl::_IOC_SIZE;

        match cmd {
            uapi::WRAPFD_DEV_IOC_GET_STATE => {
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
                Self::get_wrap_state(device, user_slice)
            }
            uapi::WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP => Self::acquire_ownership(device),
            uapi::WRAPFD_DEV_IOC_RELEASE_OWNERSHIP => Self::release_ownership(device),
            uapi::WRAPFD_DEV_IOC_LOAD => {
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
                Self::load(device, user_slice)
            }
            uapi::WRAPFD_DEV_IOC_REWRAP => {
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
                Self::rewrap(device, user_slice).map(|fd| fd as isize)
            }
            uapi::WRAPFD_DEV_IOC_EMPTY => Self::empty(device),
            uapi::WRAPFD_DEV_IOC_ALLOW_GUESTS => Self::allow_guests(device, true),
            uapi::WRAPFD_DEV_IOC_PROHIBIT_GUESTS => Self::allow_guests(device, false),
            _ => Self::file_ioctl(device, cmd, arg),
        }
    }

    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        pr_info!("WrappedDevice::compat_ioctl\n");
        Err(ENOTTY)
    }

    fn show_fdinfo(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _m: &SeqFile,
        _file: &File,
    ) {
        pr_info!("WrappedDevice::show_fdinfo\n");
    }
}
