// SPDX-License-Identifier: GPL-2.0-only
/*
 * FF-A v1.0 proxy to filter out invalid memory-sharing SMC calls issued by
 * the host. FF-A is a slightly more palatable abbreviation of "Arm Firmware
 * Framework for Arm A-profile", which is specified by Arm in document
 * number DEN0077.
 *
 * Copyright (C) 2022 - Google LLC
 * Author: Andrew Walbran <qwandor@google.com>
 *
 * This driver hooks into the SMC trapping logic for the host and intercepts
 * all calls falling within the FF-A range. Each call is either:
 *
 *	- Forwarded on unmodified to the SPMD at EL3
 *	- Rejected as "unsupported"
 *	- Accompanied by a host stage-2 page-table check/update and reissued
 *
 * Consequently, any attempts by the host to make guest memory pages
 * accessible to the secure world using FF-A will be detected either here
 * (in the case that the memory is already owned by the guest) or during
 * donation to the guest (in the case that the memory was previously shared
 * with the secure world).
 *
 * To allow the rolling-back of page-table updates and FF-A calls in the
 * event of failure, operations involving the RXTX buffers are locked for
 * the duration and are therefore serialised.
 */

#include <linux/arm_ffa.h>
#include <asm/kvm_pkvm.h>
#include <kvm/arm_hypercalls.h>

#include <nvhe/arm-smccc.h>
#include <nvhe/alloc.h>
#include <nvhe/ffa.h>
#include <nvhe/mem_protect.h>
#include <nvhe/memory.h>
#include <nvhe/trap_handler.h>
#include <nvhe/spinlock.h>

/*
 * "ID value 0 must be returned at the Non-secure physical FF-A instance"
 * We share this ID with the host.
 */
#define HOST_FFA_ID	0

/* FF-A VM handle - 0 is reserved for the host */
#define VM_FFA_HANDLE_FROM_VCPU(vcpu)	(((vcpu)->kvm->arch.pkvm.handle) - HANDLE_OFFSET + 1)

#define VM_FFA_SUPPORTED(vcpu)		((vcpu)->kvm->arch.pkvm.ffa_support)

/* The maximum number of secure partitions that can register for VM availability */
#define FFA_MAX_REGISTERED_SP_IDS	(8)

/*
 * A buffer to hold the maximum descriptor size we can see from the host,
 * which is required when the SPMD returns a fragmented FFA_MEM_RETRIEVE_RESP
 * when resolving the handle on the reclaim path.
 */
struct kvm_ffa_descriptor_buffer {
	void	*buf;
	size_t	len;
};

static struct kvm_ffa_descriptor_buffer ffa_desc_buf;

struct kvm_ffa_buffers {
	hyp_spinlock_t lock;
	void *tx;
	u64 tx_ipa;
	void *rx;
	u64 rx_ipa;
	struct list_head xfer_list;
};

struct ffa_translation {
	struct list_head node;
	u64 ipa;
	phys_addr_t pa;
};

struct ffa_mem_transfer {
	struct list_head node;
	u64 ffa_handle;
	struct list_head translations;
};

/*
 * Note that we don't currently lock these buffers explicitly, instead
 * relying on the locking of the hyp FFA buffers.
 */
static struct kvm_ffa_buffers hyp_buffers;

/* Endpoint buffers (or partition buffers per FF-A naming) */
static struct kvm_ffa_buffers endp_buffers[KVM_MAX_PVMS];
static u32 hyp_ffa_version;
static bool has_version_negotiated;
static hyp_spinlock_t version_lock;
static unsigned short hyp_buff_refcnt;

/* Secure partitions that can receive VM availability messages */
static u16 sp_ids[FFA_MAX_REGISTERED_SP_IDS];
static u8 num_registered_sp_ids;

static void ffa_to_smccc_error(struct arm_smccc_res *res, u64 ffa_errno)
{
	*res = (struct arm_smccc_res) {
		.a0	= FFA_ERROR,
		.a2	= ffa_errno,
	};
}

static void ffa_to_smccc_res_prop(struct arm_smccc_res *res, int ret, u64 prop)
{
	if (ret == FFA_RET_SUCCESS) {
		*res = (struct arm_smccc_res) { .a0 = FFA_SUCCESS,
						.a2 = prop };
	} else {
		ffa_to_smccc_error(res, ret);
	}
}

static void ffa_to_smccc_res(struct arm_smccc_res *res, int ret)
{
	ffa_to_smccc_res_prop(res, ret, 0);
}

static void ffa_set_retval(struct kvm_cpu_context *ctxt,
			   struct arm_smccc_res *res)
{
	cpu_reg(ctxt, 0) = res->a0;
	cpu_reg(ctxt, 1) = res->a1;
	cpu_reg(ctxt, 2) = res->a2;
	cpu_reg(ctxt, 3) = res->a3;
}

static bool is_ffa_call(u64 func_id)
{
	return ARM_SMCCC_IS_FAST_CALL(func_id) &&
	       ARM_SMCCC_OWNER_NUM(func_id) == ARM_SMCCC_OWNER_STANDARD &&
	       ARM_SMCCC_FUNC_NUM(func_id) >= FFA_MIN_FUNC_NUM &&
	       ARM_SMCCC_FUNC_NUM(func_id) <= FFA_MAX_FUNC_NUM;
}

static int ffa_map_hyp_buffers(u64 ffa_page_count)
{
	struct arm_smccc_res res;

	if (hyp_refcount_get(hyp_buff_refcnt) == USHRT_MAX)
		return FFA_RET_BUSY;
	else if (hyp_refcount_inc(hyp_buff_refcnt) > 1)
		return FFA_RET_SUCCESS;

	arm_smccc_1_1_smc(FFA_FN64_RXTX_MAP,
			  hyp_virt_to_phys(hyp_buffers.tx),
			  hyp_virt_to_phys(hyp_buffers.rx),
			  ffa_page_count,
			  0, 0, 0, 0,
			  &res);

	return res.a0 == FFA_SUCCESS ? FFA_RET_SUCCESS : res.a2;
}

static int ffa_unmap_hyp_buffers(void)
{
	struct arm_smccc_res res;

	/* Unmap the buffers from the spmd only when no one references them */
	if (hyp_refcount_dec(hyp_buff_refcnt) != 0)
		return FFA_RET_SUCCESS;

	arm_smccc_1_1_smc(FFA_RXTX_UNMAP,
			  HOST_FFA_ID,
			  0, 0, 0, 0, 0, 0,
			  &res);

	return res.a0 == FFA_SUCCESS ? FFA_RET_SUCCESS : res.a2;
}

static void ffa_mem_frag_tx(struct arm_smccc_res *res, u32 handle_lo,
			     u32 handle_hi, u32 fraglen, u32 endpoint_id)
{
	arm_smccc_1_1_smc(FFA_MEM_FRAG_TX,
			  handle_lo, handle_hi, fraglen, endpoint_id,
			  0, 0, 0,
			  res);
}

static void ffa_mem_frag_rx(struct arm_smccc_res *res, u32 handle_lo,
			     u32 handle_hi, u32 fragoff)
{
	arm_smccc_1_1_smc(FFA_MEM_FRAG_RX,
			  handle_lo, handle_hi, fragoff, HOST_FFA_ID,
			  0, 0, 0,
			  res);
}

static void ffa_mem_xfer(struct arm_smccc_res *res, u64 func_id, u32 len,
			  u32 fraglen)
{
	arm_smccc_1_1_smc(func_id, len, fraglen,
			  0, 0, 0, 0, 0,
			  res);
}

static void ffa_mem_reclaim(struct arm_smccc_res *res, u32 handle_lo,
			     u32 handle_hi, u32 flags)
{
	arm_smccc_1_1_smc(FFA_MEM_RECLAIM,
			  handle_lo, handle_hi, flags,
			  0, 0, 0, 0,
			  res);
}

static void ffa_retrieve_req(struct arm_smccc_res *res, u32 len)
{
	arm_smccc_1_1_smc(FFA_FN64_MEM_RETRIEVE_REQ,
			  len, len,
			  0, 0, 0, 0, 0,
			  res);
}

static void ffa_rx_release(struct arm_smccc_res *res)
{
	arm_smccc_1_1_smc(FFA_RX_RELEASE,
			  0, 0,
			  0, 0, 0, 0, 0,
			  res);
}

static int ffa_guest_share_with_cb(struct pkvm_hyp_vcpu *vcpu,
				   int (*share_cb)(struct pkvm_hyp_vcpu *, u64, u64 *),
				   phys_addr_t guest_ipa, void **out_addr, u64 *exit_code)
{
	int ret = share_cb(vcpu, guest_ipa, (u64 *)out_addr);

	if (ret == -EFAULT)
		*exit_code = __pkvm_memshare_page_req(vcpu, guest_ipa);
	else if (ret == -ENOMEM)
		pkvm_handle_empty_memcache(vcpu, exit_code);

	return ret;
}

static void *ffa_alloc(size_t size, struct pkvm_hyp_vcpu *vcpu, u64 *exit_code)
{
	void *buf;
	struct kvm_hyp_req *req;

	buf = hyp_alloc(size);
	if (!buf) {
		BUG_ON(hyp_alloc_errno() != -ENOMEM);
		req = pkvm_hyp_req_reserve(vcpu, KVM_HYP_REQ_TYPE_MEM);
		if (!req)
			return ERR_PTR(-ENOMEM);

		req->mem.dest = REQ_MEM_DEST_HYP_ALLOC;
		req->mem.nr_pages = hyp_alloc_missing_donations();

		write_sysreg_el2(read_sysreg_el2(SYS_ELR) - 4, SYS_ELR);

		*exit_code = ARM_EXCEPTION_HYP_REQ;

		return ERR_PTR(-ENOMEM);
	}

	return buf;
}

static int ffa_map_guest_buffers(void **hyp_tx_va, void **hyp_rx_va, struct kvm_cpu_context *ctxt,
				 u64 *exit_code)
{
	struct pkvm_hyp_vcpu *vcpu = PKVM_VCPU_FROM_CTXT(ctxt);
	int ret;

	DECLARE_REG(phys_addr_t, tx_ipa, ctxt, 1);
	DECLARE_REG(phys_addr_t, rx_ipa, ctxt, 2);

	ret = ffa_guest_share_with_cb(vcpu, __pkvm_guest_share_hyp, tx_ipa, hyp_tx_va, exit_code);
	if (ret)
		return ret;

	ret = ffa_guest_share_with_cb(vcpu, __pkvm_guest_share_hyp, rx_ipa, hyp_rx_va, exit_code);
	if (ret)
		goto err_unshare_tx;

	ret = hyp_pin_shared_guest_page(vcpu, tx_ipa, *hyp_tx_va);
	if (ret)
		goto err_unshare_rx;

	ret = hyp_pin_shared_guest_page(vcpu, rx_ipa, *hyp_rx_va);
	if (ret)
		goto err_unpin_tx;

	return 0;
err_unshare_tx:
	WARN_ON(__pkvm_guest_unshare_hyp(vcpu, tx_ipa));
err_unshare_rx:
	WARN_ON(__pkvm_guest_unshare_hyp(vcpu, rx_ipa));
err_unpin_tx:
	hyp_unpin_shared_guest_page(vcpu, *hyp_tx_va);
	return ret;
}

static int ffa_map_host_buffers(void **tx_virt, void **rx_virt, phys_addr_t tx,
				phys_addr_t rx)
{
	int ret;

	ret = __pkvm_host_share_hyp(hyp_phys_to_pfn(tx));
	if (ret)
		return FFA_RET_INVALID_PARAMETERS;

	ret = __pkvm_host_share_hyp(hyp_phys_to_pfn(rx));
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unshare_tx;
	}

	*tx_virt = hyp_phys_to_virt(tx);
	ret = hyp_pin_shared_mem(*tx_virt, *tx_virt + 1);
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unshare_rx;
	}

	*rx_virt = hyp_phys_to_virt(rx);
	ret = hyp_pin_shared_mem(*rx_virt, *rx_virt + 1);
	if (ret) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto err_unpin_tx;
	}

	return 0;
err_unpin_tx:
	hyp_unpin_shared_mem(*tx_virt, *tx_virt + 1);
err_unshare_rx:
	__pkvm_host_unshare_hyp(hyp_phys_to_pfn(rx));
err_unshare_tx:
	__pkvm_host_unshare_hyp(hyp_phys_to_pfn(tx));
	return ret;
}

static int kvm_notify_vm_availability(uint16_t vm_handle, u32 availability_msg)
{
	int i;
	struct arm_smccc_res res;

	if (!num_registered_sp_ids)
		return FFA_RET_SUCCESS;

	for (i = 0; i < num_registered_sp_ids; i++) {
		arm_smccc_1_1_smc(FFA_MSG_SEND_DIRECT_REQ, sp_ids[i], availability_msg,
				  0, 0, vm_handle, 0, 0, &res);
		if (res.a0 != FFA_MSG_SEND_DIRECT_RESP)
			return FFA_RET_INVALID_PARAMETERS;

		if (res.a3 != FFA_RET_SUCCESS)
			return res.a3;
	}

	return FFA_RET_SUCCESS;
}

static int do_ffa_rxtx_map(struct arm_smccc_res *res,
			   struct kvm_cpu_context *ctxt,
			   unsigned int vm_handle,
			   u64 *exit_code)
{
	DECLARE_REG(phys_addr_t, tx, ctxt, 1);
	DECLARE_REG(phys_addr_t, rx, ctxt, 2);
	DECLARE_REG(u32, npages, ctxt, 3);
	int ret = 0;
	void *rx_virt, *tx_virt;

	if (npages != (KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) / FFA_PAGE_SIZE) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (!PAGE_ALIGNED(tx) || !PAGE_ALIGNED(rx)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	ret = kvm_notify_vm_availability(vm_handle, FFA_VM_CREATION_MSG);
	if (ret != FFA_RET_SUCCESS)
		goto out;

	hyp_spin_lock(&hyp_buffers.lock);
	if (endp_buffers[vm_handle].tx) {
		ret = FFA_RET_DENIED;
		goto out_unlock;
	}

	/*
	 * Map our hypervisor buffers into the SPMD before mapping and
	 * pinning the host buffers in our own address space.
	 */
	ret = ffa_map_hyp_buffers(npages);
	if (ret)
		goto out_unlock;

	if (!vm_handle)
		ret = ffa_map_host_buffers(&tx_virt, &rx_virt, tx, rx);
	else
		ret = ffa_map_guest_buffers(&tx_virt, &rx_virt, ctxt, exit_code);
	if (ret)
		goto err_unmap;

	endp_buffers[vm_handle].tx = tx_virt;
	endp_buffers[vm_handle].rx = rx_virt;
	endp_buffers[vm_handle].tx_ipa = tx;
	endp_buffers[vm_handle].rx_ipa = rx;

out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);
out:
	ffa_to_smccc_res(res, ret);
	return ret;
err_unmap:
	ffa_unmap_hyp_buffers();
	goto out_unlock;
}

static void do_ffa_rxtx_unmap(struct arm_smccc_res *res,
			      struct kvm_cpu_context *ctxt,
			      unsigned int vm_handle)
{
	DECLARE_REG(u32, id, ctxt, 1);
	int ret = 0;
	struct pkvm_hyp_vcpu *pkvm_vcpu;

	if (id != HOST_FFA_ID) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	hyp_spin_lock(&hyp_buffers.lock);
	if (!endp_buffers[vm_handle].tx) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (vm_handle == HOST_FFA_ID) {
		hyp_unpin_shared_mem(endp_buffers[vm_handle].tx,
				     endp_buffers[vm_handle].tx + 1);
		WARN_ON(__pkvm_host_unshare_hyp(hyp_virt_to_pfn(endp_buffers[vm_handle].tx)));

		hyp_unpin_shared_mem(endp_buffers[vm_handle].rx,
				     endp_buffers[vm_handle].rx + 1);
		WARN_ON(__pkvm_host_unshare_hyp(hyp_virt_to_pfn(endp_buffers[vm_handle].rx)));
	} else {
		pkvm_vcpu = PKVM_VCPU_FROM_CTXT(ctxt);
		hyp_unpin_shared_guest_page(pkvm_vcpu, endp_buffers[vm_handle].tx);
		WARN_ON(__pkvm_guest_unshare_hyp(pkvm_vcpu, endp_buffers[vm_handle].tx_ipa));
		hyp_unpin_shared_guest_page(pkvm_vcpu, endp_buffers[vm_handle].rx);
		WARN_ON(__pkvm_guest_unshare_hyp(pkvm_vcpu, endp_buffers[vm_handle].rx_ipa));
	}

	endp_buffers[vm_handle].rx = NULL;
	endp_buffers[vm_handle].tx = NULL;

	ffa_unmap_hyp_buffers();

out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);
out:
	ffa_to_smccc_res(res, ret);
}

static u32 __ffa_host_share_ranges(struct ffa_mem_region_addr_range *ranges,
				   u32 nranges)
{
	u32 i;

	for (i = 0; i < nranges; ++i) {
		struct ffa_mem_region_addr_range *range = &ranges[i];
		u64 sz = (u64)range->pg_cnt * FFA_PAGE_SIZE;
		u64 pfn = hyp_phys_to_pfn(range->address);

		if (!PAGE_ALIGNED(sz))
			break;

		if (__pkvm_host_share_ffa(pfn, sz / PAGE_SIZE))
			break;
	}

	return i;
}

static u32 __ffa_host_unshare_ranges(struct ffa_mem_region_addr_range *ranges,
				     u32 nranges)
{
	u32 i;

	for (i = 0; i < nranges; ++i) {
		struct ffa_mem_region_addr_range *range = &ranges[i];
		u64 sz = (u64)range->pg_cnt * FFA_PAGE_SIZE;
		u64 pfn = hyp_phys_to_pfn(range->address);

		if (!PAGE_ALIGNED(sz))
			break;

		if (__pkvm_host_unshare_ffa(pfn, sz / PAGE_SIZE))
			break;
	}

	return i;
}

static int ffa_store_translation(struct ffa_mem_transfer *transfer,
				 u64 ipa, phys_addr_t pa, struct pkvm_hyp_vcpu *vcpu,
				 u64 *exit_code)
{
	struct ffa_translation *tr = ffa_alloc(sizeof(struct ffa_translation), vcpu, exit_code);
	if (IS_ERR(tr))
		return PTR_ERR(tr);

	tr->ipa = ipa;
	tr->pa = pa;
	list_add(&tr->node, &transfer->translations);

	return 0;
}

static struct ffa_translation *ffa_find_translation(struct ffa_mem_transfer *transfer,
						    phys_addr_t pa)
{
	struct ffa_translation *translation;

	list_for_each_entry(translation, &transfer->translations, node) {
		if (translation->pa == pa)
			return translation;
	}

	return NULL;
}

static int ffa_guest_unshare_ranges(struct ffa_mem_region_addr_range *ranges,
				    u32 nranges, struct pkvm_hyp_vcpu *vcpu,
				    struct ffa_mem_transfer *transfer)
{
	struct ffa_translation *translation;
	struct ffa_mem_region_addr_range *range;
	int i;

	for (i = 0; i < nranges; i++) {
		range = &ranges[i];
		translation = ffa_find_translation(transfer, range->address);

		WARN_ON(!translation);
		WARN_ON(__pkvm_guest_unshare_ffa(vcpu, translation->ipa));

		list_del(&translation->node);
		hyp_free(translation);
	}

	return 0;
}

static int ffa_guest_share_ranges(struct ffa_mem_region_addr_range *ranges,
				  u32 nranges, struct pkvm_hyp_vcpu *vcpu,
				  struct ffa_composite_mem_region *out_region,
				  u64 vm_handle, struct ffa_mem_transfer *transfer,
				  u64 *exit_code)
{
	struct ffa_mem_region_addr_range *range;
	struct ffa_mem_region_addr_range *buf = out_region->constituents;
	int i, j, ret, mem_region_idx = 0;
	u64 ipa;
	phys_addr_t pa;

	for (i = 0; i < nranges; i++) {
		range = &ranges[i];
		for (j = 0; j < range->pg_cnt; j++) {
			ipa = range->address + PAGE_SIZE * j;
			ret = ffa_guest_share_with_cb(vcpu, __pkvm_guest_share_ffa, ipa, (void **)&pa, exit_code);
			if (ret)
				goto unshare;

			ret = ffa_store_translation(transfer, ipa, pa, vcpu, exit_code);
			if (ret) {
				WARN_ON(__pkvm_guest_unshare_ffa(vcpu, ipa));
				goto unshare;
			}

			buf[mem_region_idx].address = pa;
			buf[mem_region_idx].pg_cnt = 1;

			mem_region_idx++;
		}
	}

	out_region->addr_range_cnt = mem_region_idx;
	return 0;
unshare:
	ffa_guest_unshare_ranges(buf, mem_region_idx, vcpu, transfer);
	return ret;
}

static int ffa_host_share_ranges(struct ffa_mem_region_addr_range *ranges,
				 u32 nranges)
{
	u32 nshared = __ffa_host_share_ranges(ranges, nranges);
	int ret = 0;

	if (nshared != nranges) {
		WARN_ON(__ffa_host_unshare_ranges(ranges, nshared) != nshared);
		ret = FFA_RET_DENIED;
	}

	return ret;
}

static int ffa_host_unshare_ranges(struct ffa_mem_region_addr_range *ranges,
				   u32 nranges)
{
	u32 nunshared = __ffa_host_unshare_ranges(ranges, nranges);
	int ret = 0;

	if (nunshared != nranges) {
		WARN_ON(__ffa_host_share_ranges(ranges, nunshared) != nunshared);
		ret = FFA_RET_DENIED;
	}

	return ret;
}

static void do_ffa_mem_frag_tx(struct arm_smccc_res *res,
			       struct kvm_cpu_context *ctxt,
			       unsigned int vm_handle)
{
	DECLARE_REG(u32, handle_lo, ctxt, 1);
	DECLARE_REG(u32, handle_hi, ctxt, 2);
	DECLARE_REG(u32, fraglen, ctxt, 3);
	DECLARE_REG(u32, endpoint_id, ctxt, 4);
	struct ffa_mem_region_addr_range *buf;
	int ret = FFA_RET_INVALID_PARAMETERS;
	u32 nr_ranges;

	if (fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE)
		goto out;

	if (fraglen % sizeof(*buf))
		goto out;

	hyp_spin_lock(&hyp_buffers.lock);
	if (!endp_buffers[vm_handle].tx)
		goto out_unlock;

	buf = hyp_buffers.tx;
	memcpy(buf, endp_buffers[vm_handle].tx, fraglen);
	nr_ranges = fraglen / sizeof(*buf);

	ret = ffa_host_share_ranges(buf, nr_ranges);
	if (ret) {
		/*
		 * We're effectively aborting the transaction, so we need
		 * to restore the global state back to what it was prior to
		 * transmission of the first fragment.
		 */
		ffa_mem_reclaim(res, handle_lo, handle_hi, 0);
		WARN_ON(res->a0 != FFA_SUCCESS);
		goto out_unlock;
	}

	ffa_mem_frag_tx(res, handle_lo, handle_hi, fraglen, endpoint_id);
	if (res->a0 != FFA_SUCCESS && res->a0 != FFA_MEM_FRAG_RX)
		WARN_ON(ffa_host_unshare_ranges(buf, nr_ranges));

out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);
out:
	if (ret)
		ffa_to_smccc_res(res, ret);

	/*
	 * If for any reason this did not succeed, we're in trouble as we have
	 * now lost the content of the previous fragments and we can't rollback
	 * the host stage-2 changes. The pages previously marked as shared will
	 * remain stuck in that state forever, hence preventing the host from
	 * sharing/donating them again and may possibly lead to subsequent
	 * failures, but this will not compromise confidentiality.
	 */
	return;
}

static bool is_page_count_valid(struct ffa_composite_mem_region *reg,
				u32 nranges)
{
	int i;
	u32 pg_cnt = 0;

	for (i = 0; i < nranges; i++)
		pg_cnt += reg->constituents[i].pg_cnt;

	return pg_cnt == reg->total_pg_cnt;
}

static __always_inline int __do_ffa_mem_xfer(const u64 func_id,
					     struct arm_smccc_res *res,
					     struct kvm_cpu_context *ctxt,
					     unsigned int vm_handle,
					     u64 *exit_code)
{
	DECLARE_REG(u32, len, ctxt, 1);
	DECLARE_REG(u32, fraglen, ctxt, 2);
	DECLARE_REG(u64, addr_mbz, ctxt, 3);
	DECLARE_REG(u32, npages_mbz, ctxt, 4);
	struct ffa_mem_region_attributes *ep_mem_access;
	struct ffa_composite_mem_region *reg, *temp_reg;
	struct ffa_mem_region *buf;
	u32 offset, nr_ranges;
	int ret = 0;
	struct ffa_mem_transfer *transfer = NULL;
	struct pkvm_hyp_vcpu *vcpu;

	BUILD_BUG_ON(func_id != FFA_FN64_MEM_SHARE &&
		     func_id != FFA_FN64_MEM_LEND);

	if (addr_mbz || npages_mbz || fraglen > len ||
	    fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (fraglen < sizeof(struct ffa_mem_region) +
		      sizeof(struct ffa_mem_region_attributes)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out;
	}

	if (vm_handle) {
		/* Reject the fragmentation API for the guest */
		if (len != fraglen) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out;
		}

		vcpu = PKVM_VCPU_FROM_CTXT(ctxt);
		transfer = ffa_alloc(sizeof(struct ffa_mem_transfer), vcpu, exit_code);
		if (IS_ERR(transfer)) {
			ret = PTR_ERR(transfer);
			goto out;
		}

		INIT_LIST_HEAD(&transfer->translations);
	}

	hyp_spin_lock(&hyp_buffers.lock);
	if (!endp_buffers[vm_handle].tx) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	buf = hyp_buffers.tx;
	memcpy(buf, endp_buffers[vm_handle].tx, fraglen);

	ep_mem_access = (void *)buf +
			ffa_mem_desc_offset(buf, 0, hyp_ffa_version);
	offset = ep_mem_access->composite_off;
	if (!offset || buf->ep_count != 1) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	if (fraglen < offset + sizeof(struct ffa_composite_mem_region)) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	reg = (void *)buf + offset;
	nr_ranges = ((void *)buf + fraglen) - (void *)reg->constituents;
	if (nr_ranges % sizeof(reg->constituents[0])) {
		ret = FFA_RET_INVALID_PARAMETERS;
		goto out_unlock;
	}

	nr_ranges /= sizeof(reg->constituents[0]);
	if (vm_handle) {
		if (!is_page_count_valid(reg, nr_ranges)) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out_unlock;
		}

		size_t painted_sz = reg->total_pg_cnt * sizeof(struct ffa_mem_region_addr_range)
			+ offset;
		if (painted_sz > PAGE_SIZE) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out_unlock;
		}

		memcpy(ffa_desc_buf.buf, buf, offset);
		temp_reg = ffa_desc_buf.buf + offset;
		ret = ffa_guest_share_ranges(reg->constituents, nr_ranges, vcpu,
					     temp_reg, vm_handle, transfer, exit_code);
		if (!ret) {
			/* Re-adjust the size of the transfer after painting with PAs */
			if (temp_reg->addr_range_cnt > reg->addr_range_cnt) {
				u32 extra_sz = (temp_reg->addr_range_cnt - reg->addr_range_cnt) *
					sizeof(struct ffa_mem_region_addr_range);
				fraglen += extra_sz;
				len += extra_sz;

				nr_ranges = reg->addr_range_cnt = temp_reg->addr_range_cnt;
			}

			memcpy(reg->constituents, temp_reg->constituents,
			       temp_reg->addr_range_cnt * sizeof(struct ffa_mem_region_addr_range));
		}
	} else
		ret = ffa_host_share_ranges(reg->constituents, nr_ranges);
	if (ret)
		goto out_unlock;

	ffa_mem_xfer(res, func_id, len, fraglen);
	if (fraglen != len) {
		if (res->a0 != FFA_MEM_FRAG_RX)
			goto err_unshare;

		if (res->a3 != fraglen)
			goto err_unshare;
	} else if (res->a0 != FFA_SUCCESS) {
		goto err_unshare;
	}

	if (vm_handle) {
		transfer->ffa_handle = PACK_HANDLE(res->a2, res->a3);
		list_add(&transfer->node, &endp_buffers[vm_handle].xfer_list);
	}
out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);
out:
	if (ret) {
		ffa_to_smccc_res(res, ret);
		if (transfer && !IS_ERR(transfer))
			hyp_free(transfer);
	}
	return ret;

err_unshare:
	if (vm_handle)
		WARN_ON(ffa_guest_unshare_ranges(reg->constituents, nr_ranges, vcpu, transfer));
	else
		WARN_ON(ffa_host_unshare_ranges(reg->constituents, nr_ranges));
	goto out_unlock;
}

static struct ffa_mem_transfer *find_transfer_by_handle_locked(u64 ffa_handle,
							       struct kvm_ffa_buffers *endp)
{
	struct ffa_mem_transfer *transfer;

	list_for_each_entry(transfer, &endp->xfer_list, node)
		if (transfer->ffa_handle == ffa_handle)
			return transfer;
	return NULL;
}

static void do_ffa_mem_reclaim(struct arm_smccc_res *res,
			       struct kvm_cpu_context *ctxt,
			       unsigned int vm_handle)
{
	DECLARE_REG(u32, handle_lo, ctxt, 1);
	DECLARE_REG(u32, handle_hi, ctxt, 2);
	DECLARE_REG(u32, flags, ctxt, 3);
	struct ffa_mem_region_attributes *ep_mem_access;
	struct ffa_composite_mem_region *reg;
	u32 offset, len, fraglen, fragoff;
	struct ffa_mem_region *buf;
	int ret = 0, i;
	u64 handle;
	struct ffa_mem_transfer *transfer = NULL;
	struct pkvm_hyp_vcpu *vcpu;

	handle = PACK_HANDLE(handle_lo, handle_hi);

	hyp_spin_lock(&hyp_buffers.lock);

	if (vm_handle) {
		vcpu = PKVM_VCPU_FROM_CTXT(ctxt);
		transfer = find_transfer_by_handle_locked(handle, &endp_buffers[vm_handle]);
		if (!transfer) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out_unlock;
		}
	} else {
		for (i = 1; i < KVM_MAX_PVMS; i++) {
			if (list_empty(&endp_buffers[i].xfer_list))
				continue;

			transfer = find_transfer_by_handle_locked(handle, &endp_buffers[i]);
			if (transfer)
				break;
		}

		/* Prevent the host from replicating a transfer handle used by the guest */
		WARN_ON(transfer);
	}

	buf = hyp_buffers.tx;
	*buf = (struct ffa_mem_region) {
		.sender_id	= HOST_FFA_ID,
		.handle		= handle,
	};

	ffa_retrieve_req(res, sizeof(*buf));
	buf = hyp_buffers.rx;
	if (res->a0 != FFA_MEM_RETRIEVE_RESP)
		goto out_unlock;

	len = res->a1;
	fraglen = res->a2;

	ep_mem_access = (void *)buf +
			ffa_mem_desc_offset(buf, 0, hyp_ffa_version);
	offset = ep_mem_access->composite_off;
	/*
	 * We can trust the SPMD to get this right, but let's at least
	 * check that we end up with something that doesn't look _completely_
	 * bogus.
	 */
	if (WARN_ON(offset > len ||
		    fraglen > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE)) {
		ret = FFA_RET_ABORTED;
		ffa_rx_release(res);
		goto out_unlock;
	}

	if (len > ffa_desc_buf.len) {
		ret = FFA_RET_NO_MEMORY;
		ffa_rx_release(res);
		goto out_unlock;
	}

	buf = ffa_desc_buf.buf;
	memcpy(buf, hyp_buffers.rx, fraglen);
	ffa_rx_release(res);

	for (fragoff = fraglen; fragoff < len; fragoff += fraglen) {
		ffa_mem_frag_rx(res, handle_lo, handle_hi, fragoff);
		if (res->a0 != FFA_MEM_FRAG_TX) {
			ret = FFA_RET_INVALID_PARAMETERS;
			goto out_unlock;
		}

		fraglen = res->a3;
		memcpy((void *)buf + fragoff, hyp_buffers.rx, fraglen);
		ffa_rx_release(res);
	}

	ffa_mem_reclaim(res, handle_lo, handle_hi, flags);
	if (res->a0 != FFA_SUCCESS)
		goto out_unlock;

	reg = (void *)buf + offset;
	/* If the SPMD was happy, then we should be too. */
	if (vm_handle)
		WARN_ON(ffa_guest_unshare_ranges(reg->constituents,
						 reg->addr_range_cnt, vcpu, transfer));
	else
		WARN_ON(ffa_host_unshare_ranges(reg->constituents,
						reg->addr_range_cnt));

	if (transfer) {
		list_del(&transfer->node);
		hyp_free(transfer);
	}
out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);

	if (ret)
		ffa_to_smccc_res(res, ret);
}

/*
 * Is a given FFA function supported, either by forwarding on directly
 * or by handling at EL2?
 */
static bool ffa_call_supported(u64 func_id)
{
	switch (func_id) {
	/* Unsupported memory management calls */
	case FFA_FN64_MEM_RETRIEVE_REQ:
	case FFA_MEM_RETRIEVE_RESP:
	case FFA_MEM_RELINQUISH:
	case FFA_MEM_OP_PAUSE:
	case FFA_MEM_OP_RESUME:
	case FFA_MEM_FRAG_RX:
	case FFA_FN64_MEM_DONATE:
	/* Indirect message passing via RX/TX buffers */
	case FFA_MSG_SEND:
	case FFA_MSG_POLL:
	case FFA_MSG_WAIT:
	/* 32-bit variants of 64-bit calls */
	case FFA_MSG_SEND_DIRECT_RESP:
	case FFA_RXTX_MAP:
	case FFA_MEM_DONATE:
	case FFA_MEM_RETRIEVE_REQ:
		return false;
	}

	return true;
}

static bool do_ffa_features(struct arm_smccc_res *res,
			    struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, id, ctxt, 1);
	u64 prop = 0;
	int ret = 0;

	if (!ffa_call_supported(id)) {
		ret = FFA_RET_NOT_SUPPORTED;
		goto out_handled;
	}

	switch (id) {
	case FFA_MEM_SHARE:
	case FFA_FN64_MEM_SHARE:
	case FFA_MEM_LEND:
	case FFA_FN64_MEM_LEND:
		ret = FFA_RET_SUCCESS;
		prop = 0; /* No support for dynamic buffers */
		goto out_handled;
	default:
		return false;
	}

out_handled:
	ffa_to_smccc_res_prop(res, ret, prop);
	return true;
}

static int hyp_ffa_post_init(void)
{
	size_t min_rxtx_sz;
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(FFA_ID_GET, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 != FFA_SUCCESS)
		return -EOPNOTSUPP;

	if (res.a2 != HOST_FFA_ID)
		return -EINVAL;

	arm_smccc_1_1_smc(FFA_FEATURES, FFA_FN64_RXTX_MAP,
			  0, 0, 0, 0, 0, 0, &res);
	if (res.a0 != FFA_SUCCESS)
		return -EOPNOTSUPP;

	switch (res.a2) {
	case FFA_FEAT_RXTX_MIN_SZ_4K:
		min_rxtx_sz = SZ_4K;
		break;
	case FFA_FEAT_RXTX_MIN_SZ_16K:
		min_rxtx_sz = SZ_16K;
		break;
	case FFA_FEAT_RXTX_MIN_SZ_64K:
		min_rxtx_sz = SZ_64K;
		break;
	default:
		return -EINVAL;
	}

	if (min_rxtx_sz > PAGE_SIZE)
		return -EOPNOTSUPP;

	return 0;
}

static void do_ffa_version(struct arm_smccc_res *res,
			   struct kvm_cpu_context *ctxt)
{
	DECLARE_REG(u32, ffa_req_version, ctxt, 1);

	if (FFA_MAJOR_VERSION(ffa_req_version) != 1) {
		res->a0 = FFA_RET_NOT_SUPPORTED;
		return;
	}

	hyp_spin_lock(&version_lock);
	if (has_version_negotiated) {
		res->a0 = hyp_ffa_version;
		goto unlock;
	}

	/*
	 * If the client driver tries to downgrade the version, we need to ask
	 * first if TEE supports it.
	 */
	if (FFA_MINOR_VERSION(ffa_req_version) < FFA_MINOR_VERSION(hyp_ffa_version)) {
		arm_smccc_1_1_smc(FFA_VERSION, ffa_req_version, 0,
				  0, 0, 0, 0, 0,
				  res);
		if (res->a0 == FFA_RET_NOT_SUPPORTED)
			goto unlock;

		hyp_ffa_version = ffa_req_version;
	}

	if (hyp_ffa_post_init())
		res->a0 = FFA_RET_NOT_SUPPORTED;
	else {
		has_version_negotiated = true;
		res->a0 = hyp_ffa_version;
	}
unlock:
	hyp_spin_unlock(&version_lock);
}

static void do_ffa_part_get(struct arm_smccc_res *res,
			    struct kvm_cpu_context *ctxt,
			    u64 vm_handle)
{
	DECLARE_REG(u32, uuid0, ctxt, 1);
	DECLARE_REG(u32, uuid1, ctxt, 2);
	DECLARE_REG(u32, uuid2, ctxt, 3);
	DECLARE_REG(u32, uuid3, ctxt, 4);
	DECLARE_REG(u32, flags, ctxt, 5);
	u32 i, count, partition_sz, copy_sz;

	hyp_spin_lock(&hyp_buffers.lock);
	if (!endp_buffers[vm_handle].rx) {
		ffa_to_smccc_res(res, FFA_RET_BUSY);
		goto out_unlock;
	}

	arm_smccc_1_1_smc(FFA_PARTITION_INFO_GET, uuid0, uuid1,
			  uuid2, uuid3, flags, 0, 0,
			  res);

	if (res->a0 != FFA_SUCCESS)
		goto out_unlock;

	count = res->a2;
	if (!count)
		goto out_unlock;

	if (hyp_ffa_version > FFA_VERSION_1_0) {
		/* Get the number of partitions deployed in the system */
		if (flags & 0x1)
			goto out_unlock;

		partition_sz  = res->a3;
	} else {
		/* FFA_VERSION_1_0 lacks the size in the response */
		partition_sz = FFA_1_0_PARTITON_INFO_SZ;
	}

	copy_sz = partition_sz * count;
	if (copy_sz > KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE) {
		ffa_to_smccc_res(res, FFA_RET_ABORTED);
		goto out_unlock;
	}

	memcpy(endp_buffers[vm_handle].rx, hyp_buffers.rx, copy_sz);

	if (num_registered_sp_ids)
		goto out_unlock;

	count = count < FFA_MAX_REGISTERED_SP_IDS ? count : FFA_MAX_REGISTERED_SP_IDS;
	for (i = 0; i < count; i++) {
		struct ffa_partition_info *part = hyp_buffers.rx + i * partition_sz;
		if ((part->properties & FFA_PART_VM_AVAIL_MASK) == FFA_PART_SUPPORTS_VM_AVAIL) {
			sp_ids[num_registered_sp_ids++] = part->id;
		}
	}
out_unlock:
	hyp_spin_unlock(&hyp_buffers.lock);
}

bool kvm_host_ffa_handler(struct kvm_cpu_context *ctxt, u32 func_id)
{
	DECLARE_REG(u64, arg1, ctxt, 1);
	DECLARE_REG(u64, arg2, ctxt, 2);
	DECLARE_REG(u64, arg3, ctxt, 3);
	DECLARE_REG(u64, arg4, ctxt, 4);
	struct arm_smccc_res res;
	bool handled = true;
	int err = 0;

	/*
	 * There's no way we can tell what a non-standard SMC call might
	 * be up to. Ideally, we would terminate these here and return
	 * an error to the host, but sadly devices make use of custom
	 * firmware calls for things like power management, debugging,
	 * RNG access and crash reporting.
	 *
	 * Given that the architecture requires us to trust EL3 anyway,
	 * we forward unrecognised calls on under the assumption that
	 * the firmware doesn't expose a mechanism to access arbitrary
	 * non-secure memory. Short of a per-device table of SMCs, this
	 * is the best we can do.
	 */
	if (!is_ffa_call(func_id))
		return false;

	if (!has_version_negotiated && func_id != FFA_VERSION) {
		ffa_to_smccc_error(&res, FFA_RET_INVALID_PARAMETERS);
		goto unhandled;
	}

	switch (func_id) {
	case FFA_FEATURES:
		if (!do_ffa_features(&res, ctxt)) {
			handled = false;
			goto unhandled;
		}
		break;
	/* Memory management */
	case FFA_FN64_RXTX_MAP:
		do_ffa_rxtx_map(&res, ctxt, HOST_FFA_ID, NULL);
		break;
	case FFA_RXTX_UNMAP:
		do_ffa_rxtx_unmap(&res, ctxt, HOST_FFA_ID);
		break;
	case FFA_MEM_SHARE:
	case FFA_FN64_MEM_SHARE:
		__do_ffa_mem_xfer(FFA_FN64_MEM_SHARE, &res, ctxt, HOST_FFA_ID, NULL);
		break;
	case FFA_MEM_RECLAIM:
		do_ffa_mem_reclaim(&res, ctxt, HOST_FFA_ID);
		break;
	case FFA_MEM_LEND:
	case FFA_FN64_MEM_LEND:
		__do_ffa_mem_xfer(FFA_FN64_MEM_LEND, &res, ctxt, HOST_FFA_ID, NULL);
		break;
	case FFA_MEM_FRAG_TX:
		do_ffa_mem_frag_tx(&res, ctxt, HOST_FFA_ID);
		break;
	case FFA_VERSION:
		do_ffa_version(&res, ctxt);
		break;
	case FFA_PARTITION_INFO_GET:
		do_ffa_part_get(&res, ctxt, HOST_FFA_ID);
		break;
	default:
		if (ffa_call_supported(func_id)) {
			handled = false;
			goto unhandled;
		}

		ffa_to_smccc_error(&res, FFA_RET_NOT_SUPPORTED);
	}

	ffa_set_retval(ctxt, &res);
	err = res.a0 == FFA_SUCCESS ? 0 : res.a2;
unhandled:
	trace_host_ffa_call(func_id, arg1, arg2, arg3, arg4, handled, err);
	return handled;
}

static void smccc_set_client_id(struct kvm_vcpu *vcpu, u16 vmid)
{
	vcpu_set_reg(vcpu, 7, vmid);
}

bool kvm_guest_ffa_handler(struct pkvm_hyp_vcpu *hyp_vcpu, u64 *exit_code)
{
	struct kvm_vcpu *vcpu = &hyp_vcpu->vcpu;
	struct kvm_cpu_context *ctxt = &vcpu->arch.ctxt;
	struct arm_smccc_res res;
	int ret = 0;
	uint16_t vm_handle;

	DECLARE_REG(u64, func_id, ctxt, 0);

	if (!VM_FFA_SUPPORTED(&hyp_vcpu->vcpu))
		return true;

	vm_handle = VM_FFA_HANDLE_FROM_VCPU(vcpu);
	WARN_ON(vm_handle >= KVM_MAX_PVMS);

	if (!is_ffa_call(func_id))
		goto unhandled;

	switch (func_id) {
	case FFA_FEATURES:
		if (!do_ffa_features(&res, ctxt)) {
			goto unhandled;
		}
		break;
	case FFA_VERSION:
		do_ffa_version(&res, ctxt);
		break;
	case FFA_FN64_RXTX_MAP:
		ret = do_ffa_rxtx_map(&res, ctxt, vm_handle, exit_code);
		break;
	case FFA_RXTX_UNMAP:
		do_ffa_rxtx_unmap(&res, ctxt, vm_handle);
		break;
	case FFA_MEM_SHARE:
	case FFA_FN64_MEM_SHARE:
		ret = __do_ffa_mem_xfer(FFA_FN64_MEM_SHARE, &res, ctxt, vm_handle, exit_code);
		break;
	case FFA_MEM_RECLAIM:
		do_ffa_mem_reclaim(&res, ctxt, vm_handle);
		break;
	case FFA_MEM_LEND:
	case FFA_FN64_MEM_LEND:
		ret = __do_ffa_mem_xfer(FFA_FN64_MEM_LEND, &res, ctxt, vm_handle, exit_code);
		break;
	case FFA_ID_GET:
		ffa_to_smccc_res_prop(&res, FFA_RET_SUCCESS, vm_handle);
		break;
	case FFA_PARTITION_INFO_GET:
		do_ffa_part_get(&res, ctxt, vm_handle);
		break;
	default:
		if (ffa_call_supported(func_id))
			goto unhandled;

		ffa_to_smccc_error(&res, FFA_RET_NOT_SUPPORTED);
	}

	if (ret >= 0)
		ffa_set_retval(ctxt, &res);

	return ret >= 0;
unhandled:
	smccc_set_client_id(vcpu, vm_handle);
	__kvm_hyp_host_forward_smc(ctxt);
	return true;
}

static void kvm_guest_clear_transfer(struct ffa_mem_transfer *transfer, struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct ffa_translation *translation, *tmp;

	list_for_each_entry_safe(translation, tmp, &transfer->translations, node) {
		WARN_ON(__pkvm_guest_unshare_ffa(hyp_vcpu, translation->ipa));
		list_del(&translation->node);
		hyp_free(translation);
	}
}

int kvm_reclaim_ffa_guest_pages(struct pkvm_hyp_vm *vm, pkvm_handle_t handle)
{
	int ret = 0;
	uint16_t vm_handle;
	bool guest_has_ffa = false;
	struct ffa_mem_transfer *transfer, *tmp;
	struct arm_smccc_res res;
	struct pkvm_hyp_vcpu *hyp_vcpu = vm->vcpus[0];

	if (!VM_FFA_SUPPORTED(&hyp_vcpu->vcpu))
		return 0;

	vm_handle = VM_FFA_HANDLE_FROM_VCPU(&hyp_vcpu->vcpu);
	WARN_ON(vm_handle >= KVM_MAX_PVMS);

	hyp_spin_lock(&hyp_buffers.lock);
	guest_has_ffa = endp_buffers[vm_handle].tx || endp_buffers[vm_handle].rx;
	if (!guest_has_ffa)
		goto unlock;

	ret = kvm_notify_vm_availability(vm_handle, FFA_VM_DESTRUCTION_MSG);
	if (ret != FFA_RET_SUCCESS)
		goto unlock;

	list_for_each_entry_safe(transfer, tmp, &endp_buffers[vm_handle].xfer_list, node) {
		ffa_mem_reclaim(&res,
				HANDLE_LOW(transfer->ffa_handle),
				HANDLE_HIGH(transfer->ffa_handle), 0);
		if (res.a0 != FFA_SUCCESS) {
			ret = -EAGAIN;
			goto unlock;
		}

		kvm_guest_clear_transfer(transfer, hyp_vcpu);
		list_del(&transfer->node);
		hyp_free(transfer);
	}

	if (endp_buffers[vm_handle].tx) {
		hyp_unpin_shared_guest_page(hyp_vcpu, endp_buffers[vm_handle].tx);
		WARN_ON(__pkvm_guest_unshare_hyp(hyp_vcpu, endp_buffers[vm_handle].tx_ipa));
		endp_buffers[vm_handle].tx = NULL;
	}

	if (endp_buffers[vm_handle].rx) {
		hyp_unpin_shared_guest_page(hyp_vcpu, endp_buffers[vm_handle].rx);
		WARN_ON(__pkvm_guest_unshare_hyp(hyp_vcpu, endp_buffers[vm_handle].rx_ipa));
		endp_buffers[vm_handle].rx = NULL;
	}
unlock:
	hyp_spin_unlock(&hyp_buffers.lock);

	return ret;
}

int hyp_ffa_init(void *pages)
{
	struct arm_smccc_res res;
	void *tx, *rx;
	int i;

	if (kvm_host_psci_config.smccc_version < ARM_SMCCC_VERSION_1_1)
		return 0;

	arm_smccc_1_1_smc(FFA_VERSION, FFA_VERSION_1_1, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == FFA_RET_NOT_SUPPORTED)
		return 0;

	/*
	 * Firmware returns the maximum supported version of the FF-A
	 * implementation. Check that the returned version is
	 * backwards-compatible with the hyp according to the rules in DEN0077A
	 * v1.1 REL0 13.2.1.
	 *
	 * Of course, things are never simple when dealing with firmware. v1.1
	 * broke ABI with v1.0 on several structures, which is itself
	 * incompatible with the aforementioned versioning scheme. The
	 * expectation is that v1.x implementations that do not support the v1.0
	 * ABI return NOT_SUPPORTED rather than a version number, according to
	 * DEN0077A v1.1 REL0 18.6.4.
	 */
	if (FFA_MAJOR_VERSION(res.a0) != 1)
		return -EOPNOTSUPP;

	if (FFA_MINOR_VERSION(res.a0) < FFA_MINOR_VERSION(FFA_VERSION_1_1))
		hyp_ffa_version = res.a0;
	else
		hyp_ffa_version = FFA_VERSION_1_1;

	tx = pages;
	pages += KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE;
	rx = pages;
	pages += KVM_FFA_MBOX_NR_PAGES * PAGE_SIZE;

	ffa_desc_buf = (struct kvm_ffa_descriptor_buffer) {
		.buf	= pages,
		.len	= PAGE_SIZE *
			  (hyp_ffa_proxy_pages() - (2 * KVM_FFA_MBOX_NR_PAGES)),
	};

	hyp_buffers = (struct kvm_ffa_buffers) {
		.lock	= __HYP_SPIN_LOCK_UNLOCKED,
		.tx	= tx,
		.rx	= rx,
	};

	for (i = 0; i < KVM_MAX_PVMS; i++) {
		endp_buffers[i] = (struct kvm_ffa_buffers) {
			.lock	= __HYP_SPIN_LOCK_UNLOCKED,
		};
		INIT_LIST_HEAD(&endp_buffers[i].xfer_list);
	}

	version_lock = __HYP_SPIN_LOCK_UNLOCKED;
	return 0;
}
