# SPDX-License-Identifier: GPL-2.0
#
# Makefile for USB Core files and filesystem
#

usbcore-y := usb.o hub.o hcd.o urb.o message.o driver.o
usbcore-y += config.o file.o buffer.o sysfs.o endpoint.o
usbcore-y += devio.o notify.o generic.o quirks.o devices.o
usbcore-y += phy.o port.o

usbcore-$(CONFIG_OF)		+= of.o
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
usbcore-$(CONFIG_USB_XHCI_SIDEBAND)	+= offload.o
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
usbcore-$(CONFIG_USB_PCI)		+= hcd-pci.o
usbcore-$(CONFIG_ACPI)		+= usb-acpi.o

ifdef CONFIG_USB_ONBOARD_DEV
usbcore-y			+= ../misc/onboard_usb_dev_pdevs.o
endif

obj-$(CONFIG_USB)		+= usbcore.o

obj-$(CONFIG_USB_LEDS_TRIGGER_USBPORT)	+= ledtrig-usbport.o
