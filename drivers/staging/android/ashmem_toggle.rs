// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Provides knobs for Rust ashmem.

use crate::{ashmem_range, AshmemGlobals};
use core::{marker::PhantomData, sync::atomic::Ordering};
use kernel::{
    c_str,
    error::to_result,
    fs::File,
    miscdevice::{IovIter, Kiocb, MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    prelude::*,
    sync::Arc,
};

fn kstrtobool(kstr: &CStr) -> Result<bool> {
    let mut res = false;
    to_result(unsafe { kernel::bindings::kstrtobool(kstr.as_char_ptr(), &mut res) })?;
    Ok(res)
}

pub(crate) trait AshmemToggle: Sync + 'static {
    const NAME: &'static CStr;
    fn set(globals: &AshmemGlobals, enabled: bool) -> Result<()>;
    fn get(globals: &AshmemGlobals) -> bool;
}

pub(crate) struct AshmemToggleMisc<T>(Arc<AshmemGlobals>, PhantomData<T>);

impl<T: AshmemToggle> AshmemToggleMisc<T> {
    pub(crate) fn new(
        globals: Arc<AshmemGlobals>,
    ) -> Result<Pin<KBox<MiscDeviceRegistration<AshmemToggleMisc<T>>>>> {
        KBox::pin_init(
            MiscDeviceRegistration::register(
                MiscDeviceOptions { name: T::NAME },
                AshmemToggleMisc(globals, PhantomData),
            ),
            GFP_KERNEL,
        )
    }
}

#[vtable]
impl<T: AshmemToggle> MiscDevice for AshmemToggleMisc<T> {
    type File = Arc<AshmemGlobals>;
    fn open(_: &File, misc: &MiscDeviceRegistration<Self>) -> Result<Self::File> {
        Ok(misc.0.clone())
    }
    fn read_iter(mut kiocb: Kiocb<'_, Self::File>, iov: &mut IovIter) -> Result<usize> {
        if kiocb.ki_pos() != 0 {
            return Ok(0);
        }

        let data = match T::get(&kiocb.private_data()) {
            false => b"0\n",
            true => b"1\n",
        };

        // You better give me a buffer with space for at least two bytes.
        iov.copy_to_iter(data)?;
        *kiocb.ki_pos_mut() = 2;
        Ok(2)
    }
    fn write_iter(kiocb: Kiocb<'_, Self::File>, iov: &mut IovIter) -> Result<usize> {
        let mut data = [0; 16];
        let len = iov.copy_from_iter(&mut data[..15]);
        data[len] = 0;
        let data = CStr::from_bytes_with_nul(&data[..len + 1])?;
        T::set(&kiocb.private_data(), kstrtobool(data)?)?;
        Ok(len)
    }
}

pub(crate) struct AshmemToggleShrinker;

impl AshmemToggle for AshmemToggleShrinker {
    const NAME: &'static CStr = c_str!("ashmem_unpinning_enable");
    fn set(globals: &AshmemGlobals, enabled: bool) -> Result<()> {
        ashmem_range::set_shrinker_enabled(globals, enabled)
    }
    fn get(globals: &AshmemGlobals) -> bool {
        ashmem_range::get_shrinker_enabled(globals)
    }
}

pub(crate) struct AshmemToggleRead;

impl AshmemToggle for AshmemToggleRead {
    const NAME: &'static CStr = c_str!("ashmem_ignore_unset_prot_read");
    fn set(globals: &AshmemGlobals, enabled: bool) -> Result<()> {
        globals
            .ignore_unset_prot_read
            .store(enabled, Ordering::Relaxed);
        Ok(())
    }
    fn get(globals: &AshmemGlobals) -> bool {
        globals.ignore_unset_prot_read.load(Ordering::Relaxed)
    }
}

pub(crate) struct AshmemToggleExec;

impl AshmemToggle for AshmemToggleExec {
    const NAME: &'static CStr = c_str!("ashmem_ignore_unset_prot_exec");
    fn set(globals: &AshmemGlobals, enabled: bool) -> Result<()> {
        globals
            .ignore_unset_prot_exec
            .store(enabled, Ordering::Relaxed);
        Ok(())
    }
    fn get(globals: &AshmemGlobals) -> bool {
        globals.ignore_unset_prot_exec.load(Ordering::Relaxed)
    }
}
