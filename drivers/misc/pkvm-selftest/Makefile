obj-m += pkvm-selftest.o

$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o

clean-files := hyp/hyp.lds hyp/hyp-reloc.S

pkvm-selftest-y := pkvm-selftest-host.o hyp/kvm_nvhe.o
