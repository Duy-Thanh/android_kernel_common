// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

use crate::{
    content::WrapDMAContent,
    defs::WrapFdWrap,
    helpers,
    wrapped_device::{WrappedDevice, WrappedDeviceInterface},
};

use kernel::{
    bindings, device,
    prelude::*,
    sync::Arc,
    types::{ARef, ForeignOwnable},
    uaccess::{UserPtr, UserSlice},
    uapi,
};

#[pin_data]
pub(crate) struct WrappingDevice {
    wrapping_dev: ARef<device::Device>,
    wrapped_dev: Option<Pin<KBox<WrappedDevice>>>,
}

#[vtable]
impl kernel::miscdevice::MiscDevice for WrappingDevice {
    type Ptr = Pin<KBox<Self>>;

    fn open(
        file: &kernel::fs::File,
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
                let user_slice = UserSlice::new(arg, _IOC_SIZE(cmd));
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

        let content = Arc::new(
            WrapDMAContent::new(wrapping_dev, args.fd, args.prot)?,
            GFP_KERNEL,
        )?;
        Self::publish_wrap(wrapped_dev, content).map_err(|(_content, err)| err)
    }

    pub(crate) fn publish_wrap(
        mut dev: Pin<KBox<WrappedDevice>>,
        content: Arc<WrapDMAContent>,
    ) -> Result<u32, (Arc<WrapDMAContent>, Error)> {
        let writable = content.writable;
        let close_on_exec = content.close_on_exec;

        // unsafe { device.as_mut().get_unchecked_mut() }.content = Some(content);
        let size = content.dmabuf.size();
        match WrapDMAContent::create_wrap(dev, writable, close_on_exec, size) {
            Ok((fd, file)) => {
                // XXX This is quite horrible and needs work, working around the fact
                // that attempting to create the underlying file involves transferring
                // ownership of the device via into_foreign(), but we do need to set the
                // content on success when all we have left to on is the file/fd.
                let private = unsafe { (*file.as_ptr()).private_data };
                let mut dev = unsafe {
                    <<WrappedDevice as WrappedDeviceInterface>::Ptr as ForeignOwnable>::borrow(
                        private.cast(),
                    )
                };

                dev.lock.lock().content = Some(content);

                Ok(fd)
            }
            Err(err) => Err((content, err)),
        }
    }
}
