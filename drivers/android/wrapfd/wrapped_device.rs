// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use crate::content::WrapDMAContent;
use crate::defs::{WrapFdGetState, WrapFdLoad, WrapFdRewrap};
use crate::rw_semaphore::RwSemaphore;

use kernel::{
    anon_inodes::{anon_inode_create_getfile, AnonInodeDeviceInterface, AnonInodeDeviceVTable},
    bindings, device,
    error::to_result,
    ffi,
    fs::{File, LocalFile},
    mm::virt::VmaNew,
    prelude::*,
    seq_file::SeqFile,
    seq_print,
    types::{ARef, ForeignOwnable},
    uaccess::UserSlice,
    uapi,
};

pub(crate) fn open_wrapped_device<T: AnonInodeDeviceInterface>(
    name: &core::ffi::CStr,
    ptr: T::Ptr,
    flags: u32,
) -> Result<ARef<File>, (Error, T::Ptr)> {
    anon_inode_create_getfile::<T>(name, ptr, flags)
}

enum WrapOwner {
    Task(kernel::task::Pid),

    #[allow(dead_code)]
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

        // SAFETY: The file is valid because we hold a reference to it.
        let mapping = unsafe { (*vma_file.as_ptr()).f_mapping };

        // SAFETY: "mapping" has been extracted from the underlying file structure and
        // is a valid pointer to an address_space.
        unsafe { bindings::i_mmap_lock_read(mapping) };

        // SAFETY: "mapping" has been extracted from the underlying file structure and
        // is a valid pointer to an address_space.
        let rb_root_ref = unsafe { &(*mapping).i_mmap.rb_root };

        // SAFETY: "rb_root_ref" has been extracted from an address_space structure.
        let is_mapped = !unsafe { bindings::RB_EMPTY_ROOT(rb_root_ref) };

        // SAFETY: "mapping" has been extracted from the underlying file structure and
        // is a valid pointer to an address_space.
        unsafe { bindings::i_mmap_unlock_read(mapping) };

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

enum AllowState {
    AllowGuests,
    ProhibitGuests,
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

    fn file_ioctl(&self, cmd: u32, arg: usize) -> Result<isize> {
        let guard = self.lock.try_read()?;

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

    fn get_wrap_state(&self, user_slice: UserSlice) -> Result<isize> {
        let (mut reader, mut writer) = user_slice.reader_writer();
        let mut get_state_args = reader.read::<WrapFdGetState>()?;

        if get_state_args.reserved != 0 || get_state_args.pad != 0 {
            return Err(EINVAL);
        }

        get_state_args.state = if let Ok(guard) = self.lock.try_read() {
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

    fn acquire_ownership(&self) -> Result<isize> {
        let mut guard = self.lock.try_write()?;

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

    fn release_ownership(&self) -> Result<isize> {
        let mut guard = self.lock.try_write()?;

        guard.check_modifiable()?;

        guard.owner = WrapOwner::None;
        guard.allow_guests = false;

        Ok(0)
    }

    fn load(&self, user_slice: UserSlice) -> Result<isize> {
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

        // SAFETY: The file is valid because we hold a reference to it.
        let f_mode = unsafe { (*localfile.as_ptr()).f_mode };

        if (f_mode & bindings::FMODE_READ) == 0 {
            return Err(EBADF);
        }

        // SAFETY: The file is valid because we hold a reference to it.
        if unsafe { (*(*localfile.as_ptr()).f_op).read_iter }.is_none() {
            return Err(EINVAL);
        }

        if (f_mode & bindings::FMODE_CAN_READ) == 0 {
            return Err(EBADF);
        }

        if (f_mode & bindings::FMODE_CAN_ODIRECT) == 0 {
            return Err(EBADF);
        }

        let Some(end) = file_offset.checked_add(len) else {
            return Err(EINVAL);
        };

        // SAFETY: The file is valid because we hold a reference to it.
        if end > unsafe { bindings::i_size_read(bindings::file_inode(localfile.as_ptr())) } {
            return Err(EINVAL);
        }

        let guard = self.lock.try_read()?;

        let Some(ref content) = guard.content else {
            return Err(ENOENT);
        };

        if !content.is_writable() {
            return Err(EACCES);
        }

        content.load(localfile, file_offset, buf_offset, len)
    }

    fn rewrap(&self, user_slice: UserSlice) -> Result<u32> {
        let mut reader = user_slice.reader();
        let rewrap_args = reader.read::<WrapFdRewrap>()?;

        if (rewrap_args.prot & !(bindings::PROT_WRITE | bindings::PROT_READ)) != 0 {
            return Err(EINVAL);
        }

        if rewrap_args.reserved != 0 || rewrap_args.pad != 0 {
            return Err(EINVAL);
        }

        let mut guard = self.lock.try_write()?;
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
            Err(err) => Err((err, content)),
        };

        let result = match result {
            Err((err, content)) => {
                // Restore original wrap. XXX hasn't writeability potentially changed?
                guard.content = Some(content);

                Err(err)
            }
            Ok(fd) => Ok(fd),
        };

        result
    }

    fn empty(&self) -> Result<isize> {
        let mut guard = self.lock.try_write()?;
        guard.check_modifiable()?;

        let result = if let Some(_content) = guard.content.take() {
            Ok(0)
        } else {
            Err(ENOENT)
        };

        result
    }

    fn allow_guests(&self, allow: AllowState) -> Result<isize> {
        let mut guard = self.lock.try_write()?;

        guard.check_modifiable_and_has_content()?;
        guard.allow_guests = matches!(allow, AllowState::AllowGuests);

        Ok(0)
    }
}

#[vtable]
impl AnonInodeDeviceInterface for WrappedDevice {
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
        _file: &File,
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

        // SAFETY: We hold references to "vma" as well as to the content's "dmabuf".
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

        // SAFETY: We hold a reference to "vma".
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
                device.get_wrap_state(user_slice)
            }
            uapi::WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP => device.acquire_ownership(),
            uapi::WRAPFD_DEV_IOC_RELEASE_OWNERSHIP => device.release_ownership(),
            uapi::WRAPFD_DEV_IOC_LOAD => {
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
                device.load(user_slice)
            }
            uapi::WRAPFD_DEV_IOC_REWRAP => {
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
                device.rewrap(user_slice).map(|fd| fd as isize)
            }
            uapi::WRAPFD_DEV_IOC_EMPTY => device.empty(),
            uapi::WRAPFD_DEV_IOC_ALLOW_GUESTS => device.allow_guests(AllowState::AllowGuests),
            uapi::WRAPFD_DEV_IOC_PROHIBIT_GUESTS => device.allow_guests(AllowState::ProhibitGuests),
            _ => device.file_ioctl(cmd, arg),
        }
    }

    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: u32,
        _arg: usize,
    ) -> Result<isize> {
        // pr_info!("WrappedDevice::compat_ioctl\n");
        Err(ENOTTY)
    }

    #[cfg(CONFIG_PROC_FS)]
    fn show_fdinfo(device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>, m: &SeqFile, _file: &File) {
        // pr_info!("WrappedDevice::show_fdinfo\n");

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

            // SAFETY: It's always safe to call file_count on a valid file pointer.
            let file_count = unsafe { bindings::file_count(content.dmabuf.file().as_ptr()) };
            seq_print!(m, "count:\t{}\n", file_count - 1);

            let dmabuf = content.dmabuf.as_ptr();

            // SAFETY: We hold a reference to "dmabuf".
            unsafe {
                let exp_name = (*dmabuf).exp_name;
                if !exp_name.is_null() {
                    let exp_name = CStr::from_char_ptr(exp_name);
                    seq_print!(m, "exp_name:\t{exp_name}\n");
                }
            }

            // SAFETY: We hold a reference to "dmabuf".
            let name_lock = unsafe { &mut (*dmabuf).name_lock };

            // SAFETY: It's always safe to call spin_lock on a valid pointer to a spinlock_t.
            unsafe {
                bindings::spin_lock(name_lock);
            }

            // SAFETY: We hold a reference to "dmabuf".
            unsafe {
                let name = (*dmabuf).name;
                if !name.is_null() {
                    let name = CStr::from_char_ptr(name);
                    seq_print!(m, "name:\t{name}\n");
                }
            };

            // SAFETY: It's always safe to call spin_unlock on a valid pointer to a spinlock_t.
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

    // SAFETY: "file" is a valid pointer to a "file" structure, by contract.
    if unsafe { (*file).f_op } != AnonInodeDeviceVTable::<WrappedDevice>::build() {
        return -(bindings::EBADF as ffi::c_int);
    }

    // SAFETY: "file" is a valid pointer to a "file" structure, by contract.
    let private = unsafe { (*file).private_data };

    // SAFETY: We verified that the file operations pointer f_op matches what we expect
    // for a file created through anon_inode_create_getfile, we therefore expect private_data
    // to conform to AnonInodeDeviceInterface::Ptr.
    let device = unsafe {
        <<WrappedDevice as AnonInodeDeviceInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
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

    // SAFETY: "mappable" is a valid pointer to a "wrapfd_mappable" structure, by contract.
    unsafe {
        (*mappable).dmabuf = content.dmabuf.as_ptr();
    }

    // SAFETY: We verfied that "dev" is non-null. It should have a non-zero reference count by
    // contract.
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

    // SAFETY: "file" is a valid pointer to a "file" structure, by contract.
    if unsafe { (*file).f_op } != AnonInodeDeviceVTable::<WrappedDevice>::build() {
        return -(bindings::EBADF as ffi::c_int);
    }

    // SAFETY: "file" is a valid pointer to a "file" structure, by contract.
    let private = unsafe { (*file).private_data };

    // SAFETY: We verified that the file operations pointer f_op matches what we expect
    // for a file created through anon_inode_create_getfile, we therefore expect private_data
    // to conform to AnonInodeDeviceInterface::Ptr.
    let device = unsafe {
        <<WrappedDevice as AnonInodeDeviceInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
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
