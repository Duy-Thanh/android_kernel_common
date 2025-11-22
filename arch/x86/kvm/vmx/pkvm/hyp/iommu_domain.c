// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2025 Google. */

#include <../drivers/iommu/intel/iommu.h>
#include <linux/hashtable.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "gfp.h"
#include "debug.h"
#include "ept.h"
#include "iommu_internal.h"
#include "iommu.h"
#include "iommu_domain.h"
#include "memory.h"
#include "mem_protect.h"
#include "bug.h"

/*
 * TODO: Make this a dynamic value.
 */
#define MAX_IOMMU_DOMAIN_NUM	128
static DEFINE_HASHTABLE(iommu_domain_hasht, 8);
static DECLARE_BITMAP(iommu_domains_bitmap, MAX_IOMMU_DOMAIN_NUM);
static struct pkvm_iommu_domain iommu_domains[MAX_IOMMU_DOMAIN_NUM];
static pkvm_spinlock_t iommu_domain_lock = __PKVM_SPINLOCK_UNLOCKED;

static inline struct pkvm_iommu_domain *__pkvm_get_iommu_domain_locked(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL, *tmp;

	hash_for_each_possible(iommu_domain_hasht, tmp, hnode, pgd) {
		if (tmp->pgd == pgd) {
			domain = atomic_inc_not_zero(&tmp->refcount) ? tmp : NULL;
			if (domain)
				break;
		}
	}

	return domain;
}

struct pkvm_iommu_domain *pkvm_get_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain;
	pkvm_spin_lock(&iommu_domain_lock);

	domain = __pkvm_get_iommu_domain_locked(pgd);

	pkvm_spin_unlock(&iommu_domain_lock);

	return domain;
}

void pkvm_put_iommu_domain(struct pkvm_iommu_domain *domain)
{
	if (!atomic_dec_and_test(&domain->refcount))
		return;

	pkvm_dbg("pkvm: %s: freed domain pgd: %llx\n", __func__, domain->pgd);
	pkvm_spin_lock(&iommu_domain_lock);

	hlist_del(&domain->hnode);

	__clear_bit(domain->index, iommu_domains_bitmap);

	memset(domain, 0, sizeof(struct pkvm_iommu_domain));

	pkvm_spin_unlock(&iommu_domain_lock);
}

struct pkvm_iommu_domain *pkvm_alloc_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL;
	unsigned long index;

	pkvm_spin_lock(&iommu_domain_lock);

	domain = __pkvm_get_iommu_domain_locked(pgd);
	if (domain)
		goto out;

	index = find_next_zero_bit(iommu_domains_bitmap, MAX_IOMMU_DOMAIN_NUM, 0);
	if (index < MAX_IOMMU_DOMAIN_NUM) {
		__set_bit(index, iommu_domains_bitmap);
		domain = &iommu_domains[index];
		INIT_LIST_HEAD(&domain->cache_tags);
		domain->pgd = pgd;
		domain->index = index;
		domain->qi_batch.index = 0;
		atomic_set(&domain->refcount, 1);
		pkvm_spin_lock_init(&domain->lock);
		pkvm_spin_lock_init(&domain->cache_lock);
		hash_add(iommu_domain_hasht, &domain->hnode, pgd);
	}

out:
	pkvm_spin_unlock(&iommu_domain_lock);
	pkvm_dbg("pkvm: %s: allocated domain pgd: %llx\n", __func__, pgd);

	return domain;
}

void pkvm_free_iommu_domain(u64 pgd)
{
	struct pkvm_iommu_domain *domain = NULL;

	pkvm_spin_lock(&iommu_domain_lock);
	hash_for_each_possible(iommu_domain_hasht, domain, hnode, pgd) {
		if (domain->pgd == pgd)
			break;
	}
	pkvm_spin_unlock(&iommu_domain_lock);

	if (domain && domain->pgd == pgd)
		pkvm_put_iommu_domain(domain);
}

static void domain_flush_cache(struct pkvm_iommu_domain *domain,
			       void *addr, int size)
{
	if (!domain->iommu_coherency)
		pkvm_clflush_cache_range(addr, size);
}

static int domain_pfn_supported(struct pkvm_iommu_domain *domain, unsigned long pfn)
{
	int addr_width = agaw_to_width(domain->agaw) - VTD_PAGE_SHIFT;

	return !(addr_width < BITS_PER_LONG && pfn >> addr_width);
}

/* Return largest possible superpage level for a given mapping */
static int hardware_largepage_caps(struct pkvm_iommu_domain *domain, unsigned long iov_pfn,
				   unsigned long phy_pfn, unsigned long pages)
{
	int support, level = 1;
	unsigned long pfnmerge;

	support = domain->iommu_superpage;

	/* To use a large page, the virtual *and* physical addresses
	   must be aligned to 2MiB/1GiB/etc. Lower bits set in either
	   of them will mean we have to use smaller pages. So just
	   merge them and check both at once. */
	pfnmerge = iov_pfn | phy_pfn;

	while (support && !(pfnmerge & ~VTD_STRIDE_MASK)) {
		pages >>= VTD_STRIDE_SHIFT;
		if (!pages)
			break;
		pfnmerge >>= VTD_STRIDE_SHIFT;
		level++;
		support--;
	}
	return level;
}

static struct dma_pte *pfn_to_dma_pte(struct pkvm_iommu_domain *domain,
				      union pkvm_iommu_page_donation *donation,
				      unsigned long pfn, int *target_level)
{
	struct dma_pte *parent, *pte;
	int level = agaw_to_level(domain->agaw);
	int offset;

	if (!domain_pfn_supported(domain, pfn))
		/* Address beyond IOMMU's addressing capabilities. */
		return NULL;

	parent = (struct dma_pte *)pkvm_phys_to_virt(domain->pgd);

	while (1) {
		void *tmp_page;

		offset = pfn_level_offset(pfn, level);
		pte = &parent[offset];
		if (!*target_level && (dma_pte_superpage(pte) || !dma_pte_present(pte)))
			break;
		if (level == *target_level)
			break;

		if (!dma_pte_present(pte)) {
			uint64_t pteval, tmp;

			donation->nr_pages--;
			if (donation->nr_pages < 0) {
				/*
				 * Not enough pages donated by host.
				 * Ask for more and restart the hypercall.
				 */
				pkvm_err("pkvm: %s: not enough pages donated!\n", __func__);
				return NULL;
			}
			if (__pkvm_host_donate_hyp_share_ro(
						donation->pages[donation->nr_pages], VTD_PAGE_SIZE)) {
				pkvm_err("pkvm: %s: failed to write protect page!\n", __func__);
				return NULL;
			}
			tmp_page = pkvm_phys_to_virt(donation->pages[donation->nr_pages]);

			domain_flush_cache(domain, tmp_page, VTD_PAGE_SIZE);
			pteval = pkvm_virt_to_phys(tmp_page) | DMA_PTE_READ | DMA_PTE_WRITE;
			if (domain->use_first_level)
				pteval |= DMA_FL_PTE_US | DMA_FL_PTE_ACCESS;

			tmp = 0ULL;
			if (!try_cmpxchg64(&pte->val, &tmp, pteval)) {
				/* Someone else set it while we were thinking; use theirs. */
				__pkvm_hyp_donate_host_unshare_ro(
						donation->pages[donation->nr_pages], VTD_PAGE_SIZE);
				donation->nr_pages++;
				PKVM_ASSERT(donation->nr_pages <= PKVM_MAX_IOMMU_PAGE_DONATION);
			} else {
				memset(tmp_page, 0, VTD_PAGE_SIZE);
				domain_flush_cache(domain, pte, sizeof(*pte));
			}
		}
		if (level == 1)
			break;

		parent = pkvm_phys_to_virt(dma_pte_addr(pte));
		level--;
	}

	if (!*target_level)
		*target_level = level;

	return pte;
}

/* return address's pte at specific level */
static struct dma_pte *dma_pfn_level_pte(struct pkvm_iommu_domain *domain,
					 unsigned long pfn,
					 int level, int *large_page)
{
	struct dma_pte *parent, *pte;
	int total = agaw_to_level(domain->agaw);
	int offset;

	parent = (struct dma_pte *)pkvm_phys_to_virt(domain->pgd);
	while (level <= total) {
		offset = pfn_level_offset(pfn, total);
		pte = &parent[offset];
		if (level == total)
			return pte;

		if (!dma_pte_present(pte)) {
			*large_page = total;
			break;
		}

		if (dma_pte_superpage(pte)) {
			*large_page = total;
			return pte;
		}

		parent = pkvm_phys_to_virt(dma_pte_addr(pte));
		total--;
	}
	return NULL;
}

/* clear last level pte, a tlb flush should be followed */
static void dma_pte_clear_range(struct pkvm_iommu_domain *domain,
				unsigned long start_pfn,
				unsigned long last_pfn)
{
	unsigned int large_page;
	struct dma_pte *first_pte, *pte;

	if (WARN_ON(!domain_pfn_supported(domain, last_pfn)) ||
	    WARN_ON(start_pfn > last_pfn))
		return;

	/* we don't need lock here; nobody else touches the iova range */
	do {
		large_page = 1;
		first_pte = pte = dma_pfn_level_pte(domain, start_pfn, 1, &large_page);
		if (!pte) {
			start_pfn = align_to_level(start_pfn + 1, large_page + 1);
			continue;
		}
		do {
			dma_clear_pte(pte);
			start_pfn += lvl_to_nr_pages(large_page);
			pte++;
		} while (start_pfn <= last_pfn && !first_pte_in_page(pte));

		domain_flush_cache(domain, first_pte, (void *)pte - (void *)first_pte);

	} while (start_pfn && start_pfn <= last_pfn);
}

static void dma_pte_free_level(struct pkvm_iommu_domain *domain,
			       union pkvm_iommu_page_donation *donation,
			       int level,
			       int retain_level, struct dma_pte *pte,
			       unsigned long pfn, unsigned long start_pfn,
			       unsigned long last_pfn)
{
	pfn = max(start_pfn, pfn);
	pte = &pte[pfn_level_offset(pfn, level)];

	do {
		unsigned long level_pfn;
		struct dma_pte *level_pte;

		if (!dma_pte_present(pte) || dma_pte_superpage(pte))
			goto next;

		level_pfn = pfn & level_mask(level);
		level_pte = pkvm_phys_to_virt(dma_pte_addr(pte));

		if (level > 2) {
			dma_pte_free_level(domain, donation, level - 1, retain_level,
					   level_pte, level_pfn, start_pfn,
					   last_pfn);
		}

		/*
		 * Free the page table if we're below the level we want to
		 * retain and the range covers the entire table.
		 */
		if (level < retain_level && !(start_pfn > level_pfn ||
		      last_pfn < level_pfn + level_size(level) - 1)) {
			dma_clear_pte(pte);
			domain_flush_cache(domain, pte, sizeof(*pte));
			memset(level_pte, 0, VTD_PAGE_SIZE);
			if (__pkvm_hyp_donate_host_unshare_ro(
						virt_to_phys(level_pte), VTD_PAGE_SIZE)) {
				pkvm_err("pkvm: %s: failed to remove write protect page!\n",
						__func__);
				goto next;
			}
			donation->pages[donation->nr_pages++] = pkvm_virt_to_phys(level_pte);
			PKVM_ASSERT(donation->nr_pages <= PKVM_MAX_IOMMU_PAGE_DONATION);
		}
next:
		pfn += level_size(level);
	} while (!first_pte_in_page(++pte) && pfn <= last_pfn);
}

/*
 * clear last level (leaf) ptes and free page table pages below the
 * level we wish to keep intact.
 */
static void dma_pte_free_pagetable(struct pkvm_iommu_domain *domain,
				   union pkvm_iommu_page_donation *donation,
				   unsigned long start_pfn,
				   unsigned long last_pfn,
				   int retain_level)
{
	struct dma_pte *pgd = (struct dma_pte *)pkvm_phys_to_virt(domain->pgd);
	dma_pte_clear_range(domain, start_pfn, last_pfn);

	/* We don't need lock here; nobody else touches the iova range */
	dma_pte_free_level(domain, donation, agaw_to_level(domain->agaw), retain_level,
			   pgd, 0, start_pfn, last_pfn);
}

/*
 * Ensure that old small page tables are removed to make room for superpage(s).
 * We're going to add new large pages, so make sure we don't remove their parent
 * tables. The IOTLB/devTLBs should be flushed if any PDE/PTEs are cleared.
 */
static void switch_to_super_page(struct pkvm_iommu_domain *domain,
				 union pkvm_iommu_page_donation *donation,
				 unsigned long start_pfn,
				 unsigned long end_pfn, int level)
{
	unsigned long lvl_pages = lvl_to_nr_pages(level);
	struct dma_pte *pte = NULL;

	while (start_pfn <= end_pfn) {
		if (!pte)
			pte = pfn_to_dma_pte(domain, donation, start_pfn, &level);

		if (dma_pte_present(pte)) {
			dma_pte_free_pagetable(domain, donation, start_pfn,
					       start_pfn + lvl_pages - 1,
					       level + 1);
			pkvm_cache_tag_flush_range(domain, start_pfn << VTD_PAGE_SHIFT,
					end_pfn << VTD_PAGE_SHIFT, 0);
		}

		pte++;
		start_pfn += lvl_pages;
		if (first_pte_in_page(pte))
			pte = NULL;
	}
}

static inline int verify_page_onwership(unsigned long phys_start, int level)
{
	unsigned long map_phys, phys_end;
	int ept_level, ret = 1;

	host_ept_lock();

	pkvm_host_ept_lookup(phys_start, &map_phys, NULL, &ept_level);
	if (map_phys == INVALID_ADDR) {
		ret = 0;
		goto out;
	}

	if (ept_level >= level)
		goto out;

	phys_end = phys_start + (level_size(level) * VTD_PAGE_SHIFT) - 1;
	phys_start += level_size(ept_level) * VTD_PAGE_SHIFT;

	while(phys_start >= phys_end) {
		pkvm_host_ept_lookup(phys_start, &map_phys, NULL, &ept_level);
		if (map_phys == INVALID_ADDR) {
			ret = 0;
			goto out;
		}
		phys_start += level_size(ept_level) * VTD_PAGE_SHIFT;
	}

out:
	host_ept_unlock();
	if (!ret)
		pkvm_err("pkvm: phys addr 0x%lx not mapped in host ept\n", phys_start);
	return ret;
}

static int
domain_map(struct pkvm_iommu_domain *domain, struct pkvm_iommu_map_param *param,
		union pkvm_iommu_page_donation *donation)
{
	struct dma_pte *first_pte = NULL, *pte = NULL;
	unsigned long iov_pfn = param->iov_pfn;
	unsigned long phys_pfn = param->phys_pfn;
	unsigned long nr_pages = param->nr_pages;
	int prot = param->prot;
	unsigned int largepage_lvl = 0;
	unsigned long lvl_pages = 0;
	phys_addr_t pteval;
	u64 attr;

	if (unlikely(!domain_pfn_supported(domain, iov_pfn + nr_pages - 1)))
		return -EINVAL;

	if ((prot & (DMA_PTE_READ|DMA_PTE_WRITE)) == 0)
		return -EINVAL;

	attr = prot & (DMA_PTE_READ | DMA_PTE_WRITE | DMA_PTE_SNP);
	attr |= DMA_FL_PTE_PRESENT;

	if (domain->use_first_level) {
		attr |= DMA_FL_PTE_US | DMA_FL_PTE_ACCESS;
		if (prot & DMA_PTE_WRITE)
			attr |= DMA_FL_PTE_DIRTY;
	}

	pteval = ((phys_addr_t)phys_pfn << VTD_PAGE_SHIFT) | attr;

	while (nr_pages > 0) {
		uint64_t tmp;

		if (!pte) {
			largepage_lvl = hardware_largepage_caps(domain, iov_pfn,
					phys_pfn, nr_pages);

			pte = pfn_to_dma_pte(domain, donation, iov_pfn, &largepage_lvl);
			if (!pte) {
				if (donation->nr_pages < 0)
					return -ENOMEM;
				else
					return -EFAULT;
			}
			first_pte = pte;

			lvl_pages = lvl_to_nr_pages(largepage_lvl);

			/* It is large page*/
			if (largepage_lvl > 1) {
				unsigned long end_pfn;
				unsigned long pages_to_remove;

				pteval |= DMA_PTE_LARGE_PAGE;
				pages_to_remove = min_t(unsigned long, nr_pages,
							nr_pte_to_next_page(pte) * lvl_pages);
				end_pfn = iov_pfn + pages_to_remove - 1;
				switch_to_super_page(domain, donation,
						iov_pfn, end_pfn, largepage_lvl);
			} else {
				pteval &= ~(uint64_t)DMA_PTE_LARGE_PAGE;
			}

		}

		if (!verify_page_onwership(pteval & VTD_PAGE_MASK, largepage_lvl))
			return -EPERM;

		/* We don't need lock here, nobody else
		 * touches the iova range
		 */
		tmp = 0ULL;
		if (!try_cmpxchg64_local(&pte->val, &tmp, pteval) && (tmp != pteval)) {
			pkvm_err("ERROR: DMA PTE for vPFN 0x%lx already set (to %llx not %llx)\n",
				iov_pfn, tmp, (unsigned long long)pteval);
		}

		nr_pages -= lvl_pages;
		iov_pfn += lvl_pages;
		phys_pfn += lvl_pages;
		pteval += lvl_pages * VTD_PAGE_SIZE;

		/* If the next PTE would be the first in a new page, then we
		 * need to flush the cache on the entries we've just written.
		 * And then we'll need to recalculate 'pte', so clear it and
		 * let it get set again in the if (!pte) block above.
		 *
		 * If we're done (!nr_pages) we need to flush the cache too.
		 *
		 * Also if we've been setting superpages, we may need to
		 * recalculate 'pte' and switch back to smaller pages for the
		 * end of the mapping, if the trailing size is not enough to
		 * use another superpage (i.e. nr_pages < lvl_pages).
		 */
		pte++;
		if (!nr_pages || first_pte_in_page(pte) ||
		    (largepage_lvl > 1 && nr_pages < lvl_pages)) {
			pkvm_clflush_cache_range(first_pte, (void *)pte - (void *)first_pte);
			pte = NULL;
		}
	}

	for (phys_pfn = param->phys_pfn; phys_pfn < param->phys_pfn + param->nr_pages; phys_pfn++) {
		struct hyp_page *page = hyp_phys_to_page_safe(phys_pfn << VTD_PAGE_SHIFT);
		if (page)
			hyp_page_ref_inc(page);
	}

	return 0;
}

unsigned long pkvm_iommu_domain_map(unsigned long param_va)
{
	union pkvm_iommu_page_donation *donation;
	struct pkvm_iommu_map_param *param;
	struct pkvm_iommu_domain *domain;
	unsigned long ret = 0;

	if (!param_va)
		return -EINVAL;

	param = (struct pkvm_iommu_map_param *)kern_pkvm_va((void *)param_va);
	if (WARN_ON_ONCE(param != this_pv_param(iommu_map_param)))
		return -EINVAL;

	donation = this_cpu_read(iommu_page_donation);

	domain = pkvm_get_iommu_domain(host_gpa2hpa(param->pgd_gpa));
	if (!domain) {
		pkvm_err("pkvm: %s, failed to get the domain [pgd:%llx]\n",
				__func__, param->pgd_gpa);
		return -EINVAL;
	}
	pkvm_spin_lock(&domain->lock);
	ret = domain_map(domain, param, donation);
	if (ret == 0)
		pkvm_cache_tag_flush_range_np(domain, param->iov_pfn << VTD_PAGE_SHIFT,
				(param->iov_pfn + param->nr_pages - 1) << VTD_PAGE_SHIFT);
	pkvm_spin_unlock(&domain->lock);
	pkvm_put_iommu_domain(domain);

	return ret;
}

static inline void decrease_page_ref(unsigned long addr, int level)
{
	unsigned long phys;
	struct hyp_page *page = hyp_phys_to_page_safe(addr);

	if (!page)
		return;

	for (phys = addr; phys < addr + (level_size(level) * VTD_PAGE_SIZE); phys += VTD_PAGE_SIZE)
		hyp_page_ref_dec(hyp_phys_to_page(phys));
}

/* When a page at a given level is being unlinked from its parent, we don't
   need to *modify* it at all. All we need to do is make a list of all the
   pages which can be freed just as soon as we've flushed the IOTLB and we
   know the hardware page-walk will no longer touch them.
   The 'pte' argument is the *parent* PTE, pointing to the page that is to
   be freed. */
static void dma_pte_list_pagetables(struct pkvm_iommu_domain *domain,
				    int level, struct dma_pte *pte,
				    union pkvm_iommu_page_donation *donation)
{
	u64 phys = dma_pte_addr(pte);

	if (level == 1) {
		pte = pkvm_phys_to_virt(dma_pte_addr(pte));
		do {
			if (dma_pte_present(pte))
				decrease_page_ref(dma_pte_addr(pte), 1);
			pte++;
		} while (!first_pte_in_page(pte));
		goto out;
	}

	pte = pkvm_phys_to_virt(dma_pte_addr(pte));
	do {
		if (dma_pte_present(pte)) {
			if (!dma_pte_superpage(pte))
				dma_pte_list_pagetables(domain, level - 1, pte, donation);
			else
				decrease_page_ref(dma_pte_addr(pte), level);
		}
		pte++;
	} while (!first_pte_in_page(pte));

out:
	memset(pkvm_phys_to_virt(phys), 0, VTD_PAGE_SIZE);
	if (__pkvm_hyp_donate_host_unshare_ro(phys, VTD_PAGE_SIZE)) {
		pkvm_err("pkvm: %s: failed to remove write protect page!\n", __func__);
		return;
	}
	donation->pages[donation->nr_pages++] = phys;
	PKVM_ASSERT(donation->nr_pages <= PKVM_MAX_IOMMU_PAGE_DONATION);
}

static void dma_pte_clear_level(struct pkvm_iommu_domain *domain, int level,
				struct dma_pte *pte, unsigned long pfn,
				unsigned long start_pfn, unsigned long last_pfn,
				union pkvm_iommu_page_donation *donation)
{
	struct dma_pte *first_pte = NULL, *last_pte = NULL;

	pfn = max(start_pfn, pfn);
	pte = &pte[pfn_level_offset(pfn, level)];

	do {
		unsigned long level_pfn = pfn & level_mask(level);

		if (!dma_pte_present(pte))
			goto next;

		/* If range covers entire pagetable, free it */
		if (start_pfn <= level_pfn &&
		    last_pfn >= level_pfn + level_size(level) - 1) {
			/* These suborbinate page tables are going away entirely. Don't
			   bother to clear them; we're just going to *free* them. */
			if (level > 1 && !dma_pte_superpage(pte))
				dma_pte_list_pagetables(domain, level - 1, pte, donation);
			else
				decrease_page_ref(dma_pte_addr(pte), level);
			dma_clear_pte(pte);
			if (!first_pte)
				first_pte = pte;
			last_pte = pte;
		} else if (level > 1) {
			/* Recurse down into a level that isn't *entirely* obsolete */
			dma_pte_clear_level(domain, level - 1,
					    pkvm_phys_to_virt(dma_pte_addr(pte)),
					    level_pfn, start_pfn, last_pfn,
					    donation);
		}
next:
		pfn = level_pfn + level_size(level);
	} while (!first_pte_in_page(++pte) && pfn <= last_pfn);

	if (first_pte)
		domain_flush_cache(domain, first_pte,
				   (void *)++last_pte - (void *)first_pte);
}

/* We can't just free the pages because the IOMMU may still be walking
   the page tables, and may have cached the intermediate levels. The
   pages can only be freed after the IOTLB flush has been done. */
static void domain_unmap(struct pkvm_iommu_domain *domain, unsigned long start_pfn,
			 unsigned long last_pfn, union pkvm_iommu_page_donation *donation)
{
	if (WARN_ON(!domain_pfn_supported(domain, last_pfn)) ||
	    WARN_ON(start_pfn > last_pfn))
		return;

	/* we don't need lock here; nobody else touches the iova range */
	dma_pte_clear_level(domain, agaw_to_level(domain->agaw),
			    pkvm_phys_to_virt(domain->pgd), 0, start_pfn, last_pfn, donation);
}

/*
 * TODO: See if kvm_vcpu can go away.
 * TODO: Do away with the fixed donation array.
 */
unsigned long pkvm_iommu_domain_unmap(unsigned long pgd_gpa, unsigned long start_pfn,
		unsigned long last_pfn)
{
	unsigned long start = start_pfn << VTD_PAGE_SHIFT;
	unsigned long end = last_pfn << VTD_PAGE_SHIFT;
	union pkvm_iommu_page_donation *donation;
	struct pkvm_iommu_domain *domain;
	int nr_pages;

	donation = this_cpu_read(iommu_page_donation);
	nr_pages = donation->nr_pages;

	domain = pkvm_get_iommu_domain(host_gpa2hpa(pgd_gpa));
	if (!domain) {
		pkvm_err("pkvm: %s, failed to get the domain [pgd:%lx]\n",
				__func__, pgd_gpa);
		return -EINVAL;
	}

	pkvm_spin_lock(&domain->lock);
	domain_unmap(domain, start_pfn, last_pfn, donation);

	/*
	 * Regardless of the DMA mode used by host, we perform iotlb flush on
	 * unmap. Unmapped pages may be donated to a pvm and pvm could use
	 * it to store sensitve data. Until a flush happens, stale entries in
	 * cache could enable a device to read those pages which might contain
	 * sensitive data. So perform flush unconditionally.
	 */
	/*
	 * No new pages released during unmap implies only the leaf
	 * PTEs were updated. Set IH=1(Invalidation Hint) in that case.
	 */
	pkvm_cache_tag_flush_range(domain, start, end,
				nr_pages == donation->nr_pages);
	pkvm_spin_unlock(&domain->lock);
	pkvm_put_iommu_domain(domain);

	return 0;
}
