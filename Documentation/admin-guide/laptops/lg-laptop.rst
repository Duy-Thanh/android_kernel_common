.. SPDX-License-Identifier: GPL-2.0+


LG Gram laptop extra features
=============================

By Matan Ziv-Av <matan@svgalib.org>


Hotkeys
-------

The following FN keys are ignored by the kernel without this driver:

- FN-F1 (LG control panel)   - Generates F15
- FN-F5 (Touchpad toggle)    - Generates F21
- FN-F6 (Airplane mode)      - Generates RFKILL
- FN-F9 (Reader mode)        - Generates F14

The rest of the FN keys work without a need for a special driver.


Reader mode
-----------

Writing 0/1 to /sys/devices/platform/lg-laptop/reader_mode disables/enables
reader mode. In this mode the screen colors change (blue color reduced),
and the reader mode indicator LED (on F9 key) turns on.


FN Lock
-------

Writing 0/1 to /sys/devices/platform/lg-laptop/fn_lock disables/enables
FN lock.


Battery care limit
------------------

Writing 80/100 to /sys/class/power_supply/CMB0/charge_control_end_threshold
sets the maximum capacity to charge the battery. Limiting the charge
reduces battery capacity loss over time.

This value is reset to 100 when the kernel boots.


Fan mode
--------

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
Writing 0/1/2 to /sys/devices/platform/lg-laptop/fan_mode sets fan mode to
Optimal/Silent/Performance respectively.
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
Writing 1/0 to /sys/devices/platform/lg-laptop/fan_mode disables/enables
the fan silent mode.
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)


USB charge
----------

Writing 0/1 to /sys/devices/platform/lg-laptop/usb_charge disables/enables
charging another device from the USB port while the device is turned off.

This value is reset to 0 when the kernel boots.


LEDs
~~~~

The are two LED devices supported by the driver:

Keyboard backlight
------------------

A led device named kbd_led controls the keyboard backlight. There are three
lighting level: off (0), low (127) and high (255).

The keyboard backlight is also controlled by the key combination FN-F8
which cycles through those levels.


Touchpad indicator LED
----------------------

On the F5 key. Controlled by led device names tpad_led.
