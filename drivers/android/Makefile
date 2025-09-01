# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)			# needed for trace events

obj-$(CONFIG_ANDROID_BINDERFS)		+= binderfs.o
obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder.o binder_alloc.o binder_netlink.o
obj-$(CONFIG_ANDROID_BINDER_IPC_SELFTEST) += binder_alloc_selftest.o
obj-$(CONFIG_ANDROID_VENDOR_HOOKS) += vendor_hooks.o
obj-$(CONFIG_ANDROID_DEBUG_KINFO)	+= debug_kinfo.o
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
obj-y += binder/
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======

obj-$(CONFIG_ANDROID_BINDER_IPC_RUST)	+= rust_binder.o
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY)	+= rust_binder.o
ifdef CONFIG_ANDROID_BINDER_IPC_RUST
rust_binder-objs := binder/rust_binder.o binder/rust_binderfs.o binder/rust_binder_events.o
rust_binder-objs += binder/rust_binder_hooks.o binder/page_range_helper.o
else
rust_binder-objs := binder/dummy.o
endif
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
