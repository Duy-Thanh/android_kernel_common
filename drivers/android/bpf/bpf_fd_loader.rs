// SPDX-License-Identifier: GPL-2.0-only
//! Kernel module exposing an IOCTL interface to userspace for loading and executing
//! BPF loader programs from an ELF file descriptor.

use core::mem::size_of;
use core::pin::Pin;
use kernel::{
    alloc::kvec::KVec,
    bindings,
    error::Error,
    ffi,
    fs::{File, LocalFile},
    ioctl::{_IO, _IOWR},
    miscdevice::{MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    prelude::*,
    sizes::SZ_8M,
    uaccess::{UserPtr, UserSlice},
};
use zerocopy::{FromBytes, IntoBytes};
use zerocopy_derive::{FromBytes, Immutable, IntoBytes, KnownLayout};

const ANDROID_BPF_FD_LOADER_MAGIC: u32 = b'B' as u32;

/// IOCTL argument structure matching `struct android_bpf_fd_loader_arg` in
/// `<uapi/linux/android/bpf_fd_loader.h>`.
#[repr(C, align(8))]
#[derive(Copy, Clone, Default, FromBytes, IntoBytes, Immutable, KnownLayout)]
struct AndroidBpfFdLoaderArg {
    /// File descriptor to an open ELF file containing the loader program and data.
    loader_fd: u32,
    /// Size of the context passed to the loader program.
    ctx_size: u32,
    /// Userspace pointer to the context passed to BPF_PROG_TEST_RUN.
    ctx: u64,
}

const ANDROID_BPF_FD_LOADER_IOCTL_PING: u32 = _IO(ANDROID_BPF_FD_LOADER_MAGIC, 1);
const ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD: u32 =
    _IOWR::<AndroidBpfFdLoaderArg>(ANDROID_BPF_FD_LOADER_MAGIC, 2);

const DEVICE_NAME: &CStr = c"android_bpf_loader";

const BPF_LOADER_PROG_SEC: &CStr = c"__loader.prog";
const BPF_LOADER_MAP_SEC: &CStr = c"__loader.map";
const BPF_LOADER_LICENSE_SEC: &CStr = c"license";
const BPF_LOADER_MAX_SIZE: usize = SZ_8M;

module! {
    type: BpfFdLoaderModule,
    name: "bpf_fd_loader",
    authors: [
        "Thiebaud Weksteen <tweek@google.com>",
        "Siddharth Nayyar <sidnayyar@google.com>",
    ],
    description: "Android BPF FD Loader Driver",
    license: "GPL",
    imports_ns: [
        "BPF_INTERNAL",
    ],
}

#[pin_data(PinnedDrop)]
struct BpfFdLoaderModule {
    #[pin]
    _miscdev: MiscDeviceRegistration<BpfFdLoaderDevice>,
}

impl kernel::InPlaceModule for BpfFdLoaderModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        let options = MiscDeviceOptions { name: DEVICE_NAME };

        pr_info!("module loaded, device node /dev/{DEVICE_NAME} created\n");

        try_pin_init!(Self {
            _miscdev <- MiscDeviceRegistration::register(options),
        })
    }
}

#[pinned_drop]
impl PinnedDrop for BpfFdLoaderModule {
    fn drop(self: Pin<&mut Self>) {
        pr_info!("module unloaded\n");
    }
}

struct BpfFdLoaderDevice;

#[vtable]
impl MiscDevice for BpfFdLoaderDevice {
    type Ptr = ();

    fn open(_file: &File, _misc: &MiscDeviceRegistration<Self>) -> Result<()> {
        pr_info!("device opened\n");
        Ok(())
    }

    fn release(_device: (), _file: &File) {
        pr_info!("device closed\n");
    }

    fn ioctl(_device: (), _file: &File, cmd: u32, arg: usize) -> Result<isize> {
        match cmd {
            ANDROID_BPF_FD_LOADER_IOCTL_PING => {
                pr_info!("IOCTL PING received\n");
                Ok(0)
            }
            ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD => {
                let (mut reader, mut writer) =
                    UserSlice::new(UserPtr::from_addr(arg), size_of::<AndroidBpfFdLoaderArg>())
                        .reader_writer();
                let mut loader_arg = AndroidBpfFdLoaderArg::default();
                reader.read_slice(loader_arg.as_mut_bytes())?;

                pr_info!(
                    "received loader_fd = {}, ctx_size = {}\n",
                    loader_arg.loader_fd,
                    loader_arg.ctx_size
                );

                loader_load_fd(&loader_arg)?;

                writer.write_slice(loader_arg.as_bytes())?;
                Ok(0)
            }
            _ => {
                pr_warn!("unknown IOCTL cmd 0x{:x}\n", cmd);
                Err(EINVAL)
            }
        }
    }
}

/// Reads the contents of a file up to `max_size` bytes into a `VBox<[u8]>`.
fn read_kernel_file(file: &LocalFile, max_size: usize) -> Result<VBox<[u8]>> {
    let mut raw_buf = core::ptr::null_mut();
    // SAFETY: `file.as_ptr()` points to a valid open `struct file`.
    // `kernel_read_file` allocates memory via vmalloc and writes the pointer into `raw_buf`.
    let len = unsafe {
        bindings::kernel_read_file(
            file.as_ptr(),
            0,
            &mut raw_buf,
            max_size,
            core::ptr::null_mut(),
            bindings::kernel_read_file_id_READING_MODULE,
        )
    };
    if len < 0 {
        return Err(Error::from_errno(len as ffi::c_int));
    }
    if len == 0 || raw_buf.is_null() {
        return Err(ENOEXEC);
    }
    let slice = core::ptr::slice_from_raw_parts_mut(raw_buf.cast::<u8>(), len as usize);
    // SAFETY: `raw_buf` points to memory allocated by `kernel_read_file` with `vmalloc` (represented
    // by `Vmalloc`), is properly aligned for `u8`, and is valid for `len` elements.
    // Ownership of the allocation is transferred to the `VBox`.
    Ok(unsafe { VBox::from_raw(slice) })
}

/// An RAII guard for a task-local file descriptor allocated by `kern_sys_bpf`.
///
/// Automatically closes the file descriptor in `current->files` using `close_fd` upon drop.
struct OwnedFd {
    fd: ffi::c_int,
    _not_send: kernel::types::NotThreadSafe,
}

impl OwnedFd {
    fn new(fd: ffi::c_int) -> Self {
        Self {
            fd,
            _not_send: kernel::types::NotThreadSafe,
        }
    }

    fn raw_fd(&self) -> ffi::c_int {
        self.fd
    }
}

impl Drop for OwnedFd {
    fn drop(&mut self) {
        // SAFETY: `self.fd` is a valid file descriptor returned by `kern_sys_bpf`.
        unsafe { bindings::close_fd(self.fd as u32) };
    }
}

#[repr(C)]
#[derive(Copy, Clone, FromBytes, IntoBytes, Immutable, KnownLayout)]
struct Elf64Ehdr {
    e_ident: [u8; 16],
    e_type: u16,
    e_machine: u16,
    e_version: u32,
    e_entry: u64,
    e_phoff: u64,
    e_shoff: u64,
    e_flags: u32,
    e_ehsize: u16,
    e_phentsize: u16,
    e_phnum: u16,
    e_shentsize: u16,
    e_shnum: u16,
    e_shstrndx: u16,
}

#[repr(C)]
#[derive(Copy, Clone, FromBytes, IntoBytes, Immutable, KnownLayout)]
struct Elf64Shdr {
    sh_name: u32,
    sh_type: u32,
    sh_flags: u64,
    sh_addr: u64,
    sh_offset: u64,
    sh_size: u64,
    sh_link: u32,
    sh_info: u32,
    sh_addralign: u64,
    sh_entsize: u64,
}

struct ElfInfo<'a> {
    buf: &'a [u8],
    sechdrs: &'a [Elf64Shdr],
    secstrings: &'a [u8],
}

impl<'a> ElfInfo<'a> {
    fn parse(buf: &'a [u8]) -> Result<Self> {
        let (hdr, _) = Elf64Ehdr::ref_from_prefix(buf).map_err(|_| {
            pr_err!("Invalid ELF header len {}\n", buf.len());
            ENOEXEC
        })?;

        if &hdr.e_ident[..4] != b"\x7fELF" {
            pr_err!("Invalid ELF header magic: != \\177ELF\n");
            return Err(ENOEXEC);
        }
        if hdr.e_ident[bindings::EI_CLASS as usize] != bindings::ELFCLASS64 as u8 {
            pr_err!("Only 64-bit ELF is supported\n");
            return Err(ENOEXEC);
        }
        if hdr.e_type != bindings::ET_REL as u16 {
            pr_err!(
                "Invalid ELF header type: {} != {}\n",
                hdr.e_type,
                bindings::ET_REL
            );
            return Err(ENOEXEC);
        }
        if hdr.e_machine != bindings::EM_BPF as u16 {
            pr_err!(
                "Invalid ELF machine type: {} != {}\n",
                hdr.e_machine,
                bindings::EM_BPF
            );
            return Err(ENOEXEC);
        }
        if hdr.e_shentsize as usize != size_of::<Elf64Shdr>() {
            pr_err!("Invalid ELF section header size\n");
            return Err(ENOEXEC);
        }

        let shoff = usize::try_from(hdr.e_shoff).map_err(|_| ENOEXEC)?;
        let shnum = hdr.e_shnum as usize;
        let sh_table_bytes = shnum.checked_mul(size_of::<Elf64Shdr>()).ok_or(ENOEXEC)?;

        if shoff >= buf.len() || sh_table_bytes > buf.len() - shoff {
            pr_err!("Invalid ELF section header overflow\n");
            return Err(ENOEXEC);
        }

        let sh_slice = &buf[shoff..shoff + sh_table_bytes];
        let (sechdrs, _) =
            <[Elf64Shdr]>::ref_from_prefix_with_elems(sh_slice, shnum).map_err(|_| {
                pr_err!("Invalid ELF section header alignment or size\n");
                ENOEXEC
            })?;

        if sechdrs.is_empty()
            || sechdrs[0].sh_type != bindings::SHT_NULL
            || sechdrs[0].sh_size != 0
            || sechdrs[0].sh_addr != 0
        {
            pr_err!(
                "ELF Spec violation: section 0 type({})!=SH_NULL or non-zero len or addr\n",
                sechdrs.first().map_or(0, |s| s.sh_type)
            );
            return Err(ENOEXEC);
        }

        for (i, shdr) in sechdrs.iter().enumerate().skip(1) {
            match shdr.sh_type {
                bindings::SHT_NULL | bindings::SHT_NOBITS => continue,
                _ => {
                    let secend = shdr.sh_offset.checked_add(shdr.sh_size).ok_or(ENOEXEC)?;
                    if secend > buf.len() as u64 {
                        pr_err!(
                            "Invalid ELF section in BPF loader (section {} type {})\n",
                            i,
                            shdr.sh_type
                        );
                        return Err(ENOEXEC);
                    }
                }
            }
        }

        let shstrndx = hdr.e_shstrndx as usize;
        if shstrndx == bindings::SHN_UNDEF as usize || shstrndx >= shnum {
            pr_err!(
                "Invalid ELF section name index: {} || e_shstrndx ({}) >= e_shnum ({})\n",
                shstrndx,
                shstrndx,
                shnum
            );
            return Err(ENOEXEC);
        }

        let strhdr = &sechdrs[shstrndx];
        if strhdr.sh_type != bindings::SHT_STRTAB {
            pr_err!("Invalid ELF section name table type: {}\n", strhdr.sh_type);
            return Err(ENOEXEC);
        }
        if strhdr.sh_size == 0 {
            pr_err!("empty section name table\n");
            return Err(ENOEXEC);
        }

        let str_off = strhdr.sh_offset as usize;
        let str_sz = strhdr.sh_size as usize;
        let str_end = str_off.checked_add(str_sz).ok_or(ENOEXEC)?;
        let secstrings = buf.get(str_off..str_end).ok_or(ENOEXEC)?;
        if secstrings[str_sz - 1] != 0 {
            pr_err!("ELF Spec violation: section name table isn't null terminated\n");
            return Err(ENOEXEC);
        }

        for (i, shdr) in sechdrs.iter().enumerate() {
            if shdr.sh_type == bindings::SHT_NULL {
                continue;
            }
            if (shdr.sh_name as usize) >= str_sz {
                pr_err!(
                    "Invalid ELF section name in BPF loader (section {} type {})\n",
                    i,
                    shdr.sh_type
                );
                return Err(ENOEXEC);
            }
        }

        Ok(Self {
            buf,
            sechdrs,
            secstrings,
        })
    }

    fn find_section(&self, sect_name: &CStr) -> Result<&'a [u8]> {
        for shdr in self.sechdrs.iter().skip(1) {
            if shdr.sh_type == bindings::SHT_NULL || shdr.sh_type == bindings::SHT_NOBITS {
                continue;
            }
            let name_start = shdr.sh_name as usize;
            if let Ok(name) = CStr::from_bytes_until_nul(&self.secstrings[name_start..]) {
                if name == sect_name {
                    let off = usize::try_from(shdr.sh_offset).map_err(|_| ENOEXEC)?;
                    let sz = usize::try_from(shdr.sh_size).map_err(|_| ENOEXEC)?;
                    let end = off.checked_add(sz).ok_or(ENOEXEC)?;
                    let slice = self.buf.get(off..end).ok_or(ENOEXEC)?;
                    return Ok(slice);
                }
            }
        }
        Err(EINVAL)
    }
}

fn bpf_capable() -> bool {
    // SAFETY: `capable` is safe to call from process context.
    unsafe {
        bindings::capable(bindings::CAP_BPF as ffi::c_int)
            || bindings::capable(bindings::CAP_SYS_ADMIN as ffi::c_int)
    }
}

// kern_sys_bpf is not defined in a header file. It is also declared inline in kernel/bpf/syscall.c.
extern "C" {
    fn kern_sys_bpf(
        cmd: ffi::c_int,
        attr: *mut bindings::bpf_attr,
        size: ffi::c_uint,
    ) -> ffi::c_int;
}

fn bpf_create_array_map(value_size: u32) -> Result<OwnedFd> {
    let mut sattr = bindings::bpf_attr::default();
    sattr.__bindgen_anon_1.map_type = bindings::bpf_map_type_BPF_MAP_TYPE_ARRAY;
    sattr.__bindgen_anon_1.key_size = size_of::<u32>() as u32;
    sattr.__bindgen_anon_1.value_size = value_size;
    sattr.__bindgen_anon_1.max_entries = 1;

    // SAFETY: Calling kern_sys_bpf with BPF_MAP_CREATE and a valid bpf_attr pointer.
    let fd = unsafe {
        kern_sys_bpf(
            bindings::bpf_cmd_BPF_MAP_CREATE as ffi::c_int,
            &mut sattr,
            size_of::<bindings::bpf_attr>() as u32,
        )
    };
    if fd < 0 {
        return Err(Error::from_errno(fd));
    }
    Ok(OwnedFd::new(fd))
}

fn bpf_update_map_elem(map_fd: &OwnedFd, key: &u32, data: &[u8]) -> Result {
    let mut sattr = bindings::bpf_attr::default();
    sattr.__bindgen_anon_2.map_fd = map_fd.raw_fd() as u32;
    sattr.__bindgen_anon_2.key = (key as *const u32 as usize) as u64;
    sattr.__bindgen_anon_2.__bindgen_anon_1.value = (data.as_ptr() as usize) as u64;

    // SAFETY: Calling kern_sys_bpf with BPF_MAP_UPDATE_ELEM and a valid bpf_attr pointer.
    let err = unsafe {
        kern_sys_bpf(
            bindings::bpf_cmd_BPF_MAP_UPDATE_ELEM as ffi::c_int,
            &mut sattr,
            size_of::<bindings::bpf_attr>() as u32,
        )
    };
    if err < 0 {
        return Err(Error::from_errno(err));
    }
    Ok(())
}

fn bpf_load_syscall_prog(
    license: &[u8],
    insns: &[u8],
    fd_array: &[ffi::c_int],
    prog_name: &CStr,
) -> Result<OwnedFd> {
    let mut sattr = bindings::bpf_attr::default();
    sattr.__bindgen_anon_3.prog_type = bindings::bpf_prog_type_BPF_PROG_TYPE_SYSCALL;
    sattr.__bindgen_anon_3.license = (license.as_ptr() as usize) as u64;
    sattr.__bindgen_anon_3.insns = (insns.as_ptr() as usize) as u64;
    sattr.__bindgen_anon_3.insn_cnt = (insns.len() / size_of::<bindings::bpf_insn>()) as u32;
    sattr.__bindgen_anon_3.fd_array = (fd_array.as_ptr() as usize) as u64;
    sattr.__bindgen_anon_3.prog_flags = bindings::BPF_F_SLEEPABLE;
    // Copy prog_name into a zero-initialized buffer, guaranteeing NUL-termination.
    let mut name_buf = [0; 16];
    let bytes = prog_name.to_bytes();
    let len = bytes.len().min(name_buf.len() - 1);
    name_buf[..len].copy_from_slice(&bytes[..len]);
    sattr.__bindgen_anon_3.prog_name = name_buf;

    // SAFETY: Calling kern_sys_bpf with BPF_PROG_LOAD and a valid bpf_attr pointer.
    let prog_fd = unsafe {
        kern_sys_bpf(
            bindings::bpf_cmd_BPF_PROG_LOAD as ffi::c_int,
            &mut sattr,
            size_of::<bindings::bpf_attr>() as u32,
        )
    };
    if prog_fd < 0 {
        return Err(Error::from_errno(prog_fd));
    }
    Ok(OwnedFd::new(prog_fd))
}

fn bpf_test_run(prog_fd: &OwnedFd, ctx: &mut [u8]) -> Result {
    let mut sattr = bindings::bpf_attr::default();
    sattr.test.prog_fd = prog_fd.raw_fd() as u32;
    sattr.test.ctx_in = (ctx.as_mut_ptr() as usize) as u64;
    sattr.test.ctx_size_in = ctx.len() as u32;

    // SAFETY: Calling kern_sys_bpf with BPF_PROG_TEST_RUN and a valid bpf_attr pointer.
    let err = unsafe {
        kern_sys_bpf(
            bindings::bpf_cmd_BPF_PROG_TEST_RUN as ffi::c_int,
            &mut sattr,
            size_of::<bindings::bpf_attr>() as u32,
        )
    };
    if err < 0 {
        return Err(Error::from_errno(err));
    }

    // SAFETY: sattr.test was written by kern_sys_bpf; reading retval from the test union.
    let retval = unsafe { sattr.test.retval } as i32;
    if retval < 0 {
        return Err(Error::from_errno(retval));
    }
    Ok(())
}

fn loader_load_fd(args: &AndroidBpfFdLoaderArg) -> Result {
    if !bpf_capable() {
        return Err(EPERM);
    }

    if args.ctx_size == 0 || args.ctx_size > u16::MAX as u32 {
        return Err(EINVAL);
    }

    let file = LocalFile::fget(args.loader_fd).map_err(|_| EINVAL)?;
    let vm_buf = read_kernel_file(&file, BPF_LOADER_MAX_SIZE)?;
    let elf_info = ElfInfo::parse(&vm_buf)?;

    let insns = elf_info.find_section(BPF_LOADER_PROG_SEC)?;
    if insns.is_empty() || (insns.len() % size_of::<bindings::bpf_insn>()) != 0 {
        pr_err!("ELF Spec violation: invalid instruction section size\n");
        return Err(ENOEXEC);
    }

    let data = elf_info.find_section(BPF_LOADER_MAP_SEC)?;
    if data.is_empty() {
        pr_err!("ELF Spec violation: invalid map section size\n");
        return Err(ENOEXEC);
    }

    let license = elf_info.find_section(BPF_LOADER_LICENSE_SEC)?;
    if license.is_empty() || license[license.len() - 1] != 0 {
        pr_err!("ELF Spec violation: license section isn't null terminated\n");
        return Err(ENOEXEC);
    }

    // 1. Create array map
    let map_fd = bpf_create_array_map(data.len() as u32)?;

    // 2. Populate map with loader data
    bpf_update_map_elem(&map_fd, &0, data)?;

    // 3. Load loader program
    let fd_array = [map_fd.raw_fd()];
    let prog_fd = bpf_load_syscall_prog(license, insns, &fd_array, BPF_LOADER_PROG_SEC)?;

    // 4. Copy context from userspace, run loader program, and copy updated context back
    let ctx_sz = args.ctx_size as usize;
    let ctx_addr = usize::try_from(args.ctx).map_err(|_| EINVAL)?;
    let (ureader, mut uwriter) =
        UserSlice::new(UserPtr::from_addr(ctx_addr), ctx_sz).reader_writer();
    let mut kctx = KVec::<u8>::new();
    ureader.read_all(&mut kctx, GFP_KERNEL)?;

    bpf_test_run(&prog_fd, &mut kctx)?;

    uwriter.write_slice(&kctx)?;

    Ok(())
}
