// SPDX-License-Identifier: GPL-2.0-only
//! Kernel module exposing an IOCTL interface to userspace for loading and managing
//! BPF file descriptors.

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
    transmute::{AsBytes, FromBytes},
    uaccess::{UserPtr, UserSlice},
};

const BPF_FD_LOADER_MAGIC: u32 = b'B' as u32;

#[repr(C, align(8))]
#[derive(Copy, Clone, Default)]
struct BpfFdLoaderArg {
    loader_fd: u32,
    ctx: u64,
    ctx_size: u32,
}

// SAFETY: `BpfFdLoaderArg` is `repr(C, align(8))` and all byte patterns are
// valid for integer fields.
unsafe impl FromBytes for BpfFdLoaderArg {}
// SAFETY: `BpfFdLoaderArg` contains only primitive integer types with
// initialized padding when Default-constructed/read.
unsafe impl AsBytes for BpfFdLoaderArg {}

const BPF_FD_LOADER_IOCTL_PING: u32 = _IO(BPF_FD_LOADER_MAGIC, 1);
const BPF_FD_LOADER_IOCTL_RECEIVE_FD: u32 = _IOWR::<BpfFdLoaderArg>(BPF_FD_LOADER_MAGIC, 2);

const BPF_LOADER_PROG_SEC: &[u8] = b"__loader.prog";
const BPF_LOADER_MAP_SEC: &[u8] = b"__loader.map";
const BPF_LOADER_LICENSE_SEC: &[u8] = b"license";
const BPF_LOADER_MAX_SIZE: usize = 8 << 20; // 8MB

module! {
    type: BpfFdLoaderModule,
    name: "bpf_fd_loader",
    authors: [
        "Thiebaud Weksteen <tweek@google.com>",
        "Siddharth Nayyar <sidnayyar@google.com>",
    ],
    description: "BPF FD Loader Kernel Module with IOCTL Interface",
    license: "GPL",
    imports_ns: [
        "BPF_INTERNAL",
        "ANDROID_GKI_VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver",
    ],
}

#[pin_data(PinnedDrop)]
struct BpfFdLoaderModule {
    #[pin]
    _miscdev: MiscDeviceRegistration<BpfFdLoaderDevice>,
}

impl kernel::InPlaceModule for BpfFdLoaderModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        let options = MiscDeviceOptions {
            name: c"bpf_fd_loader",
        };

        pr_info!("bpf_fd_loader: module loaded, device node /dev/bpf_fd_loader created\n");

        try_pin_init!(Self {
            _miscdev <- MiscDeviceRegistration::register(options),
        })
    }
}

#[pinned_drop]
impl PinnedDrop for BpfFdLoaderModule {
    fn drop(self: Pin<&mut Self>) {
        pr_info!("bpf_fd_loader: module unloaded\n");
    }
}

#[pin_data(PinnedDrop)]
struct BpfFdLoaderDevice;

#[pinned_drop]
impl PinnedDrop for BpfFdLoaderDevice {
    fn drop(self: Pin<&mut Self>) {
        pr_info!("bpf_fd_loader: device closed\n");
    }
}

#[vtable]
impl MiscDevice for BpfFdLoaderDevice {
    type Ptr = Pin<KBox<Self>>;

    fn open(_file: &File, _misc: &MiscDeviceRegistration<Self>) -> Result<Pin<KBox<Self>>> {
        pr_info!("bpf_fd_loader: device opened\n");
        Ok(KBox::pin_init(pin_init!(BpfFdLoaderDevice {}), GFP_KERNEL)?)
    }

    fn ioctl(_me: Pin<&Self>, _file: &File, cmd: u32, arg: usize) -> Result<isize> {
        match cmd {
            BPF_FD_LOADER_IOCTL_PING => {
                pr_info!("bpf_fd_loader: IOCTL PING received\n");
                Ok(0)
            }
            BPF_FD_LOADER_IOCTL_RECEIVE_FD => {
                let (mut reader, mut writer) =
                    UserSlice::new(UserPtr::from_addr(arg), size_of::<BpfFdLoaderArg>())
                        .reader_writer();
                let loader_arg: BpfFdLoaderArg = reader.read()?;

                pr_info!(
                    "bpf_fd_loader: received loader_fd = {}, ctx_size = {}\n",
                    loader_arg.loader_fd,
                    loader_arg.ctx_size
                );

                loader_load_fd(&loader_arg)?;

                writer.write(&loader_arg)?;
                Ok(0)
            }
            _ => {
                pr_warn!("bpf_fd_loader: unknown IOCTL cmd 0x{:x}\n", cmd);
                Err(EINVAL)
            }
        }
    }
}

struct VmallocBuf {
    ptr: *mut ffi::c_void,
    len: usize,
}

impl VmallocBuf {
    fn as_slice(&self) -> &[u8] {
        if self.ptr.is_null() || self.len == 0 {
            &[]
        } else {
            // SAFETY: `ptr` is a valid vmalloc buffer of `len` bytes allocated
            // by `kernel_read_file`.
            unsafe { core::slice::from_raw_parts(self.ptr as *const u8, self.len) }
        }
    }
}

impl Drop for VmallocBuf {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            // SAFETY: `ptr` was allocated via vmalloc in `kernel_read_file`.
            unsafe { bindings::vfree(self.ptr) };
        }
    }
}

struct OwnedFd(ffi::c_int);

impl Drop for OwnedFd {
    fn drop(&mut self) {
        // SAFETY: `self.0` is a valid open file descriptor created by `kern_sys_bpf`.
        unsafe { bindings::close_fd(self.0 as u32) };
    }
}

struct ElfInfo<'a> {
    buf: &'a [u8],
    sechdrs: &'a [bindings::Elf64_Shdr],
    secstrings: &'a [u8],
}

impl<'a> ElfInfo<'a> {
    fn parse(buf: &'a [u8]) -> Result<Self> {
        if buf.len() < size_of::<bindings::Elf64_Ehdr>() {
            pr_err!("Invalid ELF header len {}\n", buf.len());
            return Err(ENOEXEC);
        }

        // SAFETY: `buf` is at least `size_of::<Elf64_Ehdr>()` bytes and page-aligned from vmalloc.
        let hdr = unsafe { &*(buf.as_ptr() as *const bindings::Elf64_Ehdr) };

        if &hdr.e_ident[..bindings::SELFMAG as usize]
            != &bindings::ELFMAG[..bindings::SELFMAG as usize]
        {
            pr_err!("Invalid ELF header magic\n");
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
        if hdr.e_shentsize as usize != size_of::<bindings::Elf64_Shdr>() {
            pr_err!("Invalid ELF section header size\n");
            return Err(ENOEXEC);
        }

        let shoff = hdr.e_shoff as usize;
        let shnum = hdr.e_shnum as usize;
        let sh_table_bytes = shnum
            .checked_mul(size_of::<bindings::Elf64_Shdr>())
            .ok_or(ENOEXEC)?;

        if shoff >= buf.len() || sh_table_bytes > buf.len() - shoff {
            pr_err!("Invalid ELF section header overflow\n");
            return Err(ENOEXEC);
        }

        // SAFETY: Bounds of section header table were validated above.
        let sechdrs = unsafe {
            core::slice::from_raw_parts(
                buf.as_ptr().add(shoff) as *const bindings::Elf64_Shdr,
                shnum,
            )
        };

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
        let secstrings = &buf[str_off..str_off + str_sz];
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

    fn find_section(&self, sect_name: &[u8]) -> Result<&'a [u8]> {
        for shdr in self.sechdrs.iter().skip(1) {
            if shdr.sh_type == bindings::SHT_NULL || shdr.sh_type == bindings::SHT_NOBITS {
                continue;
            }
            let name_start = shdr.sh_name as usize;
            let name_slice = &self.secstrings[name_start..];
            if let Some(nul_pos) = name_slice.iter().position(|&b| b == 0) {
                if &name_slice[..nul_pos] == sect_name {
                    let off = shdr.sh_offset as usize;
                    let sz = shdr.sh_size as usize;
                    return Ok(&self.buf[off..off + sz]);
                }
            }
        }
        Err(EINVAL)
    }
}

fn loader_load_fd(args: &BpfFdLoaderArg) -> Result {
    if args.ctx_size > u16::MAX as u32 {
        return Err(EINVAL);
    }

    let file = LocalFile::fget(args.loader_fd).map_err(|_| EINVAL)?;

    let mut raw_buf: *mut ffi::c_void = core::ptr::null_mut();
    // SAFETY: `file.as_ptr()` is a valid pointer to `struct file`, and
    // `raw_buf` will be populated on success.
    let len = unsafe {
        bindings::kernel_read_file(
            file.as_ptr(),
            0,
            &mut raw_buf,
            BPF_LOADER_MAX_SIZE,
            core::ptr::null_mut(),
            bindings::kernel_read_file_id_READING_MODULE,
        )
    };
    if len < 0 {
        return Err(Error::from_errno(len as ffi::c_int));
    }
    let vm_buf = VmallocBuf {
        ptr: raw_buf,
        len: len as usize,
    };

    let elf_info = ElfInfo::parse(vm_buf.as_slice())?;

    let insns = elf_info.find_section(BPF_LOADER_PROG_SEC)?;
    let data = elf_info.find_section(BPF_LOADER_MAP_SEC)?;
    let license = elf_info.find_section(BPF_LOADER_LICENSE_SEC)?;

    if license.is_empty() || license[license.len() - 1] != 0 {
        pr_err!("ELF Spec violation: license section isn't null terminated\n");
        return Err(ENOEXEC);
    }

    // 1. Create array map
    extern "C" {
        fn kern_sys_bpf(
            cmd: ffi::c_int,
            attr: *mut bindings::bpf_attr,
            size: ffi::c_uint,
        ) -> ffi::c_int;
    }

    let mut sattr: bindings::bpf_attr = unsafe { core::mem::zeroed() };
    sattr.__bindgen_anon_1.map_type = bindings::bpf_map_type_BPF_MAP_TYPE_ARRAY;
    sattr.__bindgen_anon_1.key_size = size_of::<u32>() as u32;
    sattr.__bindgen_anon_1.value_size = data.len() as u32;
    sattr.__bindgen_anon_1.max_entries = 1;

    // SAFETY: Calling kernel internal BPF syscall helper with valid `bpf_attr`.
    let map_fd = unsafe {
        kern_sys_bpf(
            bindings::bpf_cmd_BPF_MAP_CREATE as ffi::c_int,
            &mut sattr,
            size_of::<bindings::bpf_attr>() as u32,
        )
    };
    if map_fd < 0 {
        return Err(Error::from_errno(map_fd));
    }
    let map_fd = OwnedFd(map_fd);

    // 2. Populate map with loader data
    let zero: u32 = 0;
    let mut sattr: bindings::bpf_attr = unsafe { core::mem::zeroed() };
    sattr.__bindgen_anon_2.map_fd = map_fd.0 as u32;
    sattr.__bindgen_anon_2.key = &zero as *const u32 as u64;
    sattr.__bindgen_anon_2.__bindgen_anon_1.value = data.as_ptr() as u64;

    // SAFETY: Calling kernel internal BPF syscall helper with valid `bpf_attr`.
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

    // 3. Load loader program
    let mut sattr: bindings::bpf_attr = unsafe { core::mem::zeroed() };
    sattr.__bindgen_anon_3.prog_type = bindings::bpf_prog_type_BPF_PROG_TYPE_SYSCALL;
    sattr.__bindgen_anon_3.license = license.as_ptr() as u64;
    sattr.__bindgen_anon_3.insns = insns.as_ptr() as u64;
    sattr.__bindgen_anon_3.insn_cnt = (insns.len() / size_of::<bindings::bpf_insn>()) as u32;
    let map_fd_val = map_fd.0;
    sattr.__bindgen_anon_3.fd_array = &map_fd_val as *const ffi::c_int as u64;
    sattr.__bindgen_anon_3.prog_flags = bindings::BPF_F_SLEEPABLE;
    // SAFETY: Accessing zero-initialized union field `__bindgen_anon_3`.
    unsafe {
        let copy_len = core::cmp::min(
            BPF_LOADER_PROG_SEC.len(),
            sattr.__bindgen_anon_3.prog_name.len() - 1,
        );
        for (dst, &src) in sattr.__bindgen_anon_3.prog_name[..copy_len]
            .iter_mut()
            .zip(&BPF_LOADER_PROG_SEC[..copy_len])
        {
            *dst = src as ffi::c_char;
        }
    }

    // SAFETY: Calling kernel internal BPF syscall helper with valid `bpf_attr`.
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
    let prog_fd = OwnedFd(prog_fd);

    // 4. Execute loader program
    let ctx_sz = args.ctx_size as usize;
    let mut kctx = KVec::<u8>::new();
    kctx.resize(ctx_sz, 0, GFP_KERNEL)?;

    let mut sattr: bindings::bpf_attr = unsafe { core::mem::zeroed() };
    sattr.test.prog_fd = prog_fd.0 as u32;
    sattr.test.ctx_in = kctx.as_mut_ptr() as u64;
    sattr.test.ctx_size_in = ctx_sz as u32;

    // SAFETY: Calling kernel internal BPF syscall helper with valid `bpf_attr`.
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
    // SAFETY: Reading union field `test.retval` written by `kern_sys_bpf`.
    let retval = unsafe { sattr.test.retval } as i32;
    if retval < 0 {
        return Err(Error::from_errno(retval));
    }

    UserSlice::new(UserPtr::from_addr(args.ctx as usize), ctx_sz)
        .writer()
        .write_slice(&kctx)?;

    Ok(())
}
