# SPDX-License-Identifier: GPL-2.0
#
# Driver Makefile for a <target>/ppps_modules/ directory of PPPS fixture
# modules.  The per-directory Kbuild file lists the obj-m; the directory's
# Makefile is just:
#
#	include <path to>/ppps/ppps_modules.mk
#
# lib.mk's TEST_GEN_MODS_DIR runs `make -C <dir>` and `make -C <dir> clean`
# here.  KDIR is the kernel tree to build against: the PPPS kernel's output
# directory when O= is set, otherwise the source tree these tests live in.
# Nothing is built when that tree has not been configured and built yet
# (no Module.symvers), so `make TARGETS=...` still succeeds on a bare tree.

PPPS_SELFTESTS_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/..)
KDIR ?= $(if $(O),$(O),$(realpath $(PPPS_SELFTESTS_DIR)/../../..))

ifeq ($(V),1)
Q =
else
Q = @
endif

modules all:
ifneq ("$(wildcard $(KDIR)/Module.symvers)", "")
	$(Q)$(MAKE) -C $(KDIR) M=$(CURDIR) modules
else
	@echo "  SKIP     $(notdir $(CURDIR)): no built kernel at $(KDIR)"
endif

clean:
ifneq ("$(wildcard $(KDIR)/Makefile)", "")
	$(Q)$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif

.PHONY: modules all clean
