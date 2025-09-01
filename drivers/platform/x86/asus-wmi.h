/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Asus PC WMI hotkey driver
 *
 * Copyright(C) 2010 Intel Corporation.
 * Copyright(C) 2010-2011 Corentin Chary <corentin.chary@gmail.com>
 *
 * Portions based on wistron_btns.c:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 */

#ifndef _ASUS_WMI_H_
#define _ASUS_WMI_H_

#include <linux/platform_device.h>
#include <linux/i8042.h>

#define ASUS_WMI_KEY_IGNORE (-1)
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#define ASUS_WMI_KEY_ARMOURY	0xffff01
#define ASUS_WMI_BRN_DOWN	0x2e
#define ASUS_WMI_BRN_UP		0x2f

struct module;
struct key_entry;
struct asus_wmi;

enum asus_wmi_tablet_switch_mode {
	asus_wmi_no_tablet_switch,
	asus_wmi_kbd_dock_devid,
	asus_wmi_lid_flip_devid,
	asus_wmi_lid_flip_rog_devid,
};

struct quirk_entry {
	bool hotplug_wireless;
	bool scalar_panel_brightness;
	bool store_backlight_power;
	bool wmi_backlight_set_devstate;
	bool wmi_force_als_set;
	bool wmi_ignore_fan;
	bool filter_i8042_e1_extended_codes;
	int key_wlan_event;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
#define ASUS_WMI_BRN_DOWN	0x2e
#define ASUS_WMI_BRN_UP		0x2f

struct module;
struct key_entry;
struct asus_wmi;

enum asus_wmi_tablet_switch_mode {
	asus_wmi_no_tablet_switch,
	asus_wmi_kbd_dock_devid,
	asus_wmi_lid_flip_devid,
	asus_wmi_lid_flip_rog_devid,
};

struct quirk_entry {
	bool hotplug_wireless;
	bool scalar_panel_brightness;
	bool store_backlight_power;
	bool wmi_backlight_set_devstate;
	bool wmi_force_als_set;
	bool wmi_ignore_fan;
	bool filter_i8042_e1_extended_codes;
	bool ignore_key_wlan;
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	enum asus_wmi_tablet_switch_mode tablet_switch_mode;
	int wapf;
	/*
	 * For machines with AMD graphic chips, it will send out WMI event
	 * and ACPI interrupt at the same time while hitting the hotkey.
	 * To simplify the problem, we just have to ignore the WMI event,
	 * and let the ACPI interrupt to send out the key event.
	 */
	int no_display_toggle;
	u32 xusb2pr;
};

struct asus_wmi_driver {
	int			brightness;
	int			panel_power;
	int			screenpad_brightness;
	int			wlan_ctrl_by_user;

	const char		*name;
	struct module		*owner;

	const char		*event_guid;

	const struct key_entry	*keymap;
	const char		*input_name;
	const char		*input_phys;
	struct quirk_entry	*quirks;
	/* Returns new code, value, and autorelease values in arguments.
	 * Return ASUS_WMI_KEY_IGNORE in code if event should be ignored. */
	void (*key_filter) (struct asus_wmi_driver *driver, int *code,
			    unsigned int *value, bool *autorelease);
	/* Optional standard i8042 filter */
	bool (*i8042_filter)(unsigned char data, unsigned char str,
			     struct serio *serio);

	int (*probe) (struct platform_device *device);
	void (*detect_quirks) (struct asus_wmi_driver *driver);

	struct platform_driver	platform_driver;
	struct platform_device *platform_device;
};

int asus_wmi_register_driver(struct asus_wmi_driver *driver);
void asus_wmi_unregister_driver(struct asus_wmi_driver *driver);

#endif /* !_ASUS_WMI_H_ */
