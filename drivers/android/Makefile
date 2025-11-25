# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_ANDROID_BINDER_IPC)	+= c_binder.o
obj-$(CONFIG_ANDROID_BINDER_IPC_PICK)	+= binder_pick.o
obj-$(CONFIG_ANDROID_BINDER_ALLOC_KUNIT_TEST)	+= tests/
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST)	+= binder/
obj-$(CONFIG_ANDROID_VENDOR_HOOKS) += vendor_hooks.o
obj-$(CONFIG_ANDROID_DEBUG_KINFO)	+= debug_kinfo.o

c_binder-$(CONFIG_ANDROID_BINDER_IPC) := binder.o binder_alloc.o binder_netlink.o
ifdef CONFIG_ANDROID_BINDERFS
c_binder-$(CONFIG_ANDROID_BINDER_IPC) += binderfs.o
endif
