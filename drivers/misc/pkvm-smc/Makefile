ifneq ($(KERNELRELEASE),)
clean-files := pkvm/hyp.lds pkvm/hyp-reloc.S

ifneq ($(CONFIG_PKVM_SMC_FILTER),)
obj-m += pkvm_smc.o
endif

ifneq ($(CONFIG_PKVM_GUEST_SMC_HANDLER),)
obj-m += pkvm_guest_smc.o
endif

pkvm_smc-$(CONFIG_PKVM_SMC_FILTER) := pkvm-smc.o pkvm/kvm_nvhe.o
pkvm_guest_smc-$(CONFIG_PKVM_GUEST_SMC_HANDLER) := pkvm-guest-smc.o

$(obj)/pkvm/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/pkvm $(obj)/pkvm/kvm_nvhe.o
else
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
endif