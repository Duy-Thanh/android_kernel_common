// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

#![allow(missing_docs)]

mod content;
mod defs;
mod rw_semaphore;
mod wrapped_device;
mod wrapping_device;

use crate::wrapping_device::WrappingDevice;
use kernel::{prelude::*, Module};

module! {
    type: RealWrapFdModule,
    name: "rust_wrapfd",
    author: "Andreas Huber",
    description: "WrapFD",
    license: "GPL",
    imports_ns: ["DMA_BUF"],
}

#[allow(dead_code)]
struct RealWrapFdModule(Pin<KBox<WrapFdModule>>);

impl Module for RealWrapFdModule {
    fn init(module: &'static kernel::ThisModule) -> Result<Self> {
        let inner = KBox::try_pin_init(WrapFdModule::init(module), GFP_KERNEL)?;
        Ok(Self(inner))
    }
}

#[pin_data]
struct WrapFdModule {
    #[pin]
    miscdev: kernel::miscdevice::MiscDeviceRegistration<WrappingDevice>,
}

impl WrapFdModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        let options = kernel::miscdevice::MiscDeviceOptions {
            name: kernel::c_str!("rust-wrapfd"),
        };

        try_pin_init!(Self {
            miscdev <- kernel::miscdevice::MiscDeviceRegistration::register(options),
        })
    }
}
