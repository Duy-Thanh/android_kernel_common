ifneq ($(CONFIG_PKVM_SMC_FILTER),)
hyp-obj-y += pkvm-smc.o
endif

ifneq ($(CONFIG_PKVM_SMC_FILTER),)
hyp-obj-y += pkvm-guest-smc-proxy.o
endif

include $(srctree)/arch/arm64/kvm/hyp/nvhe/Makefile.module