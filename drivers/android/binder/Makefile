# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)	# needed for trace events
ccflags-y += -Wmissing-prototypes -Wmissing-declarations

obj-$(CONFIG_ANDROID_BINDER_IPC_RUST) += rust_binder.o
rust_binder-$(CONFIG_ANDROID_BINDER_IPC_RUST) := \
	rust_binder_main.o	\
	rust_binderfs.o		\
	rust_binder_events.o	\
	rust_binder_hooks.o	\
	page_range_helper.o

<<<<<<< HEAD   (f5123010c3966c912dba3c15a16a224655baabb5 ANDROID: gki: update rustc to 1.82.0.p1)
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY) += rust_binder.o
rust_binder-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY) := dummy.o
||||||| BASE   (2969b9451541560b6e55efaea399656acc6bb990 ANDROID: gki: update rustc to 1.82.0.p1)
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY) += rust_binder.o
rust_binder-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY) := dummy.o

# Hooks needs to be built-in always so that DDK modules with vendor hooks do
# not form a hard dependency on rust_binder.ko
ifdef CONFIG_ANDROID_BINDER_IPC_RUST
obj-y += rust_binder_hooks.o
endif
=======
# Hooks needs to be built-in always so that DDK modules with vendor hooks do
# not form a hard dependency on rust_binder.ko
ifdef CONFIG_ANDROID_BINDER_IPC_RUST
obj-y += rust_binder_hooks.o
endif
>>>>>>> CHANGE (d059ed15d1f05e8c9ec40d9362c4c6b8801987fd ANDROID: Revert "binder: add ANDROID_BINDER_IPC_RUST_DUMMY")
