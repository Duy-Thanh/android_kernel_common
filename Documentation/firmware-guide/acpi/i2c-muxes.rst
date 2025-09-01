.. SPDX-License-Identifier: GPL-2.0

==============
ACPI I2C Muxes
==============

Describing an I2C device hierarchy that includes I2C muxes requires an ACPI
Device () scope per mux channel.

Consider this topology::

    +------+   +------+
    | SMB1 |-->| MUX0 |--CH00--> i2c client A (0x50)
    |      |   | 0x70 |--CH01--> i2c client B (0x50)
    +------+   +------+

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
which corresponds to the following ASL (in the scope of \_SB)::

    Device (SMB1)
    {
        Name (_HID, ...)
        Device (MUX0)
        {
            Name (_HID, ...)
            Name (_CRS, ResourceTemplate () {
                I2cSerialBus (0x70, ControllerInitiated, I2C_SPEED,
                            AddressingMode7Bit, "\\_SB.SMB1", 0x00,
                            ResourceConsumer,,)
            }

            Device (CH00)
            {
                Name (_ADR, 0)

                Device (CLIA)
                {
                    Name (_HID, ...)
                    Name (_CRS, ResourceTemplate () {
                        I2cSerialBus (0x50, ControllerInitiated, I2C_SPEED,
                                    AddressingMode7Bit, "\\_SB.SMB1.CH00", 0x00,
                                    ResourceConsumer,,)
                    }
                }
            }

            Device (CH01)
            {
                Name (_ADR, 1)

                Device (CLIB)
                {
                    Name (_HID, ...)
                    Name (_CRS, ResourceTemplate () {
                        I2cSerialBus (0x50, ControllerInitiated, I2C_SPEED,
                                    AddressingMode7Bit, "\\_SB.SMB1.CH01", 0x00,
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
which corresponds to the following ASL::

    Device (SMB1)
    {
        Name (_HID, ...)
        Device (MUX0)
        {
            Name (_HID, ...)
            Name (_CRS, ResourceTemplate () {
                I2cSerialBus (0x70, ControllerInitiated, I2C_SPEED,
                            AddressingMode7Bit, "^SMB1", 0x00,
                            ResourceConsumer,,)
            }

            Device (CH00)
            {
                Name (_ADR, 0)

                Device (CLIA)
                {
                    Name (_HID, ...)
                    Name (_CRS, ResourceTemplate () {
                        I2cSerialBus (0x50, ControllerInitiated, I2C_SPEED,
                                    AddressingMode7Bit, "^CH00", 0x00,
                                    ResourceConsumer,,)
                    }
                }
            }

            Device (CH01)
            {
                Name (_ADR, 1)

                Device (CLIB)
                {
                    Name (_HID, ...)
                    Name (_CRS, ResourceTemplate () {
                        I2cSerialBus (0x50, ControllerInitiated, I2C_SPEED,
                                    AddressingMode7Bit, "^CH01", 0x00,
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
                                    ResourceConsumer,,)
                    }
                }
            }
        }
    }
