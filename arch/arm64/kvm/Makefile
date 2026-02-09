# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -I $(srctree)/$(src)

KVM=../../../virt/kvm

obj-$(CONFIG_KVM) += kvm.o
obj-$(CONFIG_KVM) += hyp/

kvm-y := $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/eventfd.o \
	 $(KVM)/vfio.o $(KVM)/irqchip.o $(KVM)/binary_stats.o iommu.o \
	 arm.o mmu.o mmio.o psci.o hypercalls.o pvtime.o \
	 inject_fault.o va_layout.o handle_exit.o \
	 guest.o debug.o reset.o sys_regs.o stacktrace.o \
	 vgic-sys-reg-v3.o fpsimd.o pkvm.o \
	 arch_timer.o trng.o vmid.o \
	 vgic/vgic.o vgic/vgic-init.o \
	 vgic/vgic-irqfd.o vgic/vgic-v2.o \
	 vgic/vgic-v3.o vgic/vgic-v4.o \
	 vgic/vgic-mmio.o vgic/vgic-mmio-v2.o \
	 vgic/vgic-mmio-v3.o vgic/vgic-kvm-device.o \
	 vgic/vgic-its.o vgic/vgic-debug.o

<<<<<<< HEAD   (a53e8bc664aedd6a3aefddfbc972b05ca5c1683d Merge eeeaba737919 ("HID: core: Harden s32ton() against conv)
kvm-$(CONFIG_HW_PERF_EVENTS)  += pmu-emul.o pmu.o
kvm-$(CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC) += android_erratum_pgtable.o

# Work around a false positive Clang 22 -Wuninitialized-const-pointer warning
CFLAGS_sys_regs.o := $(call cc-disable-warning, uninitialized-const-pointer)

kvm-$(CONFIG_TRACING) += hyp_events.o hyp_trace.o

always-y := hyp_constants.h hyp-constants.s

define rule_gen_hyp_constants
	$(call filechk,offsets,__HYP_CONSTANTS_H__)
endef

CFLAGS_hyp-constants.o = -I $(srctree)/$(src)/hyp/include
$(obj)/hyp-constants.s: $(src)/hyp/hyp-constants.c FORCE
	$(call if_changed_dep,cc_s_c)

$(obj)/hyp_constants.h: $(obj)/hyp-constants.s FORCE
	$(call if_changed_rule,gen_hyp_constants)

obj-kvm := $(addprefix $(obj)/, $(kvm-y))
$(obj-kvm): $(obj)/hyp_constants.h
||||||| BASE   (eeeaba737919bdce9885e2a00ac2912f61a3684d HID: core: Harden s32ton() against conversion to 0 bits)
kvm-$(CONFIG_HW_PERF_EVENTS)  += pmu-emul.o
=======
kvm-$(CONFIG_HW_PERF_EVENTS)  += pmu-emul.o

# Work around a false positive Clang 22 -Wuninitialized-const-pointer warning
CFLAGS_sys_regs.o := $(call cc-disable-warning, uninitialized-const-pointer)
>>>>>>> BRANCH (ef2fe0c6353b3cfbfd1efb73b4108b8409cb1035 KVM: arm64: sys_regs: disable -Wuninitialized-const-pointer )
