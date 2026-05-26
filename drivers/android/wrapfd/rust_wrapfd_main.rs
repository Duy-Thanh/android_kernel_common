// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

#![allow(missing_docs, unused)]

mod content;
mod defs;
mod helpers;
mod wrapped_device;
mod wrapping_device;

use crate::wrapping_device::WrappingDevice;
use kernel::prelude::*;

// Terrible HACK to include the DMA_BUF namespace after all other attempts at placating modpost failed.
core::arch::global_asm!(
    r#".section ".modinfo","a"
       .asciz "import_ns=DMA_BUF"
       .previous"#
);

module! {
    type: WrapFdModule,
    name: "rust_wrapfd",
    authors: ["Suren Baghdasaryan", "Andreas Huber"],
    description: "WrapFD",
    license: "GPL",
}

#[pin_data(PinnedDrop)]
struct WrapFdModule {
    #[pin]
    miscdev: kernel::miscdevice::MiscDeviceRegistration<WrappingDevice>,
}

impl kernel::InPlaceModule for WrapFdModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        pr_info!("Rust WrapFd (init)\n");

        let options = kernel::miscdevice::MiscDeviceOptions {
            name: kernel::c_str!("rust-wrapfd"),
        };

        try_pin_init!(Self {
            miscdev <- kernel::miscdevice::MiscDeviceRegistration::register(options),
        })
        .pin_chain(|this| {
            let dev = kernel::types::ARef::from(this.miscdev.device());
            dev_info!(dev, "pin_chain works!!!\n");

            // Further initialization involving the device go here.

            Ok(())
        })
    }
}

#[pinned_drop]
impl PinnedDrop for WrapFdModule {
    fn drop(self: Pin<&mut Self>) {
        pr_info!("Rust WrapFd (exit)\n");
    }
}
