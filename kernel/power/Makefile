# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_DYNAMIC_DEBUG), y)
CFLAGS_swap.o                   := -DDEBUG
CFLAGS_snapshot.o               := -DDEBUG
CFLAGS_energy_model.o           := -DDEBUG
endif

KASAN_SANITIZE_snapshot.o	:= n

obj-y				+= qos.o
obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_VT_CONSOLE_SLEEP)	+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o snapshot.o swap.o
obj-$(CONFIG_HIBERNATION_SNAPSHOT_DEV) += user.o
obj-$(CONFIG_PM_AUTOSLEEP)	+= autosleep.o
obj-$(CONFIG_PM_WAKELOCKS)	+= wakelock.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o

<<<<<<< HEAD   (2a0d77b541ce54f0a0c63c249a9134fe376303c1 Merge 959bfe496bba ("Merge tag 'acpi-6.19-rc1' of git://git.)
obj-$(CONFIG_SUSPEND)		+= wakeup_reason.o
obj-$(CONFIG_ENERGY_MODEL)	+= energy_model.o
||||||| BASE   (959bfe496bbaf3daa5dca32d397e29ea12471779 Merge tag 'acpi-6.19-rc1' of git://git.kernel.org/pub/scm/li)
obj-$(CONFIG_ENERGY_MODEL)	+= energy_model.o
=======
obj-$(CONFIG_ENERGY_MODEL)	+= em.o
em-y				:= energy_model.o
em-$(CONFIG_NET)		+= em_netlink_autogen.o em_netlink.o
>>>>>>> BRANCH (d348c22394ad3c8eaf7bc693cb0ca0edc2ec5246 Merge tag 'pm-6.19-rc1' of git://git.kernel.org/pub/scm/linu)
