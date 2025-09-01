# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/sh/boot/romimage/Makefile
#
# create an romImage file suitable for burning to flash/mmc from zImage
#

targets		:= vmlinux head.o zeropage.bin piggy.o
load-y		:= 0

mmcif-load-$(CONFIG_CPU_SUBTYPE_SH7724)	:= 0xe5200000 # ILRAM
mmcif-obj-$(CONFIG_CPU_SUBTYPE_SH7724)	:= $(obj)/mmcif-sh7724.o
load-$(CONFIG_ROMIMAGE_MMCIF)		:= $(mmcif-load-y)
obj-$(CONFIG_ROMIMAGE_MMCIF)		:= $(mmcif-obj-y)

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
LDFLAGS_vmlinux := --oformat $(ld_bfd) -Ttext $(load-y) -e romstart \
		   -T $(obj)/../../kernel/vmlinux.lds

$(obj)/vmlinux: $(obj)/head.o $(obj-y) $(obj)/piggy.o FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS += -j .empty_zero_page

$(obj)/zeropage.bin: vmlinux FORCE
	$(call if_changed,objcopy)

LDFLAGS_piggy.o := -r --format binary --oformat $(ld_bfd) -T
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
LDFLAGS_vmlinux := --oformat $(ld-bfd) -Ttext $(load-y) -e romstart \
		   -T $(obj)/../../kernel/vmlinux.lds

$(obj)/vmlinux: $(obj)/head.o $(obj-y) $(obj)/piggy.o FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS += -j .empty_zero_page

$(obj)/zeropage.bin: vmlinux FORCE
	$(call if_changed,objcopy)

LDFLAGS_piggy.o := -r --format binary --oformat $(ld-bfd) -T
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/zeropage.bin arch/sh/boot/zImage FORCE
	$(call if_changed,ld)
