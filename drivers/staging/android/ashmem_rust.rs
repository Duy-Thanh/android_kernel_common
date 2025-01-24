// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Anonymous Shared Memory Subsystem for Android.
//!
//! The ashmem subsystem is a new shared memory allocator, similar to POSIX SHM but with different
//! behavior and sporting a simpler file-based API.
//!
//! It is, in theory, a good memory allocator for low-memory devices, because it can discard shared
//! memory units when under memory pressure.

use core::{ffi::c_int, pin::Pin};
use kernel::{
    bindings::{self, ASHMEM_GET_PIN_STATUS, ASHMEM_PIN, ASHMEM_UNPIN},
    c_str,
    error::Result,
    fs::{File, LocalFile},
    ioctl::_IOC_SIZE,
    miscdevice::{loff_t, IovIter, Kiocb, MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    mm::virt::{flags as vma_flags, VmAreaNew},
    page::page_align,
    prelude::*,
    seq_file::{seq_print, SeqFile},
    sync::{new_mutex, Mutex},
    task::Task,
    uaccess::{UserSlice, UserSliceReader, UserSliceWriter},
};

const ASHMEM_NAME_LEN: usize = bindings::ASHMEM_NAME_LEN as usize;
const ASHMEM_FULL_NAME_LEN: usize = bindings::ASHMEM_FULL_NAME_LEN as usize;
const ASHMEM_NAME_PREFIX_LEN: usize = bindings::ASHMEM_NAME_PREFIX_LEN as usize;
const ASHMEM_NAME_PREFIX: [u8; ASHMEM_NAME_PREFIX_LEN] = *b"dev/ashmem/";

const PROT_READ: usize = bindings::PROT_READ as usize;
const PROT_EXEC: usize = bindings::PROT_EXEC as usize;
const PROT_WRITE: usize = bindings::PROT_WRITE as usize;
const PROT_MASK: usize = PROT_EXEC | PROT_READ | PROT_WRITE;

mod shmem;
use shmem::ShmemFile;

/// Does PROT_READ imply PROT_EXEC for this task?
fn read_implies_exec(task: &Task) -> bool {
    // SAFETY: Always safe to read.
    let personality = unsafe { (*task.as_ptr()).personality };
    (personality & bindings::READ_IMPLIES_EXEC) != 0
}

/// Calls `capable(CAP_SYS_ADMIN)`.
fn has_cap_sys_admin() -> bool {
    use kernel::bindings::CAP_SYS_ADMIN;
    unsafe { bindings::capable(CAP_SYS_ADMIN as c_int) }
}

module! {
    type: AshmemModule,
    name: "ashmem_rust",
    author: "Alice Ryhl",
    description: "Anonymous Shared Memory Subsystem",
    license: "GPL",
}

struct AshmemModule {
    _misc: Pin<Box<MiscDeviceRegistration<Ashmem>>>,
}

impl kernel::Module for AshmemModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        // SAFETY: Called once since this is the module initializer.
        unsafe { shmem::SHMEM_FOPS_ONCE.init() };

        pr_info!("Using Rust implementation.");

        Ok(Self {
            _misc: Box::pin_init(
                MiscDeviceRegistration::register(MiscDeviceOptions {
                    name: c_str!("ashmem"),
                }),
                GFP_KERNEL,
            )?,
        })
    }
}

/// Represents an open ashmem file.
#[pin_data]
struct Ashmem {
    #[pin]
    inner: Mutex<AshmemInner>,
}

struct AshmemInner {
    size: usize,
    prot_mask: usize,
    /// If set, then this holds the ashmem name without the dev/ashmem/ prefix. No zero terminator.
    name: Option<Vec<u8>>,
    file: Option<ShmemFile>,
}

#[vtable]
impl MiscDevice for Ashmem {
    type Ptr = Pin<Box<Self>>;

    fn open(_: &File, _: &MiscDeviceRegistration<Ashmem>) -> Result<Pin<Box<Self>>> {
        Box::try_pin_init(
            try_pin_init! {
                Ashmem {
                    inner <- new_mutex!(AshmemInner {
                        size: 0,
                        prot_mask: PROT_MASK,
                        name: None,
                        file: None,
                    }),
                }
            },
            GFP_KERNEL,
        )
    }

    fn mmap(me: Pin<&Ashmem>, _file: &File, vma: &VmAreaNew) -> Result<()> {
        let asma = &mut *me.inner.lock();

        // User needs to SET_SIZE before mapping.
        if asma.size == 0 {
            return Err(EINVAL);
        }

        // Requested mapping size larger than object size.
        if vma.end() - vma.start() > page_align(asma.size) {
            return Err(EINVAL);
        }

        if asma.prot_mask & PROT_WRITE == 0 {
            vma.try_clear_maywrite().map_err(|_| EPERM)?;
        }
        if asma.prot_mask & PROT_EXEC == 0 {
            vma.try_clear_mayexec().map_err(|_| EPERM)?;
        }
        if asma.prot_mask & PROT_READ == 0 {
            vma.try_clear_mayread().map_err(|_| EPERM)?;
        }

        let file = match asma.file.as_ref() {
            Some(file) => file,
            None => {
                let mut name_buffer = [0u8; ASHMEM_FULL_NAME_LEN];
                let name = asma.full_name(&mut name_buffer);
                asma.file
                    .insert(ShmemFile::new(name, asma.size, vma.flags())?)
            }
        };

        if vma.flags() & vma_flags::SHARED != 0 {
            // We're really using this just to set vm_ops to `shmem_anon_vm_ops`. Anything else it
            // does is undone by the call to `set_file` below.
            shmem::zero_setup(vma)?;
        } else {
            shmem::vma_set_anonymous(vma);
        }

        shmem::set_file(vma, file.file());
        Ok(())
    }

    fn llseek(me: Pin<&Ashmem>, file: &LocalFile, offset: loff_t, whence: c_int) -> Result<loff_t> {
        let asma_file = {
            let asma = me.inner.lock();
            if asma.size == 0 {
                return Err(EINVAL);
            }
            match asma.file.as_ref() {
                Some(asma_file) => asma_file.clone(),
                None => return Err(EBADF),
            }
        };

        let ret = asma_file.vfs_llseek(offset, whence)?;

        // SAFETY: We protect the shmem file with the same mechanism as the ashmem file. We are in
        // llseek, so our caller ensures that accessing f_pos is okay.
        unsafe { shmem::file_set_fpos(file, shmem::file_get_fpos(asma_file.file())) };

        Ok(ret)
    }

    fn read_iter(mut kiocb: Kiocb<'_, Self::Ptr>, iov: &mut IovIter) -> Result<usize> {
        let me = kiocb.private_data();
        let asma_file = {
            let asma = me.inner.lock();
            if asma.size == 0 {
                // If size is not set, or set to 0, always return EOF.
                return Ok(0);
            }
            match asma.file.as_ref() {
                Some(asma_file) => asma_file.clone(),
                None => return Err(EBADF),
            }
        };

        let ret = asma_file.vfs_iter_read(iov, kiocb.ki_pos_mut())?;

        // SAFETY: We protect the shmem file with the same mechanism as the ashmem file. We are in
        // read_iter, so our caller ensures that accessing f_pos is okay.
        unsafe { shmem::file_set_fpos(asma_file.file(), kiocb.ki_pos()) };

        Ok(ret as usize)
    }

    fn ioctl(me: Pin<&Ashmem>, _file: &File, cmd: u32, arg: usize) -> Result<isize> {
        let size = _IOC_SIZE(cmd);
        match cmd {
            bindings::ASHMEM_SET_NAME => me.set_name(UserSlice::new(arg, size).reader()),
            bindings::ASHMEM_GET_NAME => me.get_name(UserSlice::new(arg, size).writer()),
            bindings::ASHMEM_SET_SIZE => me.set_size(arg),
            bindings::ASHMEM_GET_SIZE => me.get_size(),
            bindings::ASHMEM_SET_PROT_MASK => me.set_prot_mask(arg),
            bindings::ASHMEM_GET_PROT_MASK => me.get_prot_mask(),
            bindings::ASHMEM_GET_FILE_ID => me.get_file_id(UserSlice::new(arg, size).writer()),
            ASHMEM_PIN => Ok(bindings::ASHMEM_NOT_PURGED as isize),
            ASHMEM_UNPIN => Ok(0),
            ASHMEM_GET_PIN_STATUS => Ok(bindings::ASHMEM_IS_PINNED as isize),
            bindings::ASHMEM_PURGE_ALL_CACHES => me.purge_all_caches(),
            _ => Err(EINVAL),
        }
    }

    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(me: Pin<&Ashmem>, file: &File, compat_cmd: u32, arg: usize) -> Result<isize> {
        let cmd = match compat_cmd {
            bindings::COMPAT_ASHMEM_SET_SIZE => bindings::ASHMEM_SET_SIZE,
            bindings::COMPAT_ASHMEM_SET_PROT_MASK => bindings::ASHMEM_SET_PROT_MASK,
            _ => compat_cmd,
        };
        Self::ioctl(me, file, cmd, arg)
    }

    fn show_fdinfo(me: Pin<&Ashmem>, m: &SeqFile, _file: &File) {
        let asma = me.inner.lock();

        if let Some(file) = asma.file.as_ref() {
            seq_print!(m, "inode:\t{}\n", file.inode_ino());
        }
        if let Some(name) = asma.name.as_ref() {
            let name = core::str::from_utf8(name).unwrap_or("<invalid utf-8>");
            seq_print!(m, "name:\t{}\n", name);
        }
        seq_print!(m, "size\t{}\n", asma.size);
    }
}

impl Ashmem {
    fn set_name(&self, mut reader: UserSliceReader) -> Result<isize> {
        let mut local_name = [0u8; ASHMEM_NAME_LEN];
        reader.read_slice(&mut local_name)?;

        // Find the zero terminator. If the zero terminator is missing, the string is truncated to
        // `ASHMEM_NAME_LEN-1` so that `get_name` can return it and has enough space to add a zero
        // terminator.
        let len = local_name
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(local_name.len() - 1);

        let mut v = Vec::with_capacity(len, GFP_KERNEL)?;
        v.extend_from_slice(&local_name[..len], GFP_KERNEL)?;

        let mut asma = self.inner.lock();
        if asma.file.is_some() {
            return Err(EINVAL);
        }
        asma.name = Some(v);
        Ok(0)
    }

    fn get_name(&self, mut writer: UserSliceWriter) -> Result<isize> {
        let mut local_name = [0u8; ASHMEM_NAME_LEN];
        let asma = self.inner.lock();
        let name = asma.name.as_deref().unwrap_or(b"dev/ashmem");
        let len = name.len();
        let len_with_nul = len + 1;
        if local_name.len() <= len_with_nul {
            // This shouldn't happen in practice since `set_name` will refuse to store a string
            // that is too long.
            return Err(EINVAL);
        }
        local_name[..len].copy_from_slice(name);
        local_name[len_with_nul] = 0;
        drop(asma);

        writer.write_slice(&local_name[..len_with_nul])?;
        Ok(0)
    }

    fn set_size(&self, size: usize) -> Result<isize> {
        let mut asma = self.inner.lock();
        if asma.file.is_some() {
            return Err(EINVAL);
        }
        asma.size = size;
        Ok(0)
    }

    fn get_size(&self) -> Result<isize> {
        Ok(self.inner.lock().size as isize)
    }

    fn set_prot_mask(&self, mut prot: usize) -> Result<isize> {
        let mut asma = self.inner.lock();

        if (prot & PROT_READ != 0) && read_implies_exec(current!()) {
            prot |= PROT_EXEC;
        }

        prot |= asma.prot_mask & PROT_READ;
        prot |= asma.prot_mask & PROT_EXEC;

        // The user can only remove, not add, protection bits.
        if (asma.prot_mask & prot) != prot {
            return Err(EINVAL);
        }

        asma.prot_mask = prot;
        Ok(0)
    }

    fn get_prot_mask(&self) -> Result<isize> {
        Ok(self.inner.lock().prot_mask as isize)
    }

    fn get_file_id(&self, mut writer: UserSliceWriter) -> Result<isize> {
        let ino = {
            let asma = self.inner.lock();
            let Some(file) = asma.file.as_ref() else {
                return Err(EINVAL);
            };
            file.inode_ino()
        };
        writer.write(&ino)?;
        Ok(0)
    }

    fn purge_all_caches(&self) -> Result<isize> {
        if !has_cap_sys_admin() {
            return Err(EPERM);
        }
        Ok(0)
    }
}

impl AshmemInner {
    /// Get the full name.
    ///
    /// If the name is `Some(name)`, then this returns `dev/ashmem/name\0`.
    ///
    /// If the name is `None`, then this returns `dev/ashmem\0`.
    fn full_name<'name>(&self, name: &'name mut [u8; ASHMEM_FULL_NAME_LEN]) -> &'name CStr {
        name[..ASHMEM_NAME_PREFIX_LEN].copy_from_slice(&ASHMEM_NAME_PREFIX);
        if let Some(set_name) = self.name.as_deref() {
            name[ASHMEM_NAME_PREFIX_LEN..][..set_name.len()].copy_from_slice(set_name);
        } else {
            // Remove last slash if no name set.
            name[ASHMEM_NAME_PREFIX_LEN - 1] = 0;
        }
        name[ASHMEM_FULL_NAME_LEN - 1] = 0;

        // This unwrap only fails if there's no nul-byte, but we just added one at the end above.
        let len_with_nul = name
            .iter()
            .position(|&c| c == 0)
            .map(|len| len + 1)
            .unwrap();

        // This unwrap fails if the last byte is not a nul-byte, or if there are any nul-bytes
        // before the last byte. Neither of those are possible here since `len_with_nul` is the
        // index of the first nul-byte in `name`.
        CStr::from_bytes_with_nul(&name[..len_with_nul]).unwrap()
    }
}

#[no_mangle]
unsafe extern "C" fn ashmem_memfd_ioctl(file: *mut bindings::file, cmd: u32, arg: usize) -> isize {
    #[cfg(CONFIG_COMPAT)]
    let cmd = match cmd {
        bindings::COMPAT_ASHMEM_SET_SIZE => bindings::ASHMEM_SET_SIZE,
        bindings::COMPAT_ASHMEM_SET_PROT_MASK => bindings::ASHMEM_SET_PROT_MASK,
        cmd => cmd,
    };

    // SAFETY:
    // * The file is valid for the duration of this call.
    // * There is no active fdget_pos region on the file on this thread.
    let file = unsafe { File::from_raw_file(file) };

    match ashmem_memfd_ioctl_inner(file, cmd, arg) {
        Ok(ret) => ret,
        Err(err) => err.to_errno() as isize,
    }
}

/// This function implements the ioctls called when you attempt to use an ashmem ioctl on a file
/// created using memfd_create.
///
/// They do not apply when the file is created by directly opening /dev/ashmem.
fn ashmem_memfd_ioctl_inner(file: &File, cmd: u32, arg: usize) -> Result<isize> {
    use kernel::bindings::{F_ADD_SEALS, F_GET_SEALS, F_SEAL_FUTURE_WRITE, F_SEAL_WRITE};
    const WRITE_SEALS_MASK: u64 = (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE) as u64;

    /// # Safety
    /// The file must be a memfd file.
    unsafe fn get_seals(file: &File) -> Result<u64> {
        // SAFETY: This is a memfd file.
        let seals: i64 = unsafe { bindings::memfd_fcntl(file.as_ptr(), F_GET_SEALS, 0) };
        if seals < 0 {
            return Err(Error::from_errno(seals as i32));
        }
        Ok(seals as u64)
    }

    let size = _IOC_SIZE(cmd);
    match cmd {
        bindings::ASHMEM_GET_NAME => {
            let file_ptr = file.as_ptr();
            // SAFETY: It's safe to access a file's dentry.
            let dentry = unsafe { (*file_ptr).f_path.dentry };
            // SAFETY: memfd stores the supplied name at this location. A default value is stored
            // when no name is supplied, so this is always a valid string.
            let name = unsafe {
                core::slice::from_raw_parts(
                    (*dentry).d_name.name,
                    (*dentry).d_name.__bindgen_anon_1.__bindgen_anon_1.len as usize,
                )
            };
            let max = usize::min(name.len(), ASHMEM_NAME_LEN);

            let mut local_name = [0u8; ASHMEM_NAME_LEN];
            local_name[..max].copy_from_slice(&name[..max]);
            local_name[ASHMEM_NAME_LEN - 1] = 0;

            let mut writer = UserSlice::new(arg, size).writer();
            writer.write_slice(&local_name)?;
            Ok(0)
        }
        bindings::ASHMEM_GET_SIZE => {
            let file_ptr = file.as_ptr();
            // SAFETY: It's safe to access a file's inode.
            let inode = unsafe { (*file_ptr).f_inode };
            // SAFETY: It's safe to read the size of an inode.
            let size = unsafe { bindings::i_size_read(inode) };
            Ok(size as isize)
        }
        bindings::ASHMEM_SET_PROT_MASK => {
            // SAFETY: This is a memfd file.
            let seals = unsafe { get_seals(file) }?;
            let mut prot = arg;

            // The memfd compat layer does not support unsetting these.
            prot |= PROT_READ | PROT_EXEC;

            let is_writable = seals & WRITE_SEALS_MASK == 0;
            let should_be_writable = prot & PROT_WRITE != 0;

            if !is_writable && should_be_writable {
                // Can't add PROT bits.
                return Err(EINVAL);
            }

            if is_writable && !should_be_writable {
                // SAFETY: This is a memfd file.
                let ret = unsafe {
                    bindings::memfd_fcntl(file.as_ptr(), F_ADD_SEALS, F_SEAL_FUTURE_WRITE)
                };
                if ret < 0 {
                    return Err(Error::from_errno(ret as i32));
                }
            }
            Ok(0)
        }
        bindings::ASHMEM_GET_PROT_MASK => {
            // SAFETY: This is a memfd file.
            let seals = unsafe { get_seals(file) }?;

            let mut prot = PROT_READ | PROT_EXEC;
            if seals & WRITE_SEALS_MASK == 0 {
                prot |= PROT_WRITE;
            }
            Ok(prot as isize)
        }
        bindings::ASHMEM_GET_FILE_ID => {
            // SAFETY: Accessing the ino is always okay.
            let ino = unsafe { (*(*file.as_ptr()).f_inode).i_ino as usize };

            let mut writer = UserSlice::new(arg, size).writer();
            writer.write(&ino)?;
            Ok(0)
        }
        // Just ignore unpin requests.
        ASHMEM_PIN => Ok(bindings::ASHMEM_NOT_PURGED as isize),
        ASHMEM_UNPIN => Ok(0),
        ASHMEM_GET_PIN_STATUS => Ok(bindings::ASHMEM_IS_PINNED as isize),
        bindings::ASHMEM_PURGE_ALL_CACHES => {
            if !has_cap_sys_admin() {
                return Err(EPERM);
            }
            Ok(0)
        }
        // We do not need to implement SET_NAME or SET_SIZE. The ioctls in this function are only
        // called when you:
        //
        // 1. Think you have an ashmem fd.
        // 2. But actually have a memfd fd.
        //
        // This can only happen if you created the fd through the libcutils library, and that
        // library sets the name and size in the fd constructor where it knows whether ashmem or
        // memfd is used, so we should never end up here.
        bindings::ASHMEM_SET_NAME => Err(EINVAL),
        bindings::ASHMEM_SET_SIZE => Err(EINVAL),
        _ => Err(EINVAL),
    }
}
