# SPDX-License-Identifier: GPL-2.0
KASAN_SANITIZE := n

extra-y := vmlinux.lds
extra-y += vmlinux-fuzzing-postlink.lds

obj-y = atomic64.o setup.o threads.o irq.o time.o syscalls.o misc.o console.o \
	syscalls_32.o syscalls_lkl.o cpu.o stacktrace.o
