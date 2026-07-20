# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited

TEST_GEN_PROGS := hwcap ptrace syscall-abi tpidr2
AARCH32_TESTS := aarch32-mmap2-ppps aarch32-vdso-pages-ppps

ifneq ($(LLVM),)
TEST_GEN_PROGS += $(AARCH32_TESTS)
AARCH32_CC = $(CLANG) --target=arm-linux-gnueabi
AARCH32_LDFLAGS = -fuse-ld=lld
else ifneq ($(CROSS_COMPILE_COMPAT),)
TEST_GEN_PROGS += $(AARCH32_TESTS)
AARCH32_CC = $(CROSS_COMPILE_COMPAT)gcc
endif

include ../../lib.mk

$(OUTPUT)/syscall-abi: syscall-abi.c syscall-abi-asm.S

$(addprefix $(OUTPUT)/,$(AARCH32_TESTS)): $(OUTPUT)/%: %.c
	$(AARCH32_CC) -march=armv7-a -mfloat-abi=soft -ffreestanding \
		-fno-builtin -fno-stack-protector -nostdlib -static \
		$(AARCH32_LDFLAGS) -Wl,-e,_start -Wl,-z,max-page-size=4096 \
		-O2 -Wall -Wextra -Werror $^ -o $@

# Build with nolibc since TPIDR2 is intended to be actively managed by
# libc and we're trying to test the functionality that it depends on here.
$(OUTPUT)/tpidr2: tpidr2.c
	$(CC) -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
		-static -include ../../../../include/nolibc/nolibc.h \
		-I../.. -ffreestanding -Wall $^ -o $@ -lgcc
