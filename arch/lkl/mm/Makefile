# SPDX-License-Identifier: GPL-2.0
KASAN_SANITIZE := n

obj-y = bootmem.o \
	vmalloc.o \
	kasan_init.o

obj-$(CONFIG_MMU) += tlb.o
obj-$(CONFIG_MMU) += mem.o

