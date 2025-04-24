obj-m += pkvm_8250.o

$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o

clean-files := hyp/hyp.lds hyp/hyp-reloc.S

pkvm_8250-y := 8250-host.o hyp/kvm_nvhe.o
