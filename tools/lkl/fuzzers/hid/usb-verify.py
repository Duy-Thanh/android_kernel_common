#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# facedancer-keyboard.py

import argparse
import signal
import threading
import io
import struct

import greatfet
import hexdump

from facedancer.USB import *
from facedancer.USBDevice import *
from facedancer.USBConfiguration import *
from facedancer.USBInterface import *
from facedancer.USBEndpoint import *
from facedancer import FacedancerUSBApp


class USBHidInterface(USBInterface):
    name = "USB keyboard interface"
    hid_descriptor = b'\x09\x21\x10\x01\x00\x01\x22' # size: '\x2b\x00'

    def __init__(self, report_descriptor, report_data, verbose=0):
        descriptors = {
            USB.desc_type_hid: self.hid_descriptor + struct.pack('<H', len(report_descriptor)),
            USB.desc_type_report: report_descriptor
        }

        self.endpoint = USBEndpoint(
            3,          # endpoint number
            USBEndpoint.direction_in,
            USBEndpoint.transfer_type_interrupt,
            USBEndpoint.sync_type_none,
            USBEndpoint.usage_type_data,
            64,         # max packet size
            10,         # polling interval, see USB 2.0 spec Table 9-13
            self.handle_buffer_available    # handler function
        )

        # TODO: un-hardcode string index (last arg before "verbose")
        USBInterface.__init__(
            self,
            0,          # interface number
            0,          # alternate setting
            3,          # interface class
            0,          # subclass
            0,          # protocol
            0,          # string index
            verbose,
            [self.endpoint],
            descriptors
        )

        self.report_data = report_data

    def handle_buffer_available(self):
        print("handle_buffer_available...")
'''        
        if self.report_data:
            self.endpoint.send(bytearray(self.report_data))
            self.report_data = None
'''

class USBHidDevice(USBDevice):
    name = "USB HID device"

    def __init__(self, maxusb_app, vid, pid, report_descriptor, report_data, verbose=0):
        config = USBConfiguration(
            1,                                                # index
            "Emulated HID devcie",                            # string desc
            [USBHidInterface(report_descriptor, report_data, verbose=verbose)]  # interfaces
        )

        USBDevice.__init__(
            self,
            maxusb_app,
            0,                      # device class
            0,                      # device subclass
            0,                      # protocol release number
            64,                     # max packet size for endpoint 0
            vid,                    # vendor id
            pid,                    # product id
            0x3412,                 # device revision
            "Maxim",                # manufacturer string
            "MAX3420E Enum Code",   # product string
            "S/N3420E",             # serial number string
            [config],
            verbose=verbose
        )


def usb_thread(exit_event, vid, pid, report_descriptor, report_data):
    """entry point of USB event processing thread."""

    print("[USB] Starting...")
    u = FacedancerUSBApp(verbose=5)
    d = USBHidDevice(u, vid, pid, report_descriptor, report_data, verbose=5)

    def stop():
        if exit_event.isSet():
            print("[USB] Stopping...")
            d.stop()

    d.scheduler.add_task(stop)

    d.connect()
    d.run()

    print("[USB] Stopped, disconnecting...")
    d.disconnect()
    print("[USB] Done.")


def main():
    default_report_descriptor = (
        b'\x05\x01\x09\x06\xA1\x01\x05\x07\x19\xE0\x29\xE7\x15\x00\x25\x01'
        b'\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x01\x19\x00\x29\x65'
        b'\x15\x00\x25\x65\x75\x08\x95\x01\x81\x00\xC0'
    )

    default_report_data = bytes([x for x in range(0x20, 0x30)])

    default_vid = 0x610b
    default_pid = 0x4653

    default_hid_data = \
        struct.pack('BBxxHH', 
            len(default_report_descriptor), len(default_report_data),
            default_vid, default_pid) + \
        default_report_descriptor + \
        default_report_data

    parser = argparse.ArgumentParser(description="USB HID enumerator")

    parser.add_argument(
        "--data",
        type=argparse.FileType("rb", 0),
        help="HID report descriptor and input data",
        default=io.BytesIO(default_hid_data))

    parser.add_argument(
        "--timeout",
        type=int,
        help="Stopping emulation after specified period (seconds)"
    )
    args = parser.parse_args()
    data = args.data.read()

    if len(data) > 8:
        (desc_sz, input_sz, vid, pid) = struct.unpack_from('BBxxHH', data, 0)
        if desc_sz > 255:
            desc_sz = 255
        
        desc_data = data[8: 8 + desc_sz]

        if input_sz > 255:
            input_sz = 255
        
        input_data = data[8 + desc_sz: 8 + desc_sz + input_sz]

        print(f"VID={vid:04X}, PID={pid:04X}")
        print(f"RDESC: size={len(desc_data)}")
        hexdump.hexdump(desc_data)

        print(f"INPUT: size={len(input_data)}")
        hexdump.hexdump(input_data)

        e = threading.Event()
        signal.signal(signal.SIGINT, lambda num, frame: e.set())

        t = threading.Thread(
            target=usb_thread,
            args=(e, vid, pid, desc_data, input_data))
        t.start()

        if args.timeout:
            e.wait(args.timeout)
            e.set()

        t.join()


if __name__ == "__main__":
    main()
