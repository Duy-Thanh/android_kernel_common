// SPDX-License-Identifier: GPL-2.0
#include <asm/kvm_pkvm.h>
#include <asm/pkvm_spinlock.h>
#include "pkvm.h"
#include "mmu.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pkvm_hyp.h>
#include <vmx/pkvm/hyp/pgtable.h>
#include <vmx/pkvm/hyp/gfp.h>
#include <vmx/pkvm/hyp/ept.h>	//FIXME
#include <vmx/pkvm/hyp/mem_protect.h>
#include <vmx/pkvm/hyp/memory.h>
#include <pkvm.h>

struct pkvm_pgtable_ops guest_mmu_ops;
struct pkvm_pgtable_cap guest_mmu_cap;

DECLARE_PER_CPU(struct pkvm_vm *, __current_vm);
#define current_vm (*this_cpu_ptr(&__current_vm))

static void *guest_mmu_zalloc_page(void *mc)
{
	struct hyp_page *p;
	void *page;

	page = hyp_alloc_pages(&current_vm->pool, 0);
	if (page)
		return page;

	page = pop_pkvm_memcache(mc, hyp_phys_to_virt);
	if (!page)
		return page;

	memset(page, 0, PAGE_SIZE);
	p = hyp_virt_to_page(page);
	hyp_set_page_refcounted(p);

	return page;
}

static void guest_mmu_get_page(void *vaddr)
{
	hyp_get_page(&current_vm->pool, vaddr);
}

static void guest_mmu_put_page(void *vaddr)
{
	hyp_put_page(&current_vm->pool, vaddr);
}

static void guest_mmu_flush_tlb(struct pkvm_pgtable *pgt,
				unsigned long addr,
				unsigned long size)
{
	struct pkvm_vm *pkvm_vm = pgt_to_pkvm(pgt);
	int i;

	pkvm_spin_lock(&pkvm_vm->lock);

	for (i = 0; i < to_kvm(pkvm_vm)->created_vcpus; i++) {
		struct pkvm_vcpu *pkvm_vcpu;
		struct kvm_vcpu *vcpu;

		pkvm_vcpu = pkvm_vm->vcpus[i];
		if (WARN_ON_ONCE(!pkvm_vcpu))
			continue;

		vcpu = to_kvm_vcpu(pkvm_vcpu);

		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		pkvm_kick_vcpu(vcpu);
	}

	pkvm_spin_unlock(&pkvm_vm->lock);
}

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm, void *pgd)
{
	unsigned long nr_pages;
	int ret;

	nr_pages = 1;
	ret = hyp_pool_init(&pkvm_vm->pool, hyp_virt_to_pfn(pgd), nr_pages, 0);
	if (ret)
		return ret;

	pkvm_vm->pgt_mm_ops = (struct pkvm_mm_ops) {
		.phys_to_virt = pkvm_phys_to_virt,
		.virt_to_phys = pkvm_virt_to_phys,
		.zalloc_page = guest_mmu_zalloc_page,
		.get_page = guest_mmu_get_page,
		.put_page = guest_mmu_put_page,
		.page_count = hyp_page_count,
		.flush_tlb = guest_mmu_flush_tlb,
		.flush_cache = NULL,
	};
	pkvm_vm->pgt_lock = __PKVM_SPINLOCK_UNLOCKED;

	guest_pgt_lock(pkvm_vm);
	ret = pkvm_pgtable_init(&pkvm_vm->pgt, &pkvm_vm->pgt_mm_ops, &guest_mmu_ops,
				 &guest_mmu_cap, true);
	guest_pgt_unlock(pkvm_vm);

	return ret;
}

static bool range_has_pvmfw(struct kvm *kvm, u64 gpa_start, u64 gpa_end)
{
	struct kvm_protected_vm *pkvm = &kvm->arch.pkvm;
	u64 pvmfw_load_end = pkvm->pvmfw_load_addr + pvmfw_size;

	if (!pvmfw_present)
		return false;

	if (pkvm->pvmfw_load_addr == INVALID_GPA)
		return false;

	return gpa_end > pkvm->pvmfw_load_addr && gpa_start < pvmfw_load_end;
}

static int load_pvmfw_pages(struct kvm *kvm, u64 gpa, u64 phys, u64 size)
{
	u64 offset = gpa - kvm->arch.pkvm.pvmfw_load_addr;

	if (offset >= pvmfw_size)
		return -EINVAL;

	size = min(size, pvmfw_size - offset);
	if (!PAGE_ALIGNED(size) || !PAGE_ALIGNED(offset))
		return -EINVAL;

	memcpy(__pkvm_va(phys), __pkvm_va(pvmfw_base + offset), size);
	return 0;
}

static int guest_mmu_map_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
			      void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	struct pkvm_pgtable_map_data *data = arg;
	u64 size = pgt->pgt_ops->pgt_level_to_size(level);
	struct kvm *kvm = pgt_to_kvm(pgt);
	int ret;

	if (pgt->pgt_ops->pgt_entry_present(ptep)) {
		/*
		 * If the host wants to map the gpa to a different hpa,
		 * it should unmap it first.
		 * FIXME: we should also check if the host is trying to change
		 * permissions of a mapped page.
		 */
		if (pgt->pgt_ops->pgt_entry_to_phys(ptep) != data->phys)
			return -EBUSY;

		/*
		 * It is possible that another CPU has just created the same mapping
		 * when multiple CPUs touch the same page simultaneously.
		 * For simplicity check this on pKVM side, since the host doesn't
		 * track guest mappings in any data structure yet.
		 */
		return -EEXIST;
	}

	/*
	 * TODO: use a more suitable API than the existing page state API. Why walk
	 * the page table once again to reach this PTE if we are already at it?
	 * We could combine these 2 layers (MMU and page state API) into one layer.
	 */
	if (pkvm_is_protected_vm(kvm)) {
		ret = __pkvm_host_donate_guest(data->phys, pgt, vaddr, size,
					       data->prot, data->memcache);
		if (ret)
			return ret;

		if (range_has_pvmfw(kvm, vaddr, vaddr + size)) {
			ret = load_pvmfw_pages(kvm, vaddr, data->phys, size);
			WARN_ON_ONCE(ret);
		}
	} else {
		ret = __pkvm_host_share_guest(data->phys, pgt, vaddr, size,
					      data->prot, data->memcache);
	}

	return ret;
}
static void *admit_host_page(void *arg, unsigned long order)
{
	phys_addr_t p;
	struct pkvm_memcache *host_mc = arg;

	if (!host_mc->nr_pages)
		return NULL;

	/* Don't expect memcache to have higher order pages. */
	WARN_ON(order);

	p = host_mc->head & PAGE_MASK;
	/*
	 * The host still owns the pages in its memcache, so we need to go
	 * through host-to-hyp donation cycle to change it.
	 */
	if (__pkvm_host_donate_hyp(p, PAGE_SIZE)) {
		WARN_ON(1);
		return NULL;
	}

	return pop_pkvm_memcache(host_mc, hyp_phys_to_virt);
}

/* Refill our local memcache by popping pages from the one provided by the host. */
static int refill_memcache(struct pkvm_memcache *mc, unsigned long min_pages,
		    struct pkvm_memcache *host_mc)
{
	struct pkvm_memcache tmp = *host_mc;
	int ret;

	ret =  __topup_pkvm_memcache(mc, min_pages, admit_host_page,
				     hyp_virt_to_phys, &tmp, 0);
	*host_mc = tmp;

	return ret;
}

int pkvm_refill_memcache(struct pkvm_vcpu *pkvm_vcpu)
{
	struct kvm_vcpu *vcpu = to_kvm_vcpu(pkvm_vcpu);

	return refill_memcache(&vcpu->arch.stage2_mc,
			       pkvm_vcpu->shared_vcpu->arch.stage2_mc.nr_pages,
			       &pkvm_vcpu->shared_vcpu->arch.stage2_mc);
}

int pkvm_vm_mmu_map(struct kvm_vcpu *shared_vcpu, u64 gpa, u64 hpa, u64 size, bool writable)
{
	struct pkvm_vcpu *pkvm_vcpu;
	struct kvm_vcpu *vcpu;
	struct pkvm_vm *pkvm_vm;
	u64 prot;
	int ret;

	pkvm_vcpu = get_pkvm_vcpu_via_shared(shared_vcpu);
	if (!pkvm_vcpu)
		return -EINVAL;

	pkvm_vm = pkvm_vcpu->pkvm_vm;
	vcpu = to_kvm_vcpu(pkvm_vcpu);

	if (!writable && pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		ret = -EPERM;
		goto put_pkvm_vcpu;
	}

	/* permission bits */
	prot = pkvm_vm->pgt.pgt_ops->pgt_entry_calc_perm(true, writable, true);
	/* memory type bits */
	prot |= kvm_x86_call(get_mt_mask)(to_kvm_vcpu(pkvm_vcpu), gpa >> PAGE_SHIFT, false);

	guest_pgt_lock(pkvm_vm);

	/* Top-up our per-vcpu memcache from the host's */
	ret = pkvm_refill_memcache(pkvm_vcpu);
	if (ret)
		return ret;

	ret = pkvm_pgtable_map(&pkvm_vm->pgt, gpa, hpa, size, 0, prot,
			       guest_mmu_map_leaf, &vcpu->arch.stage2_mc);
	guest_pgt_unlock(pkvm_vm);

put_pkvm_vcpu:
	put_pkvm_vcpu(pkvm_vcpu);
	return ret;
}

static int guest_mmu_unmap_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
				void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	unsigned long phys = pgt->pgt_ops->pgt_entry_to_phys(ptep);
	unsigned long size = pgt->pgt_ops->pgt_level_to_size(level);
	int ret;

	if (WARN_ON_ONCE(!pgt->pgt_ops->pgt_entry_present(ptep)))
		return 0;

	pgt->mm_ops->get_page(ptep);
	ret = __pkvm_host_unshare_guest(phys, pgt, vaddr, size);
	pgt->mm_ops->put_page(ptep);

	flush_data->flushtlb = true;

	return ret;
}

int pkvm_vm_mmu_unmap(int vm_handle, u64 gpa, u64 size)
{
	struct pkvm_vm *pkvm_vm;
	int ret;

	pkvm_vm = get_pkvm_vm(vm_handle);
	if (!pkvm_vm)
		return -EINVAL;

	if (pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		ret = -EPERM;
		goto put_pkvm_vm;
	}

	guest_pgt_lock(pkvm_vm);
	ret = pkvm_pgtable_unmap(&pkvm_vm->pgt, gpa, size, guest_mmu_unmap_leaf);
	guest_pgt_unlock(pkvm_vm);

put_pkvm_vm:
	put_pkvm_vm(pkvm_vm);
	return ret;
}

int pkvm_vm_mmu_age(int vm_handle, u64 gpa, u64 size, bool mkold)
{
	struct pkvm_vm *pkvm_vm;
	int ret;

	pkvm_vm = get_pkvm_vm(vm_handle);
	if (!pkvm_vm)
		return -EINVAL;

	if (pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		ret = -EPERM;
		goto put_pkvm_vm;
	}

	pkvm_spin_lock(&pkvm_vm->pgt_lock);
	ret = pkvm_pgtable_test_clear_young(&pkvm_vm->pgt, gpa, size, mkold);
	pkvm_spin_unlock(&pkvm_vm->pgt_lock);

	/*
	 * Do not flush TLB. It will be flushed by the MMU notifier in KVM-high
	 * if needed.
	 */

put_pkvm_vm:
	put_pkvm_vm(pkvm_vm);
	return ret;
}

static int guest_mmu_free_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
			       void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	unsigned long phys = pgt->pgt_ops->pgt_entry_to_phys(ptep);
	unsigned long size = pgt->pgt_ops->pgt_level_to_size(level);
	struct kvm *kvm = pgt_to_kvm(pgt);

	if (!pgt->pgt_ops->pgt_entry_present(ptep)) {
		/* Guest may only share its pages, not donate them. */
		BUG_ON(pgt->pgt_ops->pgt_entry_mapped(ptep));

		return 0;
	}

	/*
	 * The pgtable_free_cb in this current page walker is still walking
	 * the page table so we cannot allow __pkvm_host_unshare_guest()
	 * or __pkvm_host_undonate_guest() to release the page table pages.
	 * So we shall get_page before calling these APIs, then put_page
	 * to let pgtable_free_cb free table pages with correct refcount.
	 */
	if (pkvm_is_protected_vm(kvm)) {
		void *virt = pgt->mm_ops->phys_to_virt(phys);

		/*
		 * Wipe the protected VM memory page before giving it back
		 * to host, to avoid secrets leakage.
		 */
		memset(virt, 0, size);
		pkvm_clflush_cache_range(virt, size);

		pgt->mm_ops->get_page(ptep);
		BUG_ON(__pkvm_host_undonate_guest(phys, pgt, vaddr, size));
		pgt->mm_ops->put_page(ptep);
	} else {
		pgt->mm_ops->get_page(ptep);
		BUG_ON(__pkvm_host_unshare_guest(phys, pgt, vaddr, size));
		pgt->mm_ops->put_page(ptep);
	}

	return 0;
}

void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm)
{
	/* vCPUs are already torn down, no need to flush TLBs. */
	pkvm_vm->pgt.mm_ops->flush_tlb = NULL;

	guest_pgt_lock(pkvm_vm);
	pkvm_pgtable_destroy(&pkvm_vm->pgt, guest_mmu_free_leaf);
	guest_pgt_unlock(pkvm_vm);
}
