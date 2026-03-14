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

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct dentry *mmdir;

struct alloc_type {
	int nodeid;
	int zoneid;
	int order;
	int migrate_type;
	struct dentry *migratedir;
};

#define ALLOC_TYPE_SIZE sizeof(struct alloc_type)

struct kmem_cache *alloc_types_cache;

static int alloc_type_open(struct inode *inode, struct file *file)
{
	struct alloc_type *alloc = (struct alloc_type *)inode->i_private;

	pr_info("Open file with node=%d zone=%d order=%d migrate_type=%d",
		alloc->nodeid, alloc->zoneid, alloc->order,
		alloc->migrate_type);

	file->private_data = alloc;

	return 0;
}

static int alloc_type_release(struct inode *inode, struct file *file)
{
	struct alloc_type *alloc = (struct alloc_type *)inode->i_private;

	pr_info("Release file with node=%d zone=%d order=%d migrate_type=%d",
		alloc->nodeid, alloc->zoneid, alloc->order,
		alloc->migrate_type);

	return 0;
}

static ssize_t alloc_type_read(struct file *file, char __user *buf, size_t lbuf,
			       loff_t *ppos)
{
	struct alloc_type *alloc = file->private_data;

	pr_info("Reading: node=%d zone=%d order=%d migrate_type=%d",
		alloc->nodeid, alloc->zoneid, alloc->order,
		alloc->migrate_type);

	return 0;
}

static ssize_t alloc_type_write(struct file *file, const char __user *buf,
				size_t lbuf, loff_t *ppos)
{
	struct alloc_type *alloc = file->private_data;

	pr_info("Writing: node=%d zone=%d order=%d migrate_type=%d",
		alloc->nodeid, alloc->zoneid, alloc->order,
		alloc->migrate_type);

	return lbuf;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = alloc_type_open,
	.release = alloc_type_release,
	.read = alloc_type_read,
	.write = alloc_type_write,
};

static inline int create_page_alloc_files(struct dentry *migratedir, int nodeid,
					  int zoneid, int order, int mtype)
{
	struct alloc_type *alloc;
	struct dentry *alloc_file;

	alloc = kmem_cache_alloc(alloc_types_cache, GFP_ATOMIC);
	if (!alloc) {
		pr_err("Failed to create alloc_type");
		return -ENOMEM;
	}

	alloc->nodeid = nodeid;
	alloc->zoneid = zoneid;
	alloc->order = order;
	alloc->migrate_type = mtype;
	alloc->migratedir = migratedir;

	alloc_file =
		debugfs_create_file("alloc", 0644, migratedir, alloc, &fops);
	if (IS_ERR(alloc_file))
		return PTR_ERR(alloc_file);

	return 0;
}

static inline int create_migrate_type_subdirs(struct dentry *orderdir,
					      int nodeid, int zoneid, int order)
{
	struct dentry *migratedir;
	char dirname[24];
	int ret;

	for (int mtype = 0; mtype < MIGRATE_TYPES; mtype++) {
		snprintf(dirname, sizeof(dirname), "migrate-%s",
			 migratetype_names[mtype]);
		migratedir = debugfs_create_dir(dirname, orderdir);
		if (IS_ERR(migratedir))
			return PTR_ERR(migratedir);

		ret = create_page_alloc_files(migratedir, nodeid, zoneid, order,
					      mtype);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int create_page_orders_subdirs(struct dentry *zonedir, int nodeid,
					     int zoneid, struct zone *zone)
{
	struct dentry *orderdir;
	struct free_area *free_area;
	char dirname[12];
	int ret;

	for (int order = 0; order < NR_PAGE_ORDERS; order++) {
		free_area = &(zone->free_area[order]);
		snprintf(dirname, sizeof(dirname), "order-%d", order);
		orderdir = debugfs_create_dir(dirname, zonedir);
		if (IS_ERR(orderdir))
			return PTR_ERR(orderdir);

		ret = create_migrate_type_subdirs(orderdir, nodeid, zoneid,
						  order);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int create_zones_subdirs(struct dentry *nodedir, int nodeid,
				       struct pglist_data *pgdata)
{
	struct dentry *zonedir;
	struct zone *zone;
	struct zone *node_zones = pgdata->node_zones;
	int zoneid;
	char dirname[24];
	int ret;

	for (zone = node_zones, zoneid = 0; zone - node_zones < MAX_NR_ZONES;
	     ++zone, ++zoneid) {
		if (!populated_zone(zone))
			continue;

		snprintf(dirname, sizeof(dirname), "zone-%s", zone->name);
		zonedir = debugfs_create_dir(dirname, nodedir);
		if (IS_ERR(zonedir))
			return PTR_ERR(zonedir);

		ret = create_page_orders_subdirs(zonedir, nodeid, zoneid, zone);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int create_nodes_subdirs(struct dentry *mmdir)
{
	struct dentry *nodedir;
	int nodeid;
	char dirname[12];
	int ret;

	for_each_online_node(nodeid) {
		struct pglist_data *pgdata = NODE_DATA(nodeid);

		snprintf(dirname, sizeof(dirname), "node-%d", nodeid);
		nodedir = debugfs_create_dir(dirname, mmdir);
		if (IS_ERR(nodedir))
			return PTR_ERR(nodedir);

		ret = create_zones_subdirs(nodedir, nodeid, pgdata);

		if (ret)
			return ret;
	}

	return 0;
}
static int __init page_alloc_debugfs_init(void)
{
	int ret;

	pr_info("Starting");
	alloc_types_cache = kmem_cache_create("alloc_types_cache",
					      ALLOC_TYPE_SIZE, 0,
					      SLAB_HWCACHE_ALIGN, NULL);
	if (!alloc_types_cache) {
		pr_err("The alloc_types_cache couldn't be created");
		return -ENOMEM;
	}

	mmdir = debugfs_create_dir("mm", NULL);
	if (IS_ERR(mmdir)) {
		pr_err("Unable to create mm directory");
		ret = PTR_ERR(mmdir);
		goto clean_cache;
	}

	ret = create_nodes_subdirs(mmdir);
	if (ret)
		goto clean_dir;

	return 0;

clean_dir:
	debugfs_remove_recursive(mmdir);

clean_cache:
	kmem_cache_destroy(alloc_types_cache);

	return ret;
}

static void __exit page_alloc_debugfs_exit(void)
{
	debugfs_remove_recursive(mmdir);
	kmem_cache_destroy(alloc_types_cache);
}

module_init(page_alloc_debugfs_init);
module_exit(page_alloc_debugfs_exit);

MODULE_AUTHOR("Juan Yescas");
MODULE_DESCRIPTION("Module to alloc pages using the debugfs filesystem");
MODULE_LICENSE("GPL v2");


