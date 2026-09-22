// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use crate::content::DmaBufContent;
use crate::defs::{WrapFdGetState, WrapFdLoad, WrapFdRewrap};
use core::ops::Deref;

use kernel::{
    anon_inodes::{anon_inode_create_getfile, AnonInodeFileInterface, AnonInodeFileVTable},
    bindings, device,
    error::to_result,
    ffi,
    fs::{file::Offset, File, LocalFile},
    mm::virt::VmaNew,
    new_mutex,
    prelude::*,
    seq_file::SeqFile,
    seq_print,
    sync::Mutex,
    task::Task,
    types::{ARef, ForeignOwnable},
    uaccess::UserSlice,
    uapi,
};

static WRAP_FOPS: AnonInodeFileVTable<Wrap> = AnonInodeFileVTable::new();

pub(crate) fn create_wrap_file(
    name: &core::ffi::CStr,
    ptr: <Wrap as AnonInodeFileInterface>::Ptr,
    flags: u32,
) -> Result<ARef<File>, (Error, <Wrap as AnonInodeFileInterface>::Ptr)> {
    anon_inode_create_getfile::<Wrap>(name, ptr, flags, &WRAP_FOPS)
}

enum WrapOwner {
    Task(ARef<Task>),

    #[allow(dead_code)]
    Device(ARef<device::Device>),

    None,
}

impl WrapOwner {
    fn get_valid_owner(&mut self) -> Option<&Task> {
        if let Self::Task(task) = self {
            // SAFETY: `task.as_ptr()` returns a raw pointer to an initialized `task_struct`.
            if unsafe { (*task.as_ptr()).mm }.is_null() {
                *self = WrapOwner::None;
            }
        };

        match self {
            Self::Task(task) => Some(task),
            _ => None,
        }
    }

    fn has_owner(&mut self) -> bool {
        match self {
            Self::None => false,
            Self::Task(_task) => self.get_valid_owner().is_some(),
            Self::Device(_dev) => true,
        }
    }

    fn owned_by_current_task(&mut self) -> bool {
        match self {
            Self::Task(_task) => {
                let Some(task) = self.get_valid_owner() else {
                    return false;
                };
                task == current!().group_leader()
            }
            _ => false,
        }
    }

    fn owned_by_device(&self, dev: &device::Device) -> bool {
        matches!(self, Self::Device(owning_dev) if *dev == **owning_dev)
    }
}

pub(crate) struct WrapInfo {
    pub(crate) content: Option<DmaBufContent>,
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

    fn is_mapped(&mut self) -> bool {
        let Some(ref vma_file) = self.vma_file else {
            return false;
        };

        // SAFETY: `vma_file` is a valid `ARef<File>`, so `vma_file.as_ptr()` is non-null and
        // valid for reads.
        let mapping = unsafe { (*vma_file.as_ptr()).f_mapping };

        // SAFETY: `mapping` points to the `struct address_space` of `vma_file`, which is kept
        // alive by our `ARef<File>` reference.
        unsafe { bindings::i_mmap_lock_read(mapping) };

        // SAFETY: Reading `i_mmap.rb_root` is synchronized under the `i_mmap_lock_read` lock.
        let rb_root_ref = unsafe { &(*mapping).i_mmap.rb_root };

        // SAFETY: `rb_root_ref` is a valid reference to `rb_root` within `struct address_space`.
        let is_mapped = !unsafe { bindings::RB_EMPTY_ROOT(rb_root_ref) };

        // SAFETY: `mapping` is valid and matches the lock acquired above.
        unsafe { bindings::i_mmap_unlock_read(mapping) };

        if !is_mapped {
            self.vma_file = None;
        }

        is_mapped
    }

    fn check_modifiable(&mut self) -> Result<()> {
        if !self.owner.owned_by_current_task() {
            return Err(EBUSY);
        }

        if self.is_mapped() {
            return Err(EINVAL);
        }

        Ok(())
    }

    fn check_modifiable_and_has_content(&mut self) -> Result<()> {
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
pub(crate) struct Wrap {
    #[pin]
    pub(crate) inner: Mutex<WrapInfo>,

    // Set once on construction, never modified afterwards, does not need to be guarded by
    // the lock.
    pub(crate) close_on_exec: bool,
}

impl Wrap {
    pub(crate) fn new(close_on_exec: bool) -> impl PinInit<Self> {
        pin_init!(Self {
            inner <- new_mutex!(WrapInfo::new()),
            close_on_exec,
        })
    }

    fn file_ioctl(&self, cmd: u32, arg: usize) -> Result<isize> {
        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;

        if !(guard.allow_guests || !guard.owner.has_owner() || guard.owner.owned_by_current_task())
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

        let guard = self.inner.lock();

        get_state_args.state = if let Some(ref content) = guard.content {
            if content.is_writable() {
                uapi::WRAPFD_CONTENT_RDWR
            } else {
                uapi::WRAPFD_CONTENT_RDONLY
            }
        } else {
            uapi::WRAPFD_CONTENT_EMPTY
        };

        drop(guard);

        writer.write(&get_state_args)?;

        Ok(0)
    }

    fn acquire_ownership(&self) -> Result<isize> {
        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;

        if guard.owner.owned_by_current_task() {
            return Ok(0);
        }

        if guard.owner.has_owner() {
            return Err(EBUSY);
        }

        if guard.is_mapped() {
            return Err(EINVAL);
        }

        if guard.content.is_none() {
            return Err(ENOENT);
        }

        guard.owner = WrapOwner::Task(ARef::from(current!().group_leader()));

        Ok(0)
    }

    fn release_ownership(&self) -> Result<isize> {
        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;

        guard.check_modifiable()?;

        guard.owner = WrapOwner::None;
        guard.allow_guests = false;

        Ok(0)
    }

    fn load(&self, user_slice: UserSlice) -> Result<isize> {
        let mut reader = user_slice.reader();
        let load_args = reader.read::<WrapFdLoad>()?;

        let file_offset: Offset = load_args.file_offs.try_into()?;
        let buf_offset: Offset = load_args.buf_offs.try_into()?;
        let len: Offset = load_args.len.try_into()?;

        if file_offset < 0 || buf_offset < 0 || len < 0 {
            return Err(EINVAL);
        }

        if load_args.reserved != 0 || load_args.pad != 0 {
            return Err(EINVAL);
        }

        let localfile = LocalFile::fget(load_args.fd)?;

        // SAFETY: `localfile` is a `LocalFile` holding an `fget` reference, so `as_ptr()` is
        // non-null.
        let f_mode = unsafe { (*localfile.as_ptr()).f_mode };

        if (f_mode & bindings::FMODE_READ) == 0 {
            return Err(EBADF);
        }

        // SAFETY: `f_op` is a valid pointer to `struct file_operations` for an open file.
        if unsafe { (*(*localfile.as_ptr()).f_op).read_iter }.is_none() {
            return Err(EINVAL);
        }

        if (f_mode & bindings::FMODE_CAN_READ) == 0 {
            return Err(EINVAL);
        }

        if (f_mode & bindings::FMODE_CAN_ODIRECT) == 0 {
            return Err(EINVAL);
        }

        let Some(end) = file_offset.checked_add(len) else {
            return Err(EINVAL);
        };

        // SAFETY: `file_inode` returns a valid `struct inode` for a live `struct file`, and
        // `i_size_read` safely reads its size under kernel RCU / lock rules.
        if end > unsafe { bindings::i_size_read(bindings::file_inode(localfile.as_ptr())) } {
            return Err(EINVAL);
        }

        let guard = self.inner.lock();

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

        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;
        guard.check_modifiable()?;

        let Some(mut content) = guard.content.take() else {
            return Err(ENOENT);
        };

        match content.make_writable((rewrap_args.prot & bindings::PROT_WRITE) != 0) {
            Ok(()) => {}
            Err(err) => {
                guard.content = Some(content);
                return Err(err);
            }
        }

        let close_on_exec = self.close_on_exec;

        let result = match KBox::pin_init(Wrap::new(close_on_exec), GFP_KERNEL) {
            Ok(new_device) => crate::WrapFdDevice::publish_wrap(new_device, content, close_on_exec),
            Err(err) => Err((err, content)),
        };

        match result {
            Err((err, content)) => {
                // Restore original wrap. XXX hasn't writeability potentially changed?
                guard.content = Some(content);

                Err(err)
            }
            Ok(fd) => Ok(fd),
        }
    }

    fn empty(&self) -> Result<isize> {
        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;
        guard.check_modifiable()?;

        if let Some(_content) = guard.content.take() {
            Ok(0)
        } else {
            Err(ENOENT)
        }
    }

    fn allow_guests(&self, allow: AllowState) -> Result<isize> {
        let mut guard = self.inner.try_lock().ok_or(EBUSY)?;

        guard.check_modifiable_and_has_content()?;
        guard.allow_guests = matches!(allow, AllowState::AllowGuests);

        Ok(0)
    }
}

#[vtable]
impl AnonInodeFileInterface for Wrap {
    type Ptr = Pin<KBox<Self>>;

    fn llseek(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        offs: Offset,
        whence: ffi::c_int,
    ) -> Result<Offset> {
        // pr_info!("Wrap::llseek\n");

        let mut guard = device.inner.try_lock().ok_or(EBUSY)?;

        let Some(ref mut content) = guard.content else {
            return Err(ENOENT);
        };

        content.llseek(offs, whence)
    }

    fn mmap(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        vma: &VmaNew,
    ) -> Result {
        // pr_info!("Wrap::mmap\n");

        let mut guard = device.inner.try_lock().ok_or(EBUSY)?;

        if !guard.allow_guests && guard.owner.has_owner() && !guard.owner.owned_by_current_task() {
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

        // SAFETY: `content.dmabuf` and `vma` are valid references, and `vm_pgoff` is safely
        // read from `vma`.
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

        // SAFETY: `vma` is a valid reference to `struct vm_area_struct`.
        let vma_file_raw = unsafe { (*vma.as_ptr()).vm_file };

        // SAFETY: In the `mmap` callback, `vm_file` points to a valid open `struct file` with
        // an existing reference count. `File::from_raw_file` safely takes ownership of a
        // reference to it.
        let vma_file: ARef<File> = unsafe { File::from_raw_file(vma_file_raw) }.into();

        if guard.is_mapped() {
            if let Some(ref old_vma_file) = guard.vma_file {
                // Let's double check that the vma_file has not changed if we're already mapped.
                if old_vma_file.as_ptr() != vma_file.as_ptr() {
                    return Err(EINVAL);
                }
            }
        } else {
            guard.vma_file = Some(vma_file);
        }

        Ok(())
    }

    fn ioctl(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        cmd: u32,
        arg: usize,
    ) -> Result<isize> {
        // pr_info!("Wrap::ioctl cmd {cmd:#x}\n");

        use kernel::ioctl::_IOC_SIZE;

        match cmd {
            uapi::WRAPFD_DEV_IOC_GET_STATE => {
                let user_slice = UserSlice::new(UserPtr::from_addr(arg), _IOC_SIZE(cmd));
                device.get_wrap_state(user_slice)
            }
            uapi::WRAPFD_DEV_IOC_ACQUIRE_OWNERSHIP => device.acquire_ownership(),
            uapi::WRAPFD_DEV_IOC_RELEASE_OWNERSHIP => device.release_ownership(),
            uapi::WRAPFD_DEV_IOC_LOAD => {
                let user_slice = UserSlice::new(UserPtr::from_addr(arg), _IOC_SIZE(cmd));
                device.load(user_slice)
            }
            uapi::WRAPFD_DEV_IOC_REWRAP => {
                let user_slice = UserSlice::new(UserPtr::from_addr(arg), _IOC_SIZE(cmd));
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
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        file: &File,
        cmd: u32,
        mut arg: usize,
    ) -> Result<isize> {
        // pr_info!("Wrap::compat_ioctl\n");

        match cmd {
            uapi::WRAPFD_DEV_IOC_GET_STATE
            | uapi::WRAPFD_DEV_IOC_LOAD
            | uapi::WRAPFD_DEV_IOC_REWRAP => {
                // These commands are associated with pointers as arguments, so use compat_ptr()
                // on them.

                // SAFETY: `compat_ptr` is safe to call on 32bit user mode pointer.
                arg = unsafe { bindings::compat_ptr(arg as u32) } as usize;
            }

            _ => {}
        }

        Self::ioctl(device, file, cmd, arg)
    }

    #[cfg(CONFIG_PROC_FS)]
    fn show_fdinfo(device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>, m: &SeqFile, _file: &File) {
        // pr_info!("Wrap::show_fdinfo\n");

        let mut guard = device.inner.lock();
        if matches!(&guard.owner, WrapOwner::Device(_dev)) {
            seq_print!(m, "owner:\t<device>\n");
        } else if let Some(task) = guard.owner.get_valid_owner() {
            seq_print!(m, "owner:\t{}\n", task.pid());
        } else {
            seq_print!(m, "owner:\t<none>\n");
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

            // SAFETY: `content.dmabuf.file()` returns a valid `struct file`, so reading `f_count`
            // is safe.
            let file_count = unsafe { bindings::file_count(content.dmabuf.file().as_ptr()) };
            seq_print!(m, "count:\t{}\n", file_count - 1);

            let dmabuf = content.dmabuf.as_ptr();

            // SAFETY: `dmabuf` is a valid pointer to `struct dma_buf` owned by `content.dmabuf`.
            unsafe {
                let exp_name = (*dmabuf).exp_name;
                if !exp_name.is_null() {
                    let exp_name = CStr::from_char_ptr(exp_name);
                    seq_print!(m, "exp_name:\t{exp_name}\n");
                }
            }

            if let Ok(Some(name)) = content.dmabuf.name() {
                seq_print!(m, "name:\t{}\n", name.deref());
            }
        }
    }
}

#[export]
/// # Safety
///
/// - `file` must be a valid, non-null pointer to a live C `struct file` created by this driver.
/// - `dev` must be a valid pointer to a live C `struct device` or null (handled gracefully).
/// - The caller must ensure `file` and `dev` remain allocated for the duration of this call.
extern "C" fn wrapfd_get_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    mappable: *mut bindings::wrapfd_mappable,
) -> ffi::c_int {
    match get_mappable(file, dev, mappable) {
        Ok(()) => 0,
        Err(err) => err.to_errno(),
    }
}

fn get_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    mappable: *mut bindings::wrapfd_mappable,
) -> Result<()> {
    if dev.is_null() {
        pr_warn!("wrapfd_get_mappable: dev is NULL!\n");
        return Err(ENODEV);
    }

    if file.is_null() {
        pr_warn!("wrapfd_get_mappable: file is NULL!\n");
        return Err(EBADF);
    }

    // SAFETY: `file` is guaranteed to be a valid pointer to a `struct file` by the C API contract.
    let file = unsafe { File::from_raw_file(file) };

    let Some(_fops) = WRAP_FOPS.borrow_private(file) else {
        return Err(EBADF);
    };

    // SAFETY: `file` is a valid pointer to `struct file`.
    let private = unsafe { (*file.as_ptr()).private_data };

    // SAFETY: Having verified that `file->f_op` matches our `AnonInodeFileVTable`, `private_data`
    // is guaranteed to hold a valid pointer to our `Wrap` instance.
    let wrap = unsafe {
        <<Wrap as AnonInodeFileInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
    };

    let Some(mut guard) = wrap.inner.try_lock() else {
        return Err(EBUSY);
    };

    // SAFETY: `dev` is verified non-null and points to a valid C `struct device`. `get_device`
    // safely increments its reference count.
    let device = unsafe { device::Device::get_device(dev) };

    if guard.owner.has_owner() && !guard.owner.owned_by_device(&device) {
        return Err(EBUSY);
    }

    if guard.is_mapped() {
        return Err(EINVAL);
    }

    let Some(ref content) = guard.content else {
        return Err(ENOENT);
    };

    // SAFETY: `mappable` is a valid, writable pointer to `struct wrapfd_mappable` per caller
    // contract.
    unsafe {
        (*mappable).dmabuf = content.dmabuf.as_ptr();
    }

    guard.owner = WrapOwner::Device(device);

    Ok(())
}

#[export]
/// # Safety
///
/// - `file` must be a valid, non-null pointer to a live C `struct file` created by this driver.
/// - `dev` must be a valid pointer to a live C `struct device` or null (handled gracefully).
/// - The caller must ensure `file` and `dev` remain allocated for the duration of this call.
unsafe extern "C" fn wrapfd_put_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    mappable: *mut bindings::wrapfd_mappable,
) -> ffi::c_int {
    match put_mappable(file, dev, mappable) {
        Ok(()) => 0,
        Err(err) => err.to_errno(),
    }
}

fn put_mappable(
    file: *mut bindings::file,
    dev: *mut bindings::device,
    _mappable: *mut bindings::wrapfd_mappable,
) -> Result<()> {
    if dev.is_null() {
        pr_warn!("wrapfd_put_mappable: dev is NULL!");
        return Err(ENODEV);
    }

    if file.is_null() {
        pr_warn!("wrapfd_put_mappable: file is NULL!\n");
        return Err(EBADF);
    }

    // SAFETY: `file` is guaranteed to be a valid pointer to a `struct file` by the C API contract.
    let file = unsafe { File::from_raw_file(file) };

    let Some(_fops) = WRAP_FOPS.borrow_private(file) else {
        return Err(EBADF);
    };

    // SAFETY: `file` is a valid pointer to `struct file`.
    let private = unsafe { (*file.as_ptr()).private_data };

    // SAFETY: Having verified that `file->f_op` matches our `AnonInodeFileVTable`, `private_data`
    // is guaranteed to hold a valid pointer to our `Wrap` instance.
    let wrap = unsafe {
        <<Wrap as AnonInodeFileInterface>::Ptr as ForeignOwnable>::borrow(private.cast())
    };

    let Some(mut guard) = wrap.inner.try_lock() else {
        return Err(EBUSY);
    };

    // SAFETY: `dev` is verified non-null and points to a valid C `struct device`. `get_device`
    // safely increments its reference count.
    let device = unsafe { device::Device::get_device(dev) };

    if guard.owner.has_owner() && !guard.owner.owned_by_device(&device) {
        return Err(EBUSY);
    }

    guard.owner = WrapOwner::None;

    Ok(())
}
