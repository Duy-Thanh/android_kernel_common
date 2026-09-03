// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

#![allow(missing_docs)]

mod content;
mod defs;
mod rw_semaphore;
mod wrapped_device;
mod wrapping_device;

use crate::wrapping_device::WrappingDevice;
use kernel::{prelude::*, InPlaceModule};

module! {
    type: WrapFdModule,
    name: "rust_wrapfd",
    authors: ["Andreas Huber"],
    description: "WrapFD",
    license: "GPL",
    imports_ns: ["DMA_BUF"],
}

#[pin_data]
struct WrapFdModule {
    #[pin]
    miscdev: kernel::miscdevice::MiscDeviceRegistration<WrappingDevice>,
}

impl InPlaceModule for WrapFdModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        let options = kernel::miscdevice::MiscDeviceOptions {
            name: kernel::c_str!("rust-wrapfd"),
        };

        try_pin_init!(Self {
            miscdev <- kernel::miscdevice::MiscDeviceRegistration::register(options),
        })
    }
}
