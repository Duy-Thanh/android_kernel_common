// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use crate::{content::WrapDMAContent, defs::WrapFdWrap, wrapped_device::WrappedDevice};

use kernel::{bindings, device, prelude::*, types::ARef, uaccess::UserSlice, uapi};

#[pin_data]
pub(crate) struct WrappingDevice {
    wrapping_dev: ARef<device::Device>,
    wrapped_dev: Option<Pin<KBox<WrappedDevice>>>,
}

#[vtable]
impl kernel::miscdevice::MiscDevice for WrappingDevice {
    type Ptr = Pin<KBox<Self>>;

    fn open(
        _file: &kernel::fs::File,
        misc: &kernel::miscdevice::MiscDeviceRegistration<Self>,
    ) -> Result<Pin<KBox<Self>>> {
        let wrapping_dev = kernel::types::ARef::from(misc.device());

        KBox::try_pin_init(
            try_pin_init! {
                WrappingDevice { wrapping_dev, wrapped_dev: None }
            },
            GFP_KERNEL,
        )
    }

    fn ioctl(me: Pin<&Self>, _file: &kernel::fs::File, cmd: u32, arg: usize) -> Result<isize> {
        match cmd {
            uapi::WRAPFD_DEV_IOC_WRAP => {
                use kernel::ioctl::_IOC_SIZE;
                let user_slice = UserSlice::new(UserPtr::from_addr(arg), _IOC_SIZE(cmd));
                let wrap_args = user_slice.reader().read::<WrapFdWrap>()?;

                let wrapped_device: Pin<KBox<WrappedDevice>> =
                    KBox::pin_init(WrappedDevice::new(), GFP_KERNEL)?;

                Ok(Self::wrap_file(me.wrapping_dev.clone(), wrapped_device, &wrap_args)? as isize)
            }
            _ => Err(ENOTTY),
        }
    }
}

impl WrappingDevice {
    fn wrap_file(
        wrapping_dev: ARef<device::Device>,
        wrapped_dev: Pin<KBox<WrappedDevice>>,
        args: &WrapFdWrap,
    ) -> Result<u32> {
        if (args.prot & !(bindings::PROT_READ | bindings::PROT_WRITE)) != 0 {
            return Err(EINVAL);
        }

        if args.reserved != 0 {
            return Err(EINVAL);
        }

        let content = WrapDMAContent::new(wrapping_dev, args.fd, args.prot)?;
        Self::publish_wrap(wrapped_dev, content).map_err(|(err, _content)| err)
    }

    pub(crate) fn publish_wrap(
        dev: Pin<KBox<WrappedDevice>>,
        content: WrapDMAContent,
    ) -> Result<u32, (Error, WrapDMAContent)> {
        WrapDMAContent::create_wrap(dev, content)
    }
}
