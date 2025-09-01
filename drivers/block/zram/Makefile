# SPDX-License-Identifier: GPL-2.0-only

zram-y	:=	zcomp.o zram_drv.o

zram-$(CONFIG_ZRAM_BACKEND_LZO)		+= backend_lzorle.o backend_lzo.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4)		+= backend_lz4.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4HC)	+= backend_lz4hc.o
zram-$(CONFIG_ZRAM_BACKEND_ZSTD)	+= backend_zstd.o
zram-$(CONFIG_ZRAM_BACKEND_DEFLATE)	+= backend_deflate.o
zram-$(CONFIG_ZRAM_BACKEND_842)		+= backend_842.o
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
zram-$(CONFIG_ZRAM_ANDROID_IOCTL)	+= zram_ioctl.o
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

obj-$(CONFIG_ZRAM)	+=	zram.o
