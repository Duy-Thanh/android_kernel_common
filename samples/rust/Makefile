# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_SAMPLE_RUST_MINIMAL)		+= rust_minimal.o
obj-$(CONFIG_SAMPLE_RUST_PRINT)			+= rust_print.o
rust_minimal-y                                  += minimal.o ../../rust/lib.a
rust_print-y                                    += rust_print.o ../../rust/lib.a

subdir-$(CONFIG_SAMPLE_RUST_HOSTPROGS)		+= hostprogs
