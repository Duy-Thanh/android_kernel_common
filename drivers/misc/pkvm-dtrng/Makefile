ifneq ($(KERNELRELEASE),)

subdir-ccflags-y += -I$(srctree)/arch/arm64/kvm/hyp/include/

clean-files := pkvm/hyp.lds pkvm/hyp-reloc.S
obj-m := pkvm_dtrng.o

pkvm_dtrng-y := pkvm-dtrng.o hyp/kvm_nvhe.o

$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o

else
all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
endif
