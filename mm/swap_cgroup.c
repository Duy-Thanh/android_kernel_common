// SPDX-License-Identifier: GPL-2.0
#include <linux/swap_cgroup.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

#include <linux/swapops.h> /* depends on mm.h include */

static DEFINE_MUTEX(swap_cgroup_mutex);
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)

/* Pack two cgroup id (short) of two entries in one swap_cgroup (atomic_t) */
#define ID_PER_SC (sizeof(struct swap_cgroup) / sizeof(unsigned short))
#define ID_SHIFT (BITS_PER_TYPE(unsigned short))
#define ID_MASK (BIT(ID_SHIFT) - 1)
struct swap_cgroup {
	atomic_t ids;
};

struct swap_cgroup_ctrl {
	struct swap_cgroup *map;
};

static struct swap_cgroup_ctrl swap_cgroup_ctrl[MAX_SWAPFILES];

static unsigned short __swap_cgroup_id_lookup(struct swap_cgroup *map,
					      pgoff_t offset)
{
	unsigned int shift = (offset % ID_PER_SC) * ID_SHIFT;
	unsigned int old_ids = atomic_read(&map[offset / ID_PER_SC].ids);

	BUILD_BUG_ON(!is_power_of_2(ID_PER_SC));
	BUILD_BUG_ON(sizeof(struct swap_cgroup) != sizeof(atomic_t));

	return (old_ids >> shift) & ID_MASK;
}

static unsigned short __swap_cgroup_id_xchg(struct swap_cgroup *map,
					    pgoff_t offset,
					    unsigned short new_id)
{
	unsigned short old_id;
	struct swap_cgroup *sc = &map[offset / ID_PER_SC];
	unsigned int shift = (offset % ID_PER_SC) * ID_SHIFT;
	unsigned int new_ids, old_ids = atomic_read(&sc->ids);

	do {
		old_id = (old_ids >> shift) & ID_MASK;
		new_ids = (old_ids & ~(ID_MASK << shift));
		new_ids |= ((unsigned int)new_id) << shift;
	} while (!atomic_try_cmpxchg(&sc->ids, &old_ids, new_ids));

	return old_id;
}

/**
 * swap_cgroup_cmpxchg - cmpxchg mem_cgroup's id for this swp_entry.
 * @ent: swap entry to be cmpxchged
 * @old: old id
 * @new: new id
 *
 * Returns old id at success, 0 at failure.
 * (There is no mem_cgroup using 0 as its id)
 */
unsigned short swap_cgroup_cmpxchg(swp_entry_t ent,
					unsigned short old, unsigned short new)
{
	struct swap_cgroup *sc;
	struct swap_cgroup *map;
	unsigned short retval = 0;
	pgoff_t offset;
	unsigned int new_ids, old_ids;
	unsigned int shift;
	bool ret;

	map = swap_cgroup_ctrl[swp_type(ent)].map;
	offset = swp_offset(ent);
	sc = &map[offset / ID_PER_SC];
	old_ids = atomic_read(&sc->ids);
	shift = (offset % ID_PER_SC) * ID_SHIFT;

	while (((old_ids >> shift) & ID_MASK) == old) {
		new_ids = (old_ids & ~(ID_MASK << shift));
		new_ids |= ((unsigned int)new) << shift;
		ret = atomic_try_cmpxchg(&sc->ids, &old_ids, new_ids);
		if (ret) {
			retval = old;
			break;
		}
	}

	return retval;
}

/**
 * swap_cgroup_record - record mem_cgroup for a set of swap entries.
 * These entries must belong to one single folio, and that folio
 * must be being charged for swap space (swap out), and these
 * entries must not have been charged
 *
 * @folio: the folio that the swap entry belongs to
 * @id: mem_cgroup ID to be recorded
 * @ent: the first swap entry to be recorded
 */
void swap_cgroup_record(struct folio *folio, unsigned short id,
			swp_entry_t ent)
{
	unsigned int nr_ents = folio_nr_pages(folio);
	struct swap_cgroup *map;
	pgoff_t offset, end;
	unsigned short old;

	offset = swp_offset(ent);
	end = offset + nr_ents;
	map = swap_cgroup_ctrl[swp_type(ent)].map;

	do {
		old = __swap_cgroup_id_xchg(map, offset, id);
		VM_BUG_ON(old);
	} while (++offset != end);
}

/**
 * swap_cgroup_clear - clear mem_cgroup for a set of swap entries.
 * These entries must be being uncharged from swap. They either
 * belongs to one single folio in the swap cache (swap in for
 * cgroup v1), or no longer have any users (slot freeing).
 *
 * @ent: the first swap entry to be recorded into
 * @nr_ents: number of swap entries to be recorded
 *
 * Returns the existing old value.
 */
unsigned short swap_cgroup_clear(swp_entry_t ent, unsigned int nr_ents)
{
	pgoff_t offset = swp_offset(ent);
	pgoff_t end = offset + nr_ents;
	struct swap_cgroup *map;
	unsigned short old, iter = 0;

	offset = swp_offset(ent);
	end = offset + nr_ents;
	map = swap_cgroup_ctrl[swp_type(ent)].map;

	do {
		old = __swap_cgroup_id_xchg(map, offset, 0);
		if (!iter)
			iter = old;
		VM_BUG_ON(iter != old);
	} while (++offset != end);

	return old;
}

/**
 * lookup_swap_cgroup_id - lookup mem_cgroup id tied to swap entry
 * @ent: swap entry to be looked up.
 *
 * Returns ID of mem_cgroup at success. 0 at failure. (0 is invalid ID)
 */
unsigned short lookup_swap_cgroup_id(swp_entry_t ent)
{
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return 0;

	ctrl = &swap_cgroup_ctrl[swp_type(ent)];
	return __swap_cgroup_id_lookup(ctrl->map, swp_offset(ent));
}

int swap_cgroup_swapon(int type, unsigned long max_pages)
{
	struct swap_cgroup *map;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return 0;

	BUILD_BUG_ON(sizeof(unsigned short) * ID_PER_SC !=
		     sizeof(struct swap_cgroup));
	map = vzalloc(DIV_ROUND_UP(max_pages, ID_PER_SC) *
		      sizeof(struct swap_cgroup));
	if (!map)
		goto nomem;

	ctrl = &swap_cgroup_ctrl[type];
	mutex_lock(&swap_cgroup_mutex);
	ctrl->map = map;
	mutex_unlock(&swap_cgroup_mutex);

	return 0;
nomem:
	pr_info("couldn't allocate enough memory for swap_cgroup\n");
	pr_info("swap_cgroup can be disabled by swapaccount=0 boot option\n");
	return -ENOMEM;
}

void swap_cgroup_swapoff(int type)
{
	struct swap_cgroup *map;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return;

	mutex_lock(&swap_cgroup_mutex);
	ctrl = &swap_cgroup_ctrl[type];
	map = ctrl->map;
	ctrl->map = NULL;
	mutex_unlock(&swap_cgroup_mutex);

	vfree(map);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
struct swap_cgroup_ctrl {
	struct page **map;
	unsigned long length;
	spinlock_t	lock;
};

static struct swap_cgroup_ctrl swap_cgroup_ctrl[MAX_SWAPFILES];

struct swap_cgroup {
	unsigned short		id;
};
#define SC_PER_PAGE	(PAGE_SIZE/sizeof(struct swap_cgroup))

/*
 * SwapCgroup implements "lookup" and "exchange" operations.
 * In typical usage, this swap_cgroup is accessed via memcg's charge/uncharge
 * against SwapCache. At swap_free(), this is accessed directly from swap.
 *
 * This means,
 *  - we have no race in "exchange" when we're accessed via SwapCache because
 *    SwapCache(and its swp_entry) is under lock.
 *  - When called via swap_free(), there is no user of this entry and no race.
 * Then, we don't need lock around "exchange".
 *
 * TODO: we can push these buffers out to HIGHMEM.
 */

/*
 * allocate buffer for swap_cgroup.
 */
static int swap_cgroup_prepare(int type)
{
	struct page *page;
	struct swap_cgroup_ctrl *ctrl;
	unsigned long idx, max;

	ctrl = &swap_cgroup_ctrl[type];

	for (idx = 0; idx < ctrl->length; idx++) {
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			goto not_enough_page;
		ctrl->map[idx] = page;

		if (!(idx % SWAP_CLUSTER_MAX))
			cond_resched();
	}
	return 0;
not_enough_page:
	max = idx;
	for (idx = 0; idx < max; idx++)
		__free_page(ctrl->map[idx]);

	return -ENOMEM;
}

static struct swap_cgroup *__lookup_swap_cgroup(struct swap_cgroup_ctrl *ctrl,
						pgoff_t offset)
{
	struct page *mappage;
	struct swap_cgroup *sc;

	mappage = ctrl->map[offset / SC_PER_PAGE];
	sc = page_address(mappage);
	return sc + offset % SC_PER_PAGE;
}

static struct swap_cgroup *lookup_swap_cgroup(swp_entry_t ent,
					struct swap_cgroup_ctrl **ctrlp)
{
	pgoff_t offset = swp_offset(ent);
	struct swap_cgroup_ctrl *ctrl;

	ctrl = &swap_cgroup_ctrl[swp_type(ent)];
	if (ctrlp)
		*ctrlp = ctrl;
	return __lookup_swap_cgroup(ctrl, offset);
}

/**
 * swap_cgroup_cmpxchg - cmpxchg mem_cgroup's id for this swp_entry.
 * @ent: swap entry to be cmpxchged
 * @old: old id
 * @new: new id
 *
 * Returns old id at success, 0 at failure.
 * (There is no mem_cgroup using 0 as its id)
 */
unsigned short swap_cgroup_cmpxchg(swp_entry_t ent,
					unsigned short old, unsigned short new)
{
	struct swap_cgroup_ctrl *ctrl;
	struct swap_cgroup *sc;
	unsigned long flags;
	unsigned short retval;

	sc = lookup_swap_cgroup(ent, &ctrl);

	spin_lock_irqsave(&ctrl->lock, flags);
	retval = sc->id;
	if (retval == old)
		sc->id = new;
	else
		retval = 0;
	spin_unlock_irqrestore(&ctrl->lock, flags);
	return retval;
}

/**
 * swap_cgroup_record - record mem_cgroup for a set of swap entries
 * @ent: the first swap entry to be recorded into
 * @id: mem_cgroup to be recorded
 * @nr_ents: number of swap entries to be recorded
 *
 * Returns old value at success, 0 at failure.
 * (Of course, old value can be 0.)
 */
unsigned short swap_cgroup_record(swp_entry_t ent, unsigned short id,
				  unsigned int nr_ents)
{
	struct swap_cgroup_ctrl *ctrl;
	struct swap_cgroup *sc;
	unsigned short old;
	unsigned long flags;
	pgoff_t offset = swp_offset(ent);
	pgoff_t end = offset + nr_ents;

	sc = lookup_swap_cgroup(ent, &ctrl);

	spin_lock_irqsave(&ctrl->lock, flags);
	old = sc->id;
	for (;;) {
		VM_BUG_ON(sc->id != old);
		sc->id = id;
		offset++;
		if (offset == end)
			break;
		if (offset % SC_PER_PAGE)
			sc++;
		else
			sc = __lookup_swap_cgroup(ctrl, offset);
	}
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return old;
}

/**
 * lookup_swap_cgroup_id - lookup mem_cgroup id tied to swap entry
 * @ent: swap entry to be looked up.
 *
 * Returns ID of mem_cgroup at success. 0 at failure. (0 is invalid ID)
 */
unsigned short lookup_swap_cgroup_id(swp_entry_t ent)
{
	if (mem_cgroup_disabled())
		return 0;
	return lookup_swap_cgroup(ent, NULL)->id;
}

int swap_cgroup_swapon(int type, unsigned long max_pages)
{
	void *array;
	unsigned long length;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return 0;

	length = DIV_ROUND_UP(max_pages, SC_PER_PAGE);

	array = vcalloc(length, sizeof(void *));
	if (!array)
		goto nomem;

	ctrl = &swap_cgroup_ctrl[type];
	mutex_lock(&swap_cgroup_mutex);
	ctrl->length = length;
	ctrl->map = array;
	spin_lock_init(&ctrl->lock);
	if (swap_cgroup_prepare(type)) {
		/* memory shortage */
		ctrl->map = NULL;
		ctrl->length = 0;
		mutex_unlock(&swap_cgroup_mutex);
		vfree(array);
		goto nomem;
	}
	mutex_unlock(&swap_cgroup_mutex);

	return 0;
nomem:
	pr_info("couldn't allocate enough memory for swap_cgroup\n");
	pr_info("swap_cgroup can be disabled by swapaccount=0 boot option\n");
	return -ENOMEM;
}

void swap_cgroup_swapoff(int type)
{
	struct page **map;
	unsigned long i, length;
	struct swap_cgroup_ctrl *ctrl;

	if (mem_cgroup_disabled())
		return;

	mutex_lock(&swap_cgroup_mutex);
	ctrl = &swap_cgroup_ctrl[type];
	map = ctrl->map;
	length = ctrl->length;
	ctrl->map = NULL;
	ctrl->length = 0;
	mutex_unlock(&swap_cgroup_mutex);

	if (map) {
		for (i = 0; i < length; i++) {
			struct page *page = map[i];
			if (page)
				__free_page(page);
			if (!(i % SWAP_CLUSTER_MAX))
				cond_resched();
		}
		vfree(map);
	}
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
}
