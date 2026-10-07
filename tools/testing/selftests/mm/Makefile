# SPDX-License-Identifier: GPL-2.0
# Makefile for mm selftests

LOCAL_HDRS += $(selfdir)/mm/local_config.h $(top_srcdir)/mm/gup_test.h
LOCAL_HDRS += $(selfdir)/mm/mseal_helpers.h

include local_config.mk

ifeq ($(ARCH),)

ifeq ($(CROSS_COMPILE),)
uname_M := $(shell uname -m 2>/dev/null || echo not)
else
uname_M := $(shell echo $(CROSS_COMPILE) | grep -o '^[a-z0-9]\+')
endif
ARCH ?= $(shell echo $(uname_M) | sed -e 's/aarch64.*/arm64/' -e 's/ppc64.*/powerpc/')
endif

# Without this, failed build products remain, with up-to-date timestamps,
# thus tricking Make (and you!) into believing that All Is Well, in subsequent
# make invocations:
.DELETE_ON_ERROR:

# Avoid accidental wrong builds, due to built-in rules working just a little
# bit too well--but not quite as well as required for our situation here.
#
# In other words, "make $SOME_TEST" is supposed to fail to build at all,
# because this Makefile only supports either "make" (all), or "make /full/path".
# However,  the built-in rules, if not suppressed, will pick up CFLAGS and the
# initial LDLIBS (but not the target-specific LDLIBS, because those are only
# set for the full path target!). This causes it to get pretty far into building
# things despite using incorrect values such as an *occasionally* incomplete
# LDLIBS.
MAKEFLAGS += --no-builtin-rules

CFLAGS = -Wall -O2 -I $(top_srcdir) $(EXTRA_CFLAGS) $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
CFLAGS += -Wunreachable-code
LDLIBS = -lrt -lpthread -lm

# Some distributions (such as Ubuntu) configure GCC so that _FORTIFY_SOURCE is
# automatically enabled at -O1 or above. This triggers various unused-result
# warnings where functions such as read() or write() are called and their
# return value is not checked. Disable _FORTIFY_SOURCE to silence those
# warnings.
CFLAGS += -U_FORTIFY_SOURCE

# PPPS (per-process page size) tests.  Those needing a kernel-side fixture get
# it from the modules in ppps_modules/, loaded by ../ppps/ppps_run_module.sh.
PPPS_TESTS := \
	access_phys_ppps \
	aio_ring_ppps anon_pageout_ppps anon_vma_name_ppps ashmem_ppps \
	anon_rmap_split_window_ppps \
	bpf_array_mmap_ppps bpf_ringbuf_mmap_ppps bpf_stack_build_id_ppps \
	btf_vmlinux_mmap_ppps \
	dma_heap_ppps dma_mmap_ppps dontneed_refault_ppps elf_core_ppps exec_env_ppps \
	dma_mmap_full_ppps \
	exec_user_args_ppps fadvise_dontneed_ppps fault_in_ppps \
	file_cow_ppps file_cow_refs_ppps filemap_eof_ppps folio_within_range_ppps \
	fork_filemap_ppps fork_page_size_ppps \
	futex_shared_ppps \
	gup_retry_ppps hole_anon_ppps \
	io_uring_mmap_ppps iommu_dma_mmap_ppps iov_iter_ppps kcov_ppps \
	madv_cold_large_file_ppps madvise_free_ppps madvise_guard_ppps \
	madvise_remove_ppps madvise_willneed_ppps mbind_ppps memfd_eof_ppps \
	memory_failure_anon_ppps mglru_folded_ppps mglru_ppps mincore_ppps mixed_context_switch_ppps \
	mixed_mm_access_ppps mlock_large_file_ppps mlock_onfault_ppps \
	mlock_partial_file_ppps mlock_reclaim_ppps \
	mmap_action_cleanup_ppps mmap_hint_ppps mmap_page_zero_ppps \
	mmu_notifier_ppps mremap_commit_ppps mremap_file_slice_ppps mremap_hint_ppps mremap_split_ppps \
	mremap_pgoff_ppps mremap_rlimit_ppps msync_offset_ppps munmap_ppps \
	mthp_native_anon_ppps \
	oom_anon_score_ppps oom_score_ppps oom_trace_ppps packed_anon_ppps \
	packed_swap_ppps page_range_ppps pagemap_exclusive_ppps pagemap_ioctl_ppps pagemap_scan_ppps pagewalk_ppps pci_mmap_ppps \
	perf_mmap_offset_ppps perf_page_size_ppps perf_phys_addr_ppps \
	print_vma_addr_ppps proc_pagewalk_ppps process_madvise_ppps \
	process_mrelease_ppps process_vm_access_ppps remap_file_pages_ppps \
	procfs_native_rmap_ppps \
	remap_pfn_cow_ppps remap_pfn_range_ppps rmap_file_referenced_ppps rss_stat_trace_ppps \
	remote_write_dontneed_ppps \
	rusage_ppps selinux_status_mmap_ppps \
	shmem_mmap_offset_ppps \
	shmem_quota_ppps shmem_swap_usage_ppps smaps_native_fallback_ppps smaps_page_size_ppps \
	smaps_pss_ppps \
	stack_guard_gap_ppps swap_header_ppps swap_pss_ppps swap_vma_readahead_ppps swapoff_ppps \
	statm_cross_geometry_ppps \
	sysv_shm_lock_ppps sysv_shm_ppps taskstats_ppps \
	tcp_zerocopy_align_ppps thp_mmap_offset_ppps \
	trace_ring_buffer_mmap_ppps truncate_ppps truncate_remote_ppps \
	userfaultfd_mixed_ppps userfaultfd_poison_remote_ppps uffd_move_retry_ppps \
	uffd_move_swapcache_lock_ppps \
	uffd_move_swapped_slice_ppps \
	userfaultfd_remap_ppps userfaultfd_remote_ppps \
	userfaultfd_shmem_ppps \
	userfaultfd_unregister_remote_ppps userfaultfd_wp_remote_ppps \
	uffd_sigbus_anon_ppps \
	v9fs_mmap_close_ppps vb2_mmap_ppps vb2_userptr_reject_ppps \
	vhost_dirty_log_ppps vm_iomap_memory_ppps \
	vma_range_slice_ppps \
	vm_map_pages_ppps vmalloc_align_ppps xdp_umem_ppps xdp_umem_race_ppps \
	vma_merge_file_slice_ppps \
	vma_merge_next_pgoff_ppps

PPPS_TESTS_arm64 := \
	brk_aslr_ppps brk_aslr_ppps_sample elf_brk_gap_ppps exec_aslr_ppps \
	elf_4k_align_ppps elf_4k_align_probe \
	exec_aslr_ppps_helper exec_aslr_ppps_interp exec_stack_ppps \
	exec_stack_random_ppps mmap_aslr_ppps mte_anon_ppps \
	shmem_thp_mmap_ppps stack_aslr_ppps uprobe_ppps

TEST_GEN_FILES = cow
TEST_GEN_FILES += compaction_test
TEST_GEN_FILES += gup_longterm
TEST_GEN_FILES += gup_test
TEST_GEN_FILES += hmm-tests
TEST_GEN_FILES += hugetlb-madvise
TEST_GEN_FILES += hugetlb-read-hwpoison
TEST_GEN_FILES += hugetlb-soft-offline
TEST_GEN_FILES += hugepage-mmap
TEST_GEN_FILES += hugepage-mremap
TEST_GEN_FILES += hugepage-shm
TEST_GEN_FILES += hugepage-vmemmap
TEST_GEN_FILES += khugepaged
TEST_GEN_FILES += madv_populate
TEST_GEN_FILES += map_fixed_noreplace
TEST_GEN_FILES += map_hugetlb
TEST_GEN_FILES += map_populate
ifneq (,$(filter $(ARCH),arm64 riscv riscv64 x86 x86_64 loongarch32 loongarch64))
TEST_GEN_FILES += memfd_secret
endif
TEST_GEN_FILES += migration
TEST_GEN_FILES += mkdirty
TEST_GEN_FILES += mlock-random-test
TEST_GEN_FILES += mlock2-tests
TEST_GEN_FILES += mrelease_test
TEST_GEN_FILES += mremap_dontunmap
TEST_GEN_FILES += mremap_test
TEST_GEN_FILES += mseal_test
TEST_GEN_FILES += on-fault-limit
TEST_GEN_FILES += pagemap_ioctl
TEST_GEN_FILES += pfnmap
TEST_GEN_FILES += process_madv
TEST_GEN_FILES += prctl_thp_disable
TEST_GEN_FILES += thuge-gen
TEST_GEN_FILES += transhuge-stress
TEST_GEN_FILES += uffd-stress
TEST_GEN_FILES += uffd-unit-tests
TEST_GEN_FILES += uffd-wp-mremap
TEST_GEN_FILES += split_huge_page_test
TEST_GEN_FILES += ksm_tests
TEST_GEN_FILES += ksm_functional_tests
TEST_GEN_FILES += mdwe_test
TEST_GEN_FILES += hugetlb_fault_after_madv
TEST_GEN_FILES += hugetlb_madv_vs_map
TEST_GEN_FILES += hugetlb_dio
TEST_GEN_FILES += droppable
TEST_GEN_FILES += guard-regions
TEST_GEN_FILES += merge
TEST_GEN_FILES += rmap
TEST_GEN_FILES += $(PPPS_TESTS)
ifeq ($(ARCH),arm64)
TEST_GEN_FILES += $(PPPS_TESTS_arm64)
endif

ifneq ($(ARCH),arm64)
TEST_GEN_FILES += soft-dirty
endif

ifeq ($(ARCH),x86_64)
CAN_BUILD_I386 := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_32bit_program.c -m32)
CAN_BUILD_X86_64 := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_64bit_program.c)
CAN_BUILD_WITH_NOPIE := $(shell ./../x86/check_cc.sh "$(CC)" ../x86/trivial_program.c -no-pie)

VMTARGETS := protection_keys
VMTARGETS += pkey_sighandler_tests
BINARIES_32 := $(VMTARGETS:%=%_32)
BINARIES_64 := $(VMTARGETS:%=%_64)

ifeq ($(CAN_BUILD_WITH_NOPIE),1)
CFLAGS += -no-pie
endif

ifeq ($(CAN_BUILD_I386),1)
TEST_GEN_FILES += $(BINARIES_32)
endif

ifeq ($(CAN_BUILD_X86_64),1)
TEST_GEN_FILES += $(BINARIES_64)
endif

else ifeq ($(ARCH),arm64)
TEST_GEN_FILES += protection_keys
TEST_GEN_FILES += pkey_sighandler_tests
else ifeq ($(ARCH),powerpc)
TEST_GEN_FILES += protection_keys
endif

ifneq (,$(filter $(ARCH),arm64 mips64 parisc64 powerpc riscv64 s390x sparc64 x86_64 s390))
TEST_GEN_FILES += va_high_addr_switch
ifneq ($(ARCH),riscv64)
TEST_GEN_FILES += virtual_address_range
endif
TEST_GEN_FILES += write_to_hugetlbfs
endif

TEST_PROGS := run_vmtests.sh vmalloc_align_ppps.sh
TEST_GEN_PROGS += uffd_copy_tuple_ppps
TEST_GEN_PROGS += mremap_slice_cow_ppps
TEST_GEN_PROGS += swap_slice_mremap_ppps

TEST_FILES := test_vmalloc.sh
TEST_FILES += test_hmm.sh
TEST_FILES += va_high_addr_switch.sh
TEST_FILES += charge_reserved_hugetlb.sh
TEST_FILES += hugetlb_reparenting_test.sh
TEST_FILES += iommu_dma_mmap_ppps.sh mmu_notifier_ppps.sh \
	      selinux_status_mmap_ppps.sh v9fs_mmap_close_ppps.sh \
	      ../ppps/ppps_run_module.sh

# required by charge_reserved_hugetlb.sh
TEST_FILES += write_hugetlb_memory.sh

TEST_GEN_MODS_DIR := ppps_modules

# Keep the upstream page_frag fixture alongside the PPPS module directory.
KDIR ?= /lib/modules/$(shell uname -r)/build
ifneq (,$(wildcard $(KDIR)/Module.symvers))
PAGE_FRAG_MODULE := page_frag/page_frag_test.ko
TEST_FILES += test_page_frag.sh $(PAGE_FRAG_MODULE)
else
PAGE_FRAG_WARNING = "missing Module.symvers, please have the kernel built first"
endif

include ../lib.mk

$(OUTPUT)/brk_aslr_ppps_sample: brk_aslr_ppps_sample.S
	$(CC) -nostdlib -static -no-pie $< -o $@

$(OUTPUT)/exec_aslr_ppps_helper: exec_aslr_ppps_helper.S
	$(CC) -nostdlib -pie -Wl,-e,_start \
		-Wl,--dynamic-linker=/proc/self/fd/3 -Wl,--build-id=none \
		-Wl,-z,max-page-size=16384 $< -o $@

$(OUTPUT)/exec_aslr_ppps_interp: exec_aslr_ppps_interp.S
	$(CC) -nostdlib -static-pie -Wl,-e,_start -Wl,--build-id=none \
		-Wl,-z,max-page-size=16384 $< -o $@

$(TEST_GEN_PROGS): vm_util.c thp_settings.c
$(TEST_GEN_FILES): vm_util.c thp_settings.c

$(OUTPUT)/uffd-stress: uffd-common.c
$(OUTPUT)/uffd-unit-tests: uffd-common.c
$(OUTPUT)/uffd-wp-mremap: uffd-common.c
$(OUTPUT)/protection_keys: pkey_util.c
$(OUTPUT)/pkey_sighandler_tests: pkey_util.c

# Headers a PPPS test shares with its fixture module or with the other tuple
# tests.  Declaring them per target (instead of in LOCAL_HDRS) keeps a header
# edit from rebuilding the whole directory; the target-specific LOCAL_HDRS
# keeps lib.mk's link rule from passing the header to the compiler.
define ppps_test_hdrs
$(OUTPUT)/$(1): LOCAL_HDRS += $(2)
$(OUTPUT)/$(1): $(2)
endef
$(eval $(call ppps_test_hdrs,fault_in_ppps,fault_in_ppps.h))
$(eval $(call ppps_test_hdrs,folio_within_range_ppps,folio_within_range_ppps.h))
$(eval $(call ppps_test_hdrs,gup_retry_ppps,gup_retry_ppps.h))
$(eval $(call ppps_test_hdrs,iov_iter_ppps,iov_iter_ppps.h))
$(eval $(call ppps_test_hdrs,page_range_ppps,iov_iter_ppps.h page_range_ppps.h))
$(eval $(call ppps_test_hdrs,file_cow_refs_ppps,iov_iter_ppps.h page_range_ppps.h))
$(eval $(call ppps_test_hdrs,mmu_notifier_ppps,mmu_notifier_ppps.h))
$(eval $(call ppps_test_hdrs,pagewalk_ppps,pagewalk_ppps.h))
$(eval $(call ppps_test_hdrs,vm_map_pages_ppps,vm_map_pages_ppps.h))
$(foreach t,mincore_ppps mlock_onfault_ppps, \
	$(eval $(call ppps_test_hdrs,$(t),../kselftest_ppps.h)))
$(foreach t,dontneed_refault_ppps file_cow_ppps hole_anon_ppps mte_anon_ppps packed_anon_ppps \
	    packed_swap_ppps uffd_copy_tuple_ppps uffd_move_retry_ppps \
	    remote_write_dontneed_ppps anon_rmap_split_window_ppps \
	    uffd_move_swapped_slice_ppps uffd_move_swapcache_lock_ppps, \
	$(eval $(call ppps_test_hdrs,$(t),ppps_tuple_test.h)))

ifeq ($(ARCH),x86_64)
BINARIES_32 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_32))
BINARIES_64 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_64))

$(BINARIES_32) $(BINARIES_64): pkey_util.c

define gen-target-rule-32
$(1) $(1)_32: $(OUTPUT)/$(1)_32
.PHONY: $(1) $(1)_32
endef

define gen-target-rule-64
$(1) $(1)_64: $(OUTPUT)/$(1)_64
.PHONY: $(1) $(1)_64
endef

ifeq ($(CAN_BUILD_I386),1)
$(BINARIES_32): CFLAGS += -m32 -mxsave
$(BINARIES_32): LDLIBS += -lrt -ldl -lm
$(BINARIES_32): $(OUTPUT)/%_32: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(notdir $^) $(LDLIBS) -o $@
$(foreach t,$(VMTARGETS),$(eval $(call gen-target-rule-32,$(t))))
endif

ifeq ($(CAN_BUILD_X86_64),1)
$(BINARIES_64): CFLAGS += -m64 -mxsave
$(BINARIES_64): LDLIBS += -lrt -ldl
$(BINARIES_64): $(OUTPUT)/%_64: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(notdir $^) $(LDLIBS) -o $@
$(foreach t,$(VMTARGETS),$(eval $(call gen-target-rule-64,$(t))))
endif

# x86_64 users should be encouraged to install 32-bit libraries
ifeq ($(CAN_BUILD_I386)$(CAN_BUILD_X86_64),01)
all: warn_32bit_failure

warn_32bit_failure:
	@echo "Warning: you seem to have a broken 32-bit build" 2>&1;		\
	echo  "environment. This will reduce test coverage of 64-bit" 2>&1;	\
	echo  "kernels. If you are using a Debian-like distribution," 2>&1;	\
	echo  "try:"; 2>&1;							\
	echo  "";								\
	echo  "  apt-get install gcc-multilib libc6-i386 libc6-dev-i386";	\
	echo  "";								\
	echo  "If you are using a Fedora-like distribution, try:";		\
	echo  "";								\
	echo  "  yum install glibc-devel.*i686";				\
	exit 0;
endif
endif

# IOURING_EXTRA_LIBS may get set in local_config.mk, or it may be left empty.
$(OUTPUT)/cow: LDLIBS += $(IOURING_EXTRA_LIBS)

# Use one coherent set of libc UAPI headers for the taskstats netlink ABI.
$(OUTPUT)/taskstats_ppps: CFLAGS := -Wall -O2 $(EXTRA_CFLAGS)
$(OUTPUT)/taskstats_ppps: CFLAGS += -Wunreachable-code -U_FORTIFY_SOURCE
$(OUTPUT)/taskstats_ppps: CFLAGS += -I$(selfdir)

$(OUTPUT)/vb2_mmap_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/vb2_userptr_reject_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/anon_vma_name_ppps $(OUTPUT)/iov_iter_ppps \
$(OUTPUT)/mremap_pgoff_ppps: LDFLAGS += -pie -Wl,-z,max-page-size=16384
$(OUTPUT)/pagemap_ioctl \
$(OUTPUT)/pagemap_ioctl_ppps $(OUTPUT)/pagemap_scan_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/process_madvise_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/exec_aslr_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/fork_filemap_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/io_uring_mmap_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/munmap_ppps: LDFLAGS += -Wl,-z,max-page-size=16384
$(OUTPUT)/ashmem_ppps: LDFLAGS += -Wl,-z,max-page-size=16384

$(OUTPUT)/gup_longterm: LDLIBS += $(IOURING_EXTRA_LIBS)

$(OUTPUT)/mlock-random-test $(OUTPUT)/memfd_secret: LDLIBS += -lcap

$(OUTPUT)/ksm_tests: LDLIBS += -lnuma

$(OUTPUT)/migration: LDLIBS += -lnuma

$(OUTPUT)/rmap: LDLIBS += -lnuma
$(OUTPUT)/elf_4k_align_probe: elf_4k_align_probe.S
	$(CC) -nostdlib -static -Wl,-no-pie -Wl,--build-id=none \
		-Wl,-z,max-page-size=4096 $< -o $@

local_config.mk local_config.h: check_config.sh
	/bin/sh ./check_config.sh $(CC)

EXTRA_CLEAN += local_config.mk local_config.h

ifeq ($(IOURING_EXTRA_LIBS),)
all: warn_missing_liburing

warn_missing_liburing:
	@echo ; \
	echo "Warning: missing liburing support. Some tests will be skipped." ; \
	echo
endif

ifneq ($(PAGE_FRAG_WARNING),)
all: warn_missing_page_frag

warn_missing_page_frag:
	@echo ; \
	echo "Warning: $(PAGE_FRAG_WARNING). page_frag test will be skipped." ; \
	echo
endif

ifneq ($(PAGE_FRAG_MODULE),)
all: $(PAGE_FRAG_MODULE)
$(PAGE_FRAG_MODULE): page_frag/page_frag_test.c page_frag/Makefile
	+$(MAKE) -C page_frag KDIR=$(KDIR)
.PHONY: clean_page_frag_module
clean_page_frag_module:
	+$(MAKE) -C page_frag KDIR=$(KDIR) clean
clean: clean_page_frag_module
endif
