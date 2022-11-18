# Fuzzing RNDIS message parser

RNDIS driver implements Remote Network Driver Interface Specification which is
exposed over USB bus in USB gadget mode.

This fuzzer is intended to reproduce and do a variant analysis of OOBR issue in
`rndis_set_response` routine (b/162326603) implemented in
`drivers/usb/gadget/function/rndis.c` file.

## Architecture

To reach the target RNDIS interface - `rndis_msg_parser` - current fuzzer uses
USB gadget driver `f_rndis` which exploses RNDIS message parser interface over
USB bus. An attacker can send RNDIS messages encapsulated in USB control
requests to the target device. The USB gadget driver `f_rndis` will process the
control requests, extract RNDIS message and forward it for processing to RNDIS
driver.

The fuzzer uses `usbdevfs` subsystem and the USB loopback driver (`dummy_hcd`)
to send RNDIS messages encapsulated in USB control requests to the target
interface:

1. The fuzz target sends a control request with a mutated RNDIS message to
   `/dev/bus/usb/BBB/DDD` which represents a USB device on the host-side of the
   USB bus managed by the USB loopback driver.

2. Usbdevfs subsystem forwards this control request to the USB loopback
   controller driver (`dummy_hcd`).

3. The USB loopback controller driver forwards the control request to USB gadget
   composite driver.

4. USB gadget composite driver forwards the control request to the USB gadget
   function driver `r_rndis`.

5. The USB gadget function driver `f_rdnis` extracts the encapsulated mutated
   RNDIS message and invokes `rndis_msg_parser` to parse it.


## Building the fuzzer

In order to build the fuzzer here is a list of Linux kernel configuration
options to set.

Enable common support for USB subsystem in Linux kernel:

* CONFIG_USB=y
* CONFIG_USB_COMMON=y
* CONFIG_USB_ARCH_HAS_HCD=y

Enable USB loopback bus:

* CONFIG_USB_DUMMY_HCD=y

Enable USB gadget subsystem including the target `f_rndis` driver:

* CONFIG_USB_GADGET=y
* CONFIG_USB_LIBCOMPOSITE=y
* CONFIG_USB_F_RNDIS=y

Enable `devtmpfs` to expose `usbdevfs` via `/dev`:

* CONFIG_DEVTMPFS=y
* CONFIG_DEVTMPFS_MOUNT=y

A set of options to enable configfs to be able to configure USB gadget subsystem
from the fuzzer harness:

* CONFIG_USB_CONFIGFS=y
* CONFIG_USB_CONFIGFS_RNDIS=y

## Notes

### Performance

In its current implementation the mutated input goes all the way through
both USB host and gadget stacks to reach the target code. This path also
includes some asynchronous processing in `dummy_hcd` which relies on a timer to
fetch enqueued control requests from USB host side to the gadget side. This
reduces performance of the fuzzer.

One way to improve the fuzzer performance is to provide mutated input directly
to `rndis_msg_parser` routine. This might require some additional work:
`rndis_msg_parser` isn't exported by `liblkl.a` and calling it directly might
introduce synchronization issues with `lkl_trigger_irq` (see b/171091577 for an
example).

### Devtmpfs support

The fuzzer relies on `usbdevfs` to send USB control requests to the target
interface which in turn depends on `devtmpfs`. Using `lkl_mount_fs` to mount
devtmpfs at `/dev` doesn't  work (b/172868430). Thus, mounting `devtmpfs` at
`/dev` is implemented in the fuzzer harness.
