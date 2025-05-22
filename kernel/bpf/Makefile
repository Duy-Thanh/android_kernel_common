# SPDX-License-Identifier: GPL-2.0
obj-y := core.o
ifneq ($(CONFIG_BPF_JIT_ALWAYS_ON),y)
# ___bpf_prog_run() needs GCSE disabled on x86; see 3193c0836f203 for details
cflags-nogcse-$(CONFIG_X86)$(CONFIG_CC_IS_GCC) := -fno-gcse
endif
CFLAGS_core.o += -Wno-override-init $(cflags-nogcse-yy)

obj-$(CONFIG_BPF_SYSCALL) += syscall.o verifier.o inode.o helpers.o tnum.o log.o token.o
obj-$(CONFIG_BPF_SYSCALL) += bpf_iter.o map_iter.o task_iter.o prog_iter.o link_iter.o
obj-$(CONFIG_BPF_SYSCALL) += hashtab.o arraymap.o percpu_freelist.o bpf_lru_list.o lpm_trie.o map_in_map.o bloom_filter.o
obj-$(CONFIG_BPF_SYSCALL) += local_storage.o queue_stack_maps.o ringbuf.o
obj-$(CONFIG_BPF_SYSCALL) += bpf_local_storage.o bpf_task_storage.o
obj-${CONFIG_BPF_LSM}	  += bpf_inode_storage.o
obj-$(CONFIG_BPF_SYSCALL) += disasm.o mprog.o
obj-$(CONFIG_BPF_JIT) += trampoline.o
obj-$(CONFIG_BPF_SYSCALL) += btf.o memalloc.o
ifeq ($(CONFIG_MMU)$(CONFIG_64BIT),yy)
obj-$(CONFIG_BPF_SYSCALL) += arena.o
endif
obj-$(CONFIG_BPF_JIT) += dispatcher.o
ifeq ($(CONFIG_NET),y)
obj-$(CONFIG_BPF_SYSCALL) += devmap.o
obj-$(CONFIG_BPF_SYSCALL) += cpumap.o
obj-$(CONFIG_BPF_SYSCALL) += offload.o
obj-$(CONFIG_BPF_SYSCALL) += net_namespace.o
obj-$(CONFIG_BPF_SYSCALL) += tcx.o
endif
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_BPF_SYSCALL) += stackmap.o
endif
ifeq ($(CONFIG_CGROUPS),y)
obj-$(CONFIG_BPF_SYSCALL) += cgroup_iter.o bpf_cgrp_storage.o
endif
obj-$(CONFIG_CGROUP_BPF) += cgroup.o
ifeq ($(CONFIG_INET),y)
obj-$(CONFIG_BPF_SYSCALL) += reuseport_array.o
endif
ifeq ($(CONFIG_SYSFS),y)
obj-$(CONFIG_DEBUG_INFO_BTF) += sysfs_btf.o
endif
ifeq ($(CONFIG_BPF_JIT),y)
obj-$(CONFIG_BPF_SYSCALL) += bpf_struct_ops.o
obj-$(CONFIG_BPF_SYSCALL) += cpumask.o
obj-${CONFIG_BPF_LSM} += bpf_lsm.o
endif
ifneq ($(CONFIG_CRYPTO),)
obj-$(CONFIG_BPF_SYSCALL) += crypto.o
endif
obj-$(CONFIG_BPF_PRELOAD) += preload/

obj-$(CONFIG_BPF_SYSCALL) += relo_core.o
<<<<<<< HEAD   (871fc10feba0af10aa92ed47a0fa61b3da983b9b BACKPORT: dma-buf: Rename debugfs symbols)
obj-$(CONFIG_BPF_SYSCALL) += btf_iter.o
obj-$(CONFIG_BPF_SYSCALL) += btf_relocate.o

||||||| BASE   (23dc7cf0f77464173dc3173d55f6b731d54227f0 BACKPORT: dma-buf: Rename debugfs symbols)
$(obj)/relo_core.o: $(srctree)/tools/lib/bpf/relo_core.c FORCE
	$(call if_changed_rule,cc_o_c)
=======

ifeq ($(CONFIG_DMA_SHARED_BUFFER),y)
obj-$(CONFIG_BPF_SYSCALL) += dmabuf_iter.o
endif

$(obj)/relo_core.o: $(srctree)/tools/lib/bpf/relo_core.c FORCE
	$(call if_changed_rule,cc_o_c)
>>>>>>> CHANGE (8817f19017d775f54c810ce66c7bb6682ed8cf61 BACKPORT: bpf: Add dmabuf iterator)
ifeq ($(CONFIG_FUSE_BPF),y)
obj-$(CONFIG_BPF_SYSCALL) += bpf_fuse.o
endif
