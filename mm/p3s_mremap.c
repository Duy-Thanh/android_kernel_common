// SPDX-License-Identifier: GPL-2.0
/* Repack compat anonymous PTEs after a cross-slice mremap. */

#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/sort.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include "internal.h"
#include "p3s.h"

struct p3s_mremap_tuple {
	unsigned long base;
	struct folio *folio;
	bool populated;
};

struct p3s_mremap_ctx {
	struct folio **sources;
	struct p3s_mremap_tuple *tuples;
	unsigned int nr_sources;
	unsigned int nr_locked;
	unsigned int nr_tuples;
};

static int p3s_folio_ptr_cmp(const void *a, const void *b)
{
	unsigned long x = (unsigned long)*(struct folio * const *)a;
	unsigned long y = (unsigned long)*(struct folio * const *)b;

	return (x > y) - (x < y);
}

void p3s_mremap_finish(struct p3s_mremap_ctx *ctx)
{
	unsigned int i;

	if (!ctx)
		return;
	for (i = 0; i < ctx->nr_locked; i++)
		folio_unlock(ctx->sources[i]);
	for (i = 0; i < ctx->nr_sources; i++)
		folio_put(ctx->sources[i]);
	for (i = 0; i < ctx->nr_tuples; i++)
		if (ctx->tuples[i].folio)
			folio_put(ctx->tuples[i].folio);
	kvfree(ctx->tuples);
	kvfree(ctx->sources);
	kfree(ctx);
}

static int p3s_mremap_source_folio(struct vm_area_struct *vma,
		unsigned long addr, struct folio **foliop, bool *retry)
{
	struct folio *folio = NULL;
	spinlock_t *ptl;
	pte_t *ptep;
	pmd_t *pmd;
	pte_t pte;

	*retry = false;
	pmd = mm_find_pmd(vma->vm_mm, addr);
	if (!pmd || pmd_none(*pmd)) {
		*foliop = NULL;
		return 0;
	}
	ptep = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!ptep) {
		*foliop = NULL;
		return 0;
	}
	pte = ptep_get(ptep);
	if (is_swap_pte(pte)) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		if (!non_swap_entry(entry) || is_migration_entry(entry))
			*retry = true;
	} else if (pte_present(pte) && !pte_special(pte)) {
		folio = vm_normal_folio(vma, addr, pte);
		if (folio && folio_test_anon(folio))
			folio_get(folio);
		else
			folio = NULL;
	}
	pte_unmap_unlock(ptep, ptl);
	*foliop = folio;
	return 0;
}

static int p3s_mremap_fault_source(struct vm_area_struct *vma,
		unsigned long addr)
{
	vm_fault_t fault = handle_mm_fault(vma, addr, FAULT_FLAG_REMOTE, NULL);

	if (fault & VM_FAULT_ERROR)
		return vm_fault_to_errno(fault, 0);
	if (WARN_ON_ONCE(fault & (VM_FAULT_RETRY | VM_FAULT_COMPLETED)))
		return -EAGAIN;
	return 0;
}

static struct folio *p3s_mremap_alloc_folio(struct vm_area_struct *vma,
		unsigned long addr)
{
	struct folio *folio;

	folio = vma_alloc_zeroed_movable_folio(vma, addr);
	if (!folio)
		return NULL;
	if (mem_cgroup_charge(folio, vma->vm_mm, GFP_KERNEL)) {
		folio_put(folio);
		return NULL;
	}
	folio_throttle_swaprate(folio, GFP_KERNEL);
	return folio;
}

static bool p3s_mremap_has_source(const struct p3s_mremap_ctx *ctx,
		struct folio *folio)
{
	unsigned int lo = 0, hi = ctx->nr_sources;

	while (lo < hi) {
		unsigned int mid = lo + (hi - lo) / 2;

		if ((unsigned long)ctx->sources[mid] < (unsigned long)folio)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < ctx->nr_sources && ctx->sources[lo] == folio;
}

static int p3s_mremap_validate_sources(struct p3s_mremap_ctx *ctx,
		struct vm_area_struct *vma, unsigned long addr, unsigned long len)
{
	for (; addr < vma->vm_end && len; addr += PAGE_SIZE_4KB,
	     len -= PAGE_SIZE_4KB) {
		struct folio *folio;
		bool retry;
		int ret;

		ret = p3s_mremap_source_folio(vma, addr, &folio, &retry);
		if (ret)
			return ret;
		if (retry)
			return -EAGAIN;
		if (folio) {
			bool found = p3s_mremap_has_source(ctx, folio);

			folio_put(folio);
			if (!found)
				return -EAGAIN;
		}
	}
	return len ? -EFAULT : 0;
}

struct p3s_mremap_ctx *p3s_mremap_prepare(
		struct vm_area_struct *src_vma, struct vm_area_struct *dst_vma,
		unsigned long old_addr, unsigned long new_addr, unsigned long len)
{
	unsigned long first, last, addr;
	unsigned int max_sources, i, nr = 0;
	struct p3s_mremap_ctx *ctx;
	int ret;

	if (!vma_is_p3s_4k(src_vma) || !vma_is_anonymous(src_vma) || !len ||
	    !((old_addr ^ new_addr) & (PAGE_SIZE_KERNEL - 1)))
		return NULL;
	if (src_vma->vm_flags & VM_MTE)
		return ERR_PTR(-EOPNOTSUPP);
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);
	max_sources = len >> PAGE_SHIFT_4KB;
	ctx->sources = kvcalloc(max_sources, sizeof(*ctx->sources), GFP_KERNEL);
	first = new_addr & PAGE_MASK;
	last = PAGE_ALIGN(new_addr + len);
	ctx->nr_tuples = (last - first) >> PAGE_SHIFT;
	ctx->tuples = kvcalloc(ctx->nr_tuples, sizeof(*ctx->tuples), GFP_KERNEL);
	if (!ctx->sources || !ctx->tuples) {
		ret = -ENOMEM;
		goto err;
	}
	for (i = 0; i < ctx->nr_tuples; i++)
		ctx->tuples[i].base = first + ((unsigned long)i << PAGE_SHIFT);

	for (addr = old_addr; addr < old_addr + len;
	     addr += PAGE_SIZE_4KB) {
		struct folio *folio;
		bool retry;

		for (;;) {
			ret = p3s_mremap_source_folio(src_vma, addr, &folio,
						       &retry);
			if (ret || !retry)
				break;
			ret = p3s_mremap_fault_source(src_vma, addr);
			if (ret)
				break;
		}
		if (ret)
			goto err;
		if (!folio)
			continue;
		ctx->sources[nr++] = folio;
		ctx->tuples[((new_addr + addr - old_addr) - first) >> PAGE_SHIFT]
			.populated = true;
	}
	ctx->nr_sources = nr;
	if (!nr)
		return ctx;

	sort(ctx->sources, nr, sizeof(*ctx->sources), p3s_folio_ptr_cmp, NULL);
	for (i = 0, nr = 0; i < ctx->nr_sources; i++) {
		if (nr && ctx->sources[nr - 1] == ctx->sources[i])
			folio_put(ctx->sources[i]);
		else
			ctx->sources[nr++] = ctx->sources[i];
	}
	ctx->nr_sources = nr;
	for (i = 0; i < nr; i++) {
		ret = folio_lock_killable(ctx->sources[i]);
		if (ret)
			goto err;
		ctx->nr_locked++;
		if (folio_maybe_dma_pinned(ctx->sources[i])) {
			ret = -EBUSY;
			goto err;
		}
	}
	ret = p3s_mremap_validate_sources(ctx, src_vma, old_addr, len);
	if (ret)
		goto err;
	for (i = 0; i < ctx->nr_tuples; i++) {
		if (!ctx->tuples[i].populated)
			continue;
		ctx->tuples[i].folio = p3s_mremap_alloc_folio(dst_vma,
							ctx->tuples[i].base);
		if (!ctx->tuples[i].folio) {
			ret = -ENOMEM;
			goto err;
		}
	}
	return ctx;
err:
	p3s_mremap_finish(ctx);
	return ERR_PTR(ret);
}

static pte_t p3s_mremap_new_pte(struct vm_area_struct *vma,
		struct folio *folio, unsigned long addr, pte_t old)
{
	pte_t entry = vma_folio_mk_pte(vma, folio, addr);

	if (pte_young(old))
		entry = pte_mkyoung(entry);
	if (pte_dirty(old))
		entry = pte_mkdirty(entry);
	if (pte_soft_dirty(old))
		entry = pte_mksoft_dirty(entry);
	if (pte_uffd_wp(old))
		entry = pte_mkuffd_wp(entry);
	if (pte_write(old))
		entry = maybe_mkwrite(entry, vma);
	return entry;
}

static void p3s_mremap_copy_slice(struct folio *dst, unsigned int dst_slice,
		struct folio *src, unsigned int src_slice)
{
	void *from = kmap_local_page(&src->page);
	void *to = kmap_local_page(&dst->page);

	memcpy(to + ((unsigned long)dst_slice << PAGE_SHIFT_4KB),
	       from + ((unsigned long)src_slice << PAGE_SHIFT_4KB),
	       PAGE_SIZE_4KB);
	kunmap_local(to);
	kunmap_local(from);
	flush_dcache_page(&dst->page);
}

void p3s_mremap_reslice(struct p3s_mremap_ctx *ctx,
		struct vm_area_struct *vma, unsigned long new_addr,
		unsigned long len)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned int tuple_idx;

	if (!ctx || !ctx->nr_sources)
		return;
	raw_write_seqcount_begin(&mm->write_protect_seq);
	smp_mb();
	for (tuple_idx = 0; tuple_idx < ctx->nr_tuples; tuple_idx++) {
		struct p3s_mremap_tuple *tuple = &ctx->tuples[tuple_idx];
		unsigned long start, end, addr;
		unsigned int mapped = 0;

		if (!tuple->folio)
			continue;
		start = max(tuple->base, new_addr);
		end = min(tuple->base + PAGE_SIZE_KERNEL, new_addr + len);

		/* Stop userspace writes before copying any contributing slice. */
		for (addr = start; addr < end; addr += PAGE_SIZE_4KB) {
			spinlock_t *ptl;
			pte_t *ptep;
			pmd_t *pmd = mm_find_pmd(mm, addr);
			pte_t pte;

			if (!pmd)
				continue;
			ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
			if (!ptep)
				continue;
			pte = ptep_get(ptep);
			if (pte_present(pte) && !pte_special(pte) &&
			    vm_normal_folio(vma, addr, pte)) {
				ptep_set_wrprotect(mm, addr, ptep);
				flush_tlb_page(vma, addr);
			}
			pte_unmap_unlock(ptep, ptl);
		}

		for (addr = start; addr < end; addr += PAGE_SIZE_4KB) {
			struct folio *old_folio;
			unsigned int src_slice, dst_slice;
			spinlock_t *ptl;
			pte_t *ptep;
			pmd_t *pmd = mm_find_pmd(mm, addr);
			pte_t old, entry;

			if (!pmd)
				continue;
			ptep = pte_offset_map_lock(mm, pmd, addr, &ptl);
			if (!ptep)
				continue;
			old = ptep_get(ptep);
			old_folio = pte_present(old) && !pte_special(old) ?
				vm_normal_folio(vma, addr, old) : NULL;
			if (!old_folio || !folio_test_anon(old_folio)) {
				pte_unmap_unlock(ptep, ptl);
				continue;
			}
			src_slice = (__pte_to_phys(old) & (PAGE_SIZE_KERNEL - 1)) >>
				PAGE_SHIFT_4KB;
			dst_slice = vma_slice_offset(vma, addr);
			p3s_mremap_copy_slice(tuple->folio, dst_slice,
					      old_folio, src_slice);
			ptep_clear_flush(vma, addr, ptep);
			if (!mapped) {
				folio_add_new_anon_rmap(tuple->folio, vma, addr,
							RMAP_EXCLUSIVE);
				folio_add_lru_vma(tuple->folio, vma);
			} else {
				atomic_inc(&tuple->folio->_mapcount);
				folio_ref_add(tuple->folio, 1);
			}
			entry = p3s_mremap_new_pte(vma, tuple->folio, addr, old);
			set_pte_at(mm, addr, ptep, entry);
			update_mmu_cache(vma, addr, ptep);
			folio_remove_rmap_pte(old_folio, &old_folio->page, vma);
			folio_put(old_folio);
			mapped++;
			pte_unmap_unlock(ptep, ptl);
		}
		if (mapped) {
			__folio_mark_uptodate(tuple->folio);
			tuple->folio = NULL;
		}
	}
	raw_write_seqcount_end(&mm->write_protect_seq);
}
