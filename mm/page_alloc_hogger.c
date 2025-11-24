// SPDX-License-Identifier: GPL-2.0
/*
 * Page allocator DebugFS Interface
 *
 * This interface allows to make page allocations per node, zone, migrate type
 * and order using the DebugFS filesystem.
 *
 * The directory and files will be created in:
 *
 *    /sys/kernel/debug/mm
 *
 * For example, if we want to allocate pages from Node 0, Normal Zone, Order 11
 * and MIGRATE_MOVABLE, we run the command:
 *
 *    echo 1 > /sys/kernel/debug/mm/node-0/zone-Normal/order-11/migrate-Movable
 *
 * Before using this feature, remember to create the debug directory
 * and mount the debugfs filesystem:
 *
 *     mkdir -p /sys/kernel/debug
 *     mount -t debugfs none /sys/kernel/debug
 *
 * Another option is to modify the /etc/fstab and add this entry:
 *
 *    # <file system> <mount pt>      <type>  <options>       <dump>  <pass>
 *    debugfs    /sys/kernel/debug    debugfs    defaults    0    0
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cma.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/gfp.h>
#include <linux/gfp_types.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/xarray.h>

struct dentry *mmdir;

/*
 * It is used to name the file that is created for an allocation. It is
 * increase atomically after that.
 */
atomic_long_t pages_allocs_seq = ATOMIC_INIT(0);
static DEFINE_XARRAY(pages_allocs_xa);

struct pages_allocs_info {
	int node_idx;
	int zone_idx;
	int order;
	int migrate_type;
	struct dentry *parentdir;
};

/*
 * This struct contains the pointer to the page(s) allocated and
 * the information about the allocation.
 */
struct pages_allocated {
	struct page *page;
	struct pages_allocs_info *pages_allocs_info;
	struct dentry *pages_allocs_dentry;
};

struct kmem_cache *nr_pages_allocs_info_cache;
struct kmem_cache *pages_allocated_cache;

inline void set_zone_to_alloc_from(int zone_idx, gfp_t *flags_ptr)
{
	switch (zone_idx) {
#ifdef CONFIG_ZONE_DMA
	case ZONE_DMA:
		*flags_ptr |= __GFP_DMA;
		break;
#endif
#ifdef CONFIG_ZONE_DMA32
	case ZONE_DMA32:
		*flags_ptr |= __GFP_DMA32;
		break;
#endif
#ifdef CONFIG_HIGHMEM
	case ZONE_HIGHMEM:
		*flags_ptr |= __GFP_HIGHMEM;
		break;
#endif
	case ZONE_MOVABLE:
		*flags_ptr |= __GFP_MOVABLE;
		break;
	case ZONE_NORMAL:
	default:
		*flags_ptr |= 0;
		break;
	}
}

inline void set_migrate_type_to_alloc_from(int migrate_type, gfp_t *flags_ptr)
{
	switch (migrate_type) {
	case MIGRATE_UNMOVABLE:
		*flags_ptr &= ~(__GFP_RECLAIMABLE | __GFP_MOVABLE);
		break;
	case MIGRATE_RECLAIMABLE:
		*flags_ptr |= __GFP_RECLAIMABLE;
		break;
	case MIGRATE_HIGHATOMIC:
		*flags_ptr |= __GFP_HIGH;
		break;
	case MIGRATE_MOVABLE: default:
		*flags_ptr |= __GFP_MOVABLE;
		break;
	}
}

static ssize_t pages_allocated_read(struct file *file, char __user *ubuf,
				    size_t cnt, loff_t *ppos)
{
	struct pages_allocated *pgs_allocated = file->private_data;
	struct pages_allocs_info *alloc_info = pgs_allocated->pages_allocs_info;
	unsigned long pfn;

	pfn = page_to_pfn(pgs_allocated->page);
	pr_info("PFN: %lu\n", pfn);
	pr_info("Node: %d\n", alloc_info->node_idx);
	pr_info("Zone: %d\n", alloc_info->zone_idx);
	pr_info("Order: %d\n", alloc_info->order);
	pr_info("Migrate Type: %s\n", migratetype_names[alloc_info->migrate_type]);

	return 0;
}

static const struct file_operations pages_allocated_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = pages_allocated_read,
};

static int make_pages_alloc(struct pages_allocs_info *alloc_info,
			    gfp_t flags, unsigned long *nr_pages_allocs_id)
{
	struct pages_allocated *pgs_allocated;
	struct page *page;
	char new_alloc_name[12];
	struct dentry *new_alloc_dentry;
	int ret;

	page = alloc_pages_node_noprof(alloc_info->node_idx, flags,
				       alloc_info->order);
	if (page) {
		pgs_allocated =
			kmem_cache_alloc(pages_allocated_cache, GFP_KERNEL);
		if (!pgs_allocated) {
			ret = -ENOMEM;
			goto free_pages;
		}

		*nr_pages_allocs_id = atomic_long_inc_return(&pages_allocs_seq);
		ret = xa_insert(&pages_allocs_xa, *nr_pages_allocs_id, pgs_allocated, GFP_KERNEL);
		if (ret)
			goto free_pages_allocated;

		snprintf(new_alloc_name, sizeof(new_alloc_name), "%lu",
			 *nr_pages_allocs_id);

		new_alloc_dentry = debugfs_create_file(
			new_alloc_name, 0644, alloc_info->parentdir,
			pgs_allocated, &pages_allocated_fops);
		if (IS_ERR(new_alloc_dentry)) {
			ret = PTR_ERR(new_alloc_dentry);
			goto erase_xa_entry;
		}

		pgs_allocated->pages_allocs_info = alloc_info;
		pgs_allocated->page = page;
		pgs_allocated->pages_allocs_dentry = new_alloc_dentry;
	} else {
		return -ENOMEM;
	}

	return 0;

erase_xa_entry:
	xa_erase(&pages_allocs_xa, *nr_pages_allocs_id);

free_pages_allocated:
	kmem_cache_free(pages_allocated_cache, pgs_allocated);

free_pages:
	__free_pages(page, pgs_allocated->pages_allocs_info->order);

	return ret;
}

static int free_pages_allocs_helper(unsigned long nr_pages_allocs_id)
{
	struct pages_allocated *pages_allocated;

	pages_allocated = xa_erase(&pages_allocs_xa, nr_pages_allocs_id);
	if (!pages_allocated) {
		pr_err("The pages allocs id %lu was not found!",
		       nr_pages_allocs_id);
		return -EINVAL;
	}

	__free_pages(pages_allocated->page,
		     pages_allocated->pages_allocs_info->order);
	debugfs_remove(pages_allocated->pages_allocs_dentry);
	kmem_cache_free(pages_allocated_cache, pages_allocated);

	return 0;
}

static ssize_t nr_pages_allocs_write(struct file *file, const char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	struct pages_allocs_info *pages_allocs_info = file->private_data;
	unsigned long nr_pages_allocs_id;
	unsigned long nr_pages_allocs;
	unsigned long *page_allocs_ids;
	gfp_t flags = 0;
	int ret;
	int i;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &nr_pages_allocs);
	if (ret)
		return ret;

	page_allocs_ids = kmalloc_array(nr_pages_allocs, sizeof(unsigned long),
					GFP_KERNEL);
	if (!page_allocs_ids)
		return -ENOMEM;

	set_zone_to_alloc_from(pages_allocs_info->zone_idx, &flags);
	set_migrate_type_to_alloc_from(pages_allocs_info->migrate_type,
				       &flags);

	for (i = 0; i < nr_pages_allocs; i++) {
		ret = make_pages_alloc(pages_allocs_info, flags,
				       &nr_pages_allocs_id);
		if (ret)
			goto free_pages_allocated;

		page_allocs_ids[i] = nr_pages_allocs_id;
	}

	kfree(page_allocs_ids);

	return cnt;

free_pages_allocated:
	for (int j = 0; j < i; j++) {
		int ret2 = free_pages_allocs_helper(page_allocs_ids[j]);

		if (ret2) {
			pr_err("Unavaile to free pages associated with the pages allocs id %lu",
			       page_allocs_ids[j]);
		}
	}

	kfree(page_allocs_ids);

	return ret;
}

static const struct file_operations nr_pages_allocs_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = nr_pages_allocs_write,
};

/*
 * free_write() - free the previously allocated pages.
 * @file: File where the user is writing.
 * @buf: Contains the name of the file the user wants to delete and have the
 *       memory associated with that file deleted.
 * @lbuf: Size of the @buf.
 * @ppos: Current position in the file.
 */
static ssize_t free_page_allocs_write(struct file *file,
				      const char __user *ubuf, size_t cnt,
				      loff_t *ppos)
{
	int ret;
	unsigned long nr_pages_allocs_id;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &nr_pages_allocs_id);
	if (ret)
		return ret;

	ret = free_pages_allocs_helper(nr_pages_allocs_id);
	if (ret)
		return ret;

	return cnt;
}

static const struct file_operations free_page_allocs_fops = {
	.owner = THIS_MODULE,
	.write = free_page_allocs_write,
};

/*
 * Creates the file "nr_pages_allocs".
 *
 * - The "nr_pages_allocs" file will be used to write the number of allocations that
 *   will be performed. When the allocations are performed, a new file for each
 *   allocation will be created in @migratedir.
 */
static inline int create_nr_pages_allocs_file(struct dentry *migratedir,
					      int node_idx, int zone_idx,
					      int order, int mtype)
{
	struct pages_allocs_info *allocs_info;
	struct dentry *nr_allocs_file;

	allocs_info = kmem_cache_alloc(nr_pages_allocs_info_cache, GFP_KERNEL);
	if (!allocs_info) {
		pr_err("Failed to create nr_pages_allocs_info");
		return -ENOMEM;
	}

	allocs_info->node_idx = node_idx;
	allocs_info->zone_idx = zone_idx;
	allocs_info->order = order;
	allocs_info->migrate_type = mtype;
	allocs_info->parentdir = migratedir;

	nr_allocs_file = debugfs_create_file("nr_pages_allocs", 0644,
					     migratedir, allocs_info,
					     &nr_pages_allocs_fops);
	if (IS_ERR(nr_allocs_file))
		return PTR_ERR(nr_allocs_file);

	return 0;
}

/*
 * Creates a directory for each migrate type inside the order directory. Once
 * the migrate type directory is created, it creates the "nr_allocs" file and
 * the "free" file.
 */
static inline int create_migrate_type_subdirs(struct dentry *orderdir,
					      int node_idx, int zone_idx,
					      int order)
{
	struct dentry *migratedir;
	char dirname[24];
	int ret;

	for (int mtype = 0; mtype < MIGRATE_TYPES; mtype++) {
#ifdef CONFIG_CMA
		if (mtype == MIGRATE_CMA) /* CMA allocs are not supported yet*/
			continue;
#endif
#ifdef CONFIG_MEMORY_ISOLATION
		if (mtype == MIGRATE_ISOLATE) /* can't allocate from here */
			continue;
#endif
		snprintf(dirname, sizeof(dirname), "migrate-%s",
			 migratetype_names[mtype]);
		migratedir = debugfs_create_dir(dirname, orderdir);
		if (IS_ERR(migratedir))
			return PTR_ERR(migratedir);

		ret = create_nr_pages_allocs_file(migratedir, node_idx,
						  zone_idx, order, mtype);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Creates a directory for each page order inside the zone directory. Once that
 * the page order directory is created, it creates a directory for each migrate
 * type.
 */
static inline int create_page_orders_subdirs(struct dentry *zonedir,
					     int node_idx, int zone_idx,
					     struct zone *zone)
{
	struct dentry *orderdir;
	char dirname[12];
	int ret;

	for (int order = 0; order < NR_PAGE_ORDERS; order++) {
		snprintf(dirname, sizeof(dirname), "order-%d", order);
		orderdir = debugfs_create_dir(dirname, zonedir);
		if (IS_ERR(orderdir))
			return PTR_ERR(orderdir);

		ret = create_migrate_type_subdirs(orderdir, node_idx, zone_idx,
						  order);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Creates a directory for each populated zone in the node. Once that the zone
 * directory is created, it creates a directory for each order supported.
 *
 * Note: ZONE_DEVICE pages won't be allocated using this driver. The main reason
 * is because they are not managed by the Buddy Allocator or CMA allocator.
 */
static inline int create_zones_subdirs(struct dentry *nodedir, int node_idx,
				       struct pglist_data *pgdata)
{
	struct dentry *zonedir;
	struct zone *zone;
	struct zone *node_zones = pgdata->node_zones;
	int zone_idx;
	char dirname[24];
	int ret;

	for (zone = node_zones, zone_idx = 0; zone - node_zones < MAX_NR_ZONES;
	     ++zone, ++zone_idx) {
		if (!populated_zone(zone))
			continue;

		// ZONE_DEVICE pages won't be allocated using this driver.
		if (zone_is_zone_device(zone))
			continue;

		snprintf(dirname, sizeof(dirname), "zone-%s", zone->name);
		zonedir = debugfs_create_dir(dirname, nodedir);
		if (IS_ERR(zonedir))
			return PTR_ERR(zonedir);

		ret = create_page_orders_subdirs(zonedir, node_idx, zone_idx,
						 zone);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Creates a directory for each online node in the system. Once that the node
 * directory is created, it creates a directory for each zone in the node.
 *
 * This function also creates the "free" dir that is used to released the
 * page allocations made by the page alloc hogger.
 */
static inline int create_nodes_subdirs(struct dentry *mmdir)
{
	struct dentry *nodedir;
	struct dentry *free_file;
	int node_idx;
	char dirname[12];
	int ret;

	for_each_online_node(node_idx) {
		struct pglist_data *pgdata = NODE_DATA(node_idx);

		snprintf(dirname, sizeof(dirname), "node-%d", node_idx);
		nodedir = debugfs_create_dir(dirname, mmdir);
		if (IS_ERR(nodedir))
			return PTR_ERR(nodedir);

		ret = create_zones_subdirs(nodedir, node_idx, pgdata);

		if (ret)
			return ret;
	}

	free_file = debugfs_create_file("free", 0644, mmdir, NULL,
					&free_page_allocs_fops);
	if (IS_ERR(free_file))
		return PTR_ERR(free_file);

	return 0;
}

static int __init page_alloc_debugfs_init(void)
{
	int ret;

	nr_pages_allocs_info_cache =
		kmem_cache_create("nr_pages_allocs_info_cache",
				  sizeof(struct pages_allocs_info), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!nr_pages_allocs_info_cache) {
		pr_err("The nr_pages_allocs_info_cache couldn't be created");
		ret = -ENOMEM;
		goto error_exit;
	}

	pages_allocated_cache = kmem_cache_create(
		"pages_allocated_cache", sizeof(struct pages_allocated), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!pages_allocated_cache) {
		pr_err("The pages_allocated_cache couldn't be created");
		ret = -ENOMEM;
		goto clean_nr_pages_allocs_cache;
	}

	mmdir = debugfs_create_dir("mm", NULL);
	if (IS_ERR(mmdir)) {
		pr_err("Unable to create mm directory");
		ret = PTR_ERR(mmdir);
		goto clean_pages_allocated_cache;
	}

	ret = create_nodes_subdirs(mmdir);
	if (ret)
		goto clean_dir;

	return 0;

clean_dir:
	debugfs_remove_recursive(mmdir);

clean_pages_allocated_cache:
	kmem_cache_destroy(pages_allocated_cache);

clean_nr_pages_allocs_cache:
	kmem_cache_destroy(nr_pages_allocs_info_cache);

error_exit:

	return ret;
}

static void __exit page_alloc_debugfs_exit(void)
{
	struct pages_allocated *pages_allocated;
	unsigned long idx;

	xa_for_each(&pages_allocs_xa, idx, pages_allocated) {
		__free_pages(pages_allocated->page,
		     pages_allocated->pages_allocs_info->order);
		xa_erase(&pages_allocs_xa, idx);
	}

	debugfs_remove_recursive(mmdir);
	kmem_cache_destroy(nr_pages_allocs_info_cache);
	kmem_cache_destroy(pages_allocated_cache);
}

module_init(page_alloc_debugfs_init);
module_exit(page_alloc_debugfs_exit);

MODULE_AUTHOR("Juan Yescas");
MODULE_DESCRIPTION("Module to alloc pages using the debugfs filesystem");
MODULE_LICENSE("GPL");
