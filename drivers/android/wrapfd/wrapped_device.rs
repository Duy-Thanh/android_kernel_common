// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use core::marker::PhantomData;
use core::mem::MaybeUninit;

use crate::content::WrapDMAContent;
use crate::defs::{WrapFdGetState, WrapFdLoad, WrapFdMappable, WrapFdRewrap};
use crate::rw_semaphore::RwSemaphore;
use crate::{helpers, helpers::is_page_aligned};

use kernel::{
    bindings, build_error, device,
    error::{from_err_ptr, to_result, VTABLE_DEFAULT_ERROR},
    ffi,
    fs::{File, LocalFile},
    mm::virt::VmaNew,
    new_mutex, new_spinlock,
    prelude::*,
    seq_file::SeqFile,
    seq_print,
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

    /// Handler for llseek.
    ///
    fn llseek(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _offs: bindings::loff_t,
        _whence: ffi::c_int,
    ) -> Result<bindings::loff_t> {
        build_error!(VTABLE_DEFAULT_ERROR)
    }

    /// Handler for mmap.
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

    #[cfg(CONFIG_PROC_FS)]
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
    unsafe extern "C" fn release(
        _inode: *mut bindings::inode,
        file: *mut bindings::file,
    ) -> ffi::c_int {
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
    unsafe extern "C" fn llseek(
        file: *mut bindings::file,
        offs: bindings::loff_t,
        whence: ffi::c_int,
    ) -> bindings::loff_t {
        // SAFETY: The mmap call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: This is a Rust WrappedDevice, so we call `into_foreign` in `open` and
        // `from_foreign` in `release`, and `fops_mmap` is guaranteed to be called between those
        // two operations.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };
        match T::llseek(device, offs, whence) {
            Ok(pos) => pos,
            Err(err) => err.to_errno().into(),
        }
    }

    /// # Safety
    ///
    /// `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    /// `vma` must be a vma that is currently being mmap'ed with this file.
    unsafe extern "C" fn mmap(
        file: *mut bindings::file,
        vma: *mut bindings::vm_area_struct,
    ) -> ffi::c_int {
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
    unsafe extern "C" fn ioctl(
        file: *mut bindings::file,
        cmd: ffi::c_uint,
        arg: ffi::c_ulong,
    ) -> ffi::c_long {
        // SAFETY: The ioctl call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: Ioctl calls can borrow the private data of the file.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };

        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };

        match T::ioctl(device, file, cmd, arg) {
            Ok(ret) => ret as ffi::c_long,
            Err(err) => err.to_errno() as ffi::c_long,
        }
    }

    /// # Safety
    ///
    /// `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    #[cfg(CONFIG_COMPAT)]
    unsafe extern "C" fn compat_ioctl(
        file: *mut bindings::file,
        cmd: ffi::c_uint,
        arg: ffi::c_ulong,
    ) -> ffi::c_long {
        // SAFETY: The compat ioctl call of a file can access the private data.
        let private = unsafe { (*file).private_data };
        // SAFETY: Ioctl calls can borrow the private data of the file.
        let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private.cast()) };

        // SAFETY:
        // * The file is valid for the duration of this call.
        // * There is no active fdget_pos region on the file on this thread.
        let file = unsafe { File::from_raw_file(file) };

        match T::compat_ioctl(device, file, cmd, arg) {
            Ok(ret) => ret as ffi::c_long,
            Err(err) => err.to_errno() as ffi::c_long,
        }
    }

    /// # Safety
    ///
    /// - `file` must be a valid file that is associated with a `WrappedDeviceRegistration<T>`.
    /// - `seq_file` must be a valid `struct seq_file` that we can write to.
    #[cfg(CONFIG_PROC_FS)]
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
        llseek: Some(Self::llseek),
        mmap: Some(Self::mmap),
        unlocked_ioctl: Some(Self::ioctl),
        #[cfg(CONFIG_COMPAT)]
        compat_ioctl: Some(Self::compat_ioctl),
        #[cfg(CONFIG_PROC_FS)]
        show_fdinfo: Some(Self::show_fdinfo),
        // SAFETY: All zeros is a valid value for `bindings::file_operations`.
        ..unsafe { MaybeUninit::zeroed().assume_init() }
    };

    pub(crate) const fn build() -> &'static bindings::file_operations {
        &Self::VTABLE
    }
}

pub(crate) fn open_wrapped_device<T: WrappedDeviceInterface>(
    name: *const ffi::c_char,
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
    pub(crate) content: Option<WrapDMAContent>,
    owner: WrapOwner,
    allow_guests: bool,
    vma_file: Option<ARef<File>>,
}

impl WrapInfo {
    fn new() -> Self {
        Self {
            content: None,
            owner: WrapOwner::None,
            allow_guests: false,
            vma_file: None,
        }
    }

    fn is_mapped(&self) -> bool {
        let Some(ref vma_file) = self.vma_file else {
            return false;
        };

        let mapping = unsafe { (*vma_file.as_ptr()).f_mapping };

        let mut is_mapped = false;
        unsafe { helpers::my_i_mmap_lock_read(mapping as *mut ffi::c_void) };
        let rb_root_ref = unsafe { &(*mapping).i_mmap.rb_root };
        is_mapped = !unsafe { helpers::my_rb_empty_root(rb_root_ref) };
        unsafe { helpers::my_i_mmap_unlock_read(mapping as *mut ffi::c_void) };

        is_mapped
    }

    fn check_modifiable(&self) -> Result<()> {
        if !self.owner.owned_by_current_task() {
            return Err(EBUSY);
        }

        if self.is_mapped() {
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

#[pin_data]
pub(crate) struct WrappedDevice {
    #[pin]
    pub(crate) lock: RwSemaphore<WrapInfo>,
}

impl WrappedDevice {
    pub(crate) fn new() -> impl PinInit<Self> {
        pin_init!(Self {
            lock <- RwSemaphore::new(WrapInfo::new()),
        })
    }

    fn file_ioctl(device: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
        let guard = device.lock.try_read()?;

        if !guard.allow_guests
            && !matches!(guard.owner, WrapOwner::None)
            && !guard.owner.owned_by_current_task()
        {
            return Err(EBUSY);
        }

        let Some(ref content) = guard.content else {
            return Err(ENOENT);
        };

        content.ioctl(cmd, arg)
    }

    fn get_wrap_state(device: Pin<&Self>, user_slice: UserSlice) -> Result<isize> {
        let (mut reader, mut writer) = user_slice.reader_writer();
        let mut get_state_args = reader.read::<WrapFdGetState>()?;

        if get_state_args.reserved != 0 || get_state_args.pad != 0 {
            return Err(EINVAL);
        }

        get_state_args.state = if let Ok(guard) = device.lock.try_read() {
            if let Some(ref content) = guard.content {
                if content.is_writable() {
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
        let mut guard = device.lock.try_write()?;

        if guard.owner.owned_by_current_task() {
            return Ok(0);
        }

        if !matches!(guard.owner, WrapOwner::None) {
            return Err(EBUSY);
        }

        if guard.is_mapped() {
            return Err(EINVAL);
        }

        if guard.content.is_none() {
            return Err(ENOENT);
        }

        guard.owner = WrapOwner::Task(current!().pid());

        Ok(0)
    }

    fn release_ownership(device: Pin<&Self>) -> Result<isize> {
        let mut guard = device.lock.try_write()?;

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

        if file_offset < 0 || buf_offset < 0 || len < 0 {
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
                helpers::my_i_size_read(helpers::my_file_inode(
                    localfile.as_ptr() as *const ffi::c_void
                ))
            }
        {
            return Err(EINVAL);
        }

        let guard = device.lock.try_read()?;

        let Some(ref content) = guard.content else {
            return Err(ENOENT);
        };

        if !content.is_writable() {
            return Err(EACCES);
        }

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

        let mut guard = device.lock.try_write()?;
        guard.check_modifiable()?;

        let Some(mut content) = guard.content.take() else {
            return Err(ENOENT);
        };

        match content.make_writable((rewrap_args.prot & bindings::PROT_WRITE) != 0) {
            Ok(_) => {}
            Err(err) => {
                guard.content = Some(content);
                return Err(err);
            }
        }

        let result = match KBox::pin_init(WrappedDevice::new(), GFP_KERNEL) {
            Ok(new_device) => crate::WrappingDevice::publish_wrap(new_device, content),
            Err(err) => Err((content, err)),
        };

        let result = match result {
            Err((content, err)) => {
                // Restore original wrap. XXX hasn't writeability potentially changed?
                guard.content = Some(content);

                Err(err)
            }
            Ok(fd) => Ok(fd),
        };

        result
    }

    fn empty(device: Pin<&Self>) -> Result<isize> {
        let mut guard = device.lock.try_write()?;
        guard.check_modifiable()?;

        let result = if let Some(_content) = guard.content.take() {
            Ok(0)
        } else {
            Err(ENOENT)
        };

        result
    }

    fn allow_guests(device: Pin<&Self>, allow: bool) -> Result<isize> {
        let mut guard = device.lock.try_write()?;

        guard.check_modifiable_and_has_content()?;
        guard.allow_guests = allow;

        Ok(0)
    }
}

#[vtable]
impl WrappedDeviceInterface for WrappedDevice {
    type Ptr = Pin<KBox<Self>>;

    fn llseek(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        offs: bindings::loff_t,
        whence: ffi::c_int,
    ) -> Result<bindings::loff_t> {
        // pr_info!("WrappedDevice::llseek\n");

        let guard = device.lock.try_read()?;

        let Some(ref content) = guard.content else {
            return Err(ENOENT);
        };

        content.llseek(offs, whence)
    }

    fn mmap(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        file: &File,
        vma: &VmaNew,
    ) -> Result {
        // pr_info!("WrappedDevice::mmap\n");

        let mut guard = device.lock.try_write()?;

        if !guard.allow_guests
            && !matches!(guard.owner, WrapOwner::None)
            && !guard.owner.owned_by_current_task()
        {
            return Err(EBUSY);
        }

        let Some(ref content) = guard.content else {
            return Err(ENOENT);
        };

        let mut make_rdonly = false;

        if !content.is_writable() {
            if vma.writable() {
                return Err(EACCES);
            }

            make_rdonly = (vma.flags() & kernel::mm::virt::flags::MAYWRITE) != 0;
        }

        to_result(unsafe {
            let pgoff = (*vma.as_ptr()).vm_pgoff;
            bindings::dma_buf_mmap(content.dmabuf.as_ptr(), vma.as_ptr(), pgoff)
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

        Ok(())
    }

    fn ioctl(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        cmd: u32,
        arg: usize,
    ) -> Result<isize> {
        // pr_info!("WrappedDevice::ioctl cmd {cmd:#x}\n");

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

    #[cfg(CONFIG_PROC_FS)]
    fn show_fdinfo(device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>, m: &SeqFile, _file: &File) {
        pr_info!("WrappedDevice::show_fdinfo\n");

        let guard = device.lock.read();
        match guard.owner {
            WrapOwner::None => seq_print!(m, "owner:\t<none>\n"),
            WrapOwner::Task(pid) => seq_print!(m, "owner:\t{pid}\n"),
            WrapOwner::Device(_) => seq_print!(m, "owner:\t<device>\n"),
        }
        seq_print!(
            m,
            "guests:\t{}\n",
            if guard.allow_guests { "yes" } else { "no" }
        );
        seq_print!(
            m,
            "maps:\t{}\n",
            if guard.is_mapped() { "yes" } else { "no" }
        );

        if let Some(ref content) = guard.content {
            seq_print!(
                m,
                "rdonly:\t{}\n",
                if content.is_writable() { "no" } else { "yes" }
            );
            seq_print!(m, "type:\tdmabuf\n");

            seq_print!(m, "size:\t{}\n", content.dmabuf.size());

            let file_count =
                unsafe { helpers::my_file_count(content.dmabuf.file() as *mut ffi::c_void) };
            seq_print!(m, "count:\t{}\n", file_count - 1);

            let dmabuf = content.dmabuf.as_ptr();

            unsafe {
                let exp_name = (*dmabuf).exp_name;
                if !exp_name.is_null() {
                    let exp_name = CStr::from_char_ptr(exp_name);
                    seq_print!(m, "exp_name:\t{exp_name}\n");
                }
            }

            let name_lock = unsafe { &mut (*dmabuf).name_lock };

            unsafe {
                bindings::spin_lock(name_lock);
            }

            unsafe {
                let name = (*dmabuf).name;
                if !name.is_null() {
                    let name = CStr::from_char_ptr(name);
                    seq_print!(m, "name:\t{name}\n");
                }
            };

            unsafe {
                bindings::spin_unlock(name_lock);
            }
        }
    }
}

// #[export]
#[no_mangle]
extern "C" fn wrapfd_get_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    mappable: *mut bindings::wrapfd_mappable,
) -> ffi::c_int {
    if dev.is_null() {
        pr_warn!("wrapfd_get_mappable: dev is NULL!");
        return -(bindings::ENODEV as ffi::c_int);
    }

    if unsafe { (*file).f_op } != WrappedDeviceVTable::<WrappedDevice>::build() {
        return -(bindings::EBADF as ffi::c_int);
    }

    let private = unsafe { (*file).private_data };
    let device = unsafe {
        <<WrappedDevice as WrappedDeviceInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
    };

    let Ok(mut guard) = device.lock.try_write() else {
        return -(bindings::EBUSY as ffi::c_int);
    };

    if !matches!(guard.owner, WrapOwner::None) && !guard.owner.owned_by_current_task() {
        return -(bindings::EBUSY as ffi::c_int);
    }

    if guard.is_mapped() {
        return -(bindings::EINVAL as ffi::c_int);
    }

    let Some(ref content) = guard.content else {
        return -(bindings::ENOENT as ffi::c_int);
    };

    use core::ops::DerefMut;
    unsafe {
        (*mappable).dmabuf = content.dmabuf.as_ptr();
    }

    guard.owner = WrapOwner::Device(unsafe { device::Device::get_device(dev) });

    0
}

// #[export]
#[no_mangle]
unsafe extern "C" fn wrapfd_put_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    _mappable: *mut bindings::wrapfd_mappable,
) -> ffi::c_int {
    if dev.is_null() {
        pr_warn!("wrapfd_put_mappable: dev is NULL!");
        return -(bindings::ENODEV as ffi::c_int);
    }

    if unsafe { (*file).f_op } != WrappedDeviceVTable::<WrappedDevice>::build() {
        return -(bindings::EBADF as ffi::c_int);
    }

    let private = unsafe { (*file).private_data };
    let device = unsafe {
        <<WrappedDevice as WrappedDeviceInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
    };

    let Ok(mut guard) = device.lock.try_write() else {
        return -(bindings::EBUSY as ffi::c_int);
    };

    if !guard.owner.owned_by_current_task() {
        return -(bindings::EBUSY as ffi::c_int);
    }

    guard.owner = WrapOwner::None;

    0
}
