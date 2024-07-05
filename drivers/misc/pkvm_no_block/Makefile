clean-files := hyp/hyp.lds hyp/hyp-reloc.S

obj-m := pkvm_no_block.o
pkvm_no_block-y := no_block.o hyp/kvm_nvhe.o

$(obj)/hyp/kvm_nvhe.o: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/hyp $(obj)/hyp/kvm_nvhe.o
