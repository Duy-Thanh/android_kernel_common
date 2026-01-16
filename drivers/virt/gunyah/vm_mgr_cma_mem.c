// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gunyah_vm_mgr_cma_mem: " fmt
#include <linux/anon_inodes.h>
#include <linux/cma.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/falloc.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/mm.h>

#include "vm_mgr.h"

struct gunyah_cma {
	struct device dev;
	struct file *file;
	struct page *page;
	struct miscdevice miscdev;
	struct list_head list;
	unsigned long max_size;
	unsigned long mapped_size;
	struct gunyah_vm *ghvm;
	struct gunyah_vm_binding *binding;
};

struct gunyah_cma_parent {
	struct list_head gunyah_cma_children;
};

/*
 * gunyah_cma_alloc - Allocate cma region.
 * @cma: the gunyah cma memory
 * @len: the size of the cma region.
 *
 * Uses cma_alloc to allocate contiguous memory region of size len.
 *
 * Return: The 0 on success or an error.
 */
static int gunyah_cma_alloc(struct gunyah_cma *cma, loff_t len)
{
	pgoff_t pagecount = len >> PAGE_SHIFT;
	unsigned long align = get_order(len);
	loff_t max_size;

	if (cma->page)
		return -EINVAL;

	max_size = i_size_read(file_inode(cma->file));
	if (len > max_size)
		return -EINVAL;

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	cma->page = cma_alloc(cma->dev.cma_area, pagecount, align, false);
	if (!cma->page)
		return -ENOMEM;

	if (len < max_size)
		dev_dbg(&cma->dev, "client mapped %lld bytes, less than max %lld bytes\n",
			len, max_size);

	cma->mapped_size = len;

	return 0;
}

static int gunyah_cma_release(struct inode *inode, struct file *file)
{
	struct gunyah_cma *cma = file->private_data;

	if (!cma->page)
		return 0;

	struct gunyah_vm_binding *binding = cma->binding;

	if (!binding)
		return 0;

	u64 start_gfn = (binding->guest_phys_addr >> PAGE_SHIFT);
	u64 count = (binding->size >> PAGE_SHIFT);

	for (u64 gfn = start_gfn; gfn < start_gfn + count; gfn++) {
		struct page *page = gunyah_gfn_to_page_get_mapping(binding, gfn);

		if (!page)
			continue;

		if (page >= cma->page &&
			page < (cma->page + (cma->mapped_size >> PAGE_SHIFT))) {
			cma_release(cma->dev.cma_area, page, 1);
			continue;
		} else {
			set_direct_map_default_noflush(page);
			__free_page(page);
		}
	}

	gunyah_gfn_to_page_mapping_unmap_range(binding, start_gfn, count);
	gunyah_gfn_to_page_mapping_destroy(binding);

	cma->page = NULL;
	return 0;
}

static int gunyah_cma_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gunyah_cma *cma = file->private_data;
	unsigned long len = vma->vm_end - vma->vm_start;
	int nr_pages = PAGE_ALIGN(len) >> PAGE_SHIFT;
	struct page **pages;
	int ret, i;

	file_accessed(file);

	pages = kvmalloc_array(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = gunyah_cma_alloc(cma, len);
	if (ret < 0) {
		kvfree(pages);
		return ret;
	}

	for (i = 0; i < nr_pages; i++)
		pages[i] = nth_page(cma->page, i);

	ret =  vm_map_pages_zero(vma, pages, nr_pages);
	if (ret)
		pr_err("Mapping memory failed: %d\n", ret);

	kvfree(pages);
	return ret;
}

int gunyah_cma_demand_page(struct gunyah_vm *ghvm, struct gunyah_vm_binding *b,
								u64 gpa, bool write)
{
	int ret = 0;
	unsigned long gfn = gunyah_gpa_to_gfn(gpa);
	struct folio *folio;

	if (write && !(b->flags & GUNYAH_MEM_ALLOW_WRITE))
		return -EPERM;

	struct page *page = alloc_page(GFP_KERNEL);

	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	folio = page_folio(page);

	folio_lock(folio);

	ret = gunyah_vm_provide_folio(ghvm, folio, gfn - folio_page_idx(folio, page),
				      !(b->share_type == VM_MEM_LEND),
				      !!(b->flags & GUNYAH_MEM_ALLOW_WRITE));
	folio_unlock(folio);
	if (ret) {
		if (ret != -EAGAIN)
			pr_err_ratelimited(
				"Failed to provide folio for guest addr: %016llx: %d\n",
				gpa, ret);
		goto out;
	}

	gunyah_gfn_to_page_mapping_map_range(b, gfn, page, 1);
out:
	return ret;
}

static long gunyah_cma_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct gunyah_cma *cma = file->private_data;
	loff_t end = offset + len;
	int ret = 0;

	if (!cma->ghvm)
		return -EINVAL;

	if (offset < 0 || len <= 0)
		return -EINVAL;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (!PAGE_ALIGNED(offset) || !PAGE_ALIGNED(len))
		return -EINVAL;

	if (!cma->page)
		return -EBUSY;

	if (end > cma->mapped_size)
		return -EFBIG;

	u64 start_idx = offset >> PAGE_SHIFT;

	struct gunyah_vm_binding *binding = cma->binding;
	u64 start_gfn = gunyah_gpa_to_gfn(binding->guest_phys_addr) + start_idx;
	u32 count = PAGE_ALIGN(len) >> PAGE_SHIFT;

	for (u64 gfn = start_gfn; gfn < start_gfn + count; gfn++) {
		struct page *page = gunyah_gfn_to_page_get_mapping(binding, gfn);

		if (!page)
			continue;

		if (page >= cma->page && page < (cma->page + (cma->mapped_size >> PAGE_SHIFT))) {
			cma_release(cma->dev.cma_area, page, 1);
			continue;
		} else {
			set_direct_map_default_noflush(page);
			__free_page(page);
		}
	}

	gunyah_gfn_to_page_mapping_unmap_range(binding, start_gfn, count);

	return ret;
}

static const struct file_operations gunyah_cma_fops = {
	.owner = THIS_MODULE,
	.llseek = generic_file_llseek,
	.mmap = gunyah_cma_mmap,
	.open = generic_file_open,
	.release = gunyah_cma_release,
	.fallocate = gunyah_cma_fallocate,
};

int gunyah_cma_reclaim_parcel(struct gunyah_vm *ghvm, struct gunyah_vm_parcel *vm_parcel,
				struct gunyah_vm_binding *b)
{
	struct gunyah_rm_mem_parcel *parcel = &vm_parcel->parcel;
	int ret;

	if (parcel->mem_handle == GUNYAH_MEM_HANDLE_INVAL)
		return 0;

	ret = gunyah_rm_mem_reclaim(ghvm->rm, parcel);
	if (ret) {
		dev_err(ghvm->parent, "Failed to reclaim parcel: %d\n",
			ret);
		/* We can't reclaim the pages -- hold onto the pages
		 * forever because we don't know what state the memory
		 * is in
		 */
		return ret;
	}
	parcel->mem_handle = GUNYAH_MEM_HANDLE_INVAL;
	kfree(parcel->mem_entries);
	kfree(parcel->acl_entries);
	vm_parcel->start = 0;
	vm_parcel->pages = 0;
	b->vm_parcel = NULL;
	fput(b->cma.file);
	return ret;
}


int gunyah_gfn_to_page_mapping_create(struct gunyah_vm_binding *binding)
{
	int ret = 0;

	binding->gfn_to_page = kzalloc(sizeof(*binding->gfn_to_page) *
					(binding->size >> PAGE_SHIFT),
					GFP_KERNEL);

	if (!binding->gfn_to_page) {
		ret = -ENOMEM;
		goto out;
	}

	spin_lock_init(&binding->gfn_to_page_lock);
out:
	return ret;
}

void gunyah_gfn_to_page_mapping_destroy(struct gunyah_vm_binding *binding)
{
	if (!binding || !binding->gfn_to_page)
		return;

	for (int ptr = 0; ptr < (binding->size >> PAGE_SHIFT); ptr++)
		binding->gfn_to_page[ptr] = NULL;

	kfree(binding->gfn_to_page);
}

struct page *gunyah_gfn_to_page_get_mapping(struct gunyah_vm_binding *binding, u64 gfn)
{
	struct page *page;

	if (!binding)
		return NULL;

	if ((gfn - (binding->guest_phys_addr >> PAGE_SHIFT)) >= (binding->size >> PAGE_SHIFT))
		return NULL;

	spin_lock(&binding->gfn_to_page_lock);
	page = binding->gfn_to_page[gfn - (binding->guest_phys_addr >> PAGE_SHIFT)];
	spin_unlock(&binding->gfn_to_page_lock);

	return page;
}

void gunyah_gfn_to_page_mapping_map_locked(struct gunyah_vm_binding *binding, u64 gfn,
					struct page *page)
{
	if (!binding || !page)
		return;

	if ((gfn - (binding->guest_phys_addr >> PAGE_SHIFT)) >= (binding->size >> PAGE_SHIFT))
		return;

	binding->gfn_to_page[gfn - (binding->guest_phys_addr >> PAGE_SHIFT)] = page;
}

void gunyah_gfn_to_page_mapping_unmap_locked(struct gunyah_vm_binding *binding, u64 gfn)
{
	if (!binding)
		return;

	if ((gfn - (binding->guest_phys_addr >> PAGE_SHIFT)) >= (binding->size >> PAGE_SHIFT))
		return;

	binding->gfn_to_page[gfn - (binding->guest_phys_addr >> PAGE_SHIFT)] = NULL;
}

void gunyah_gfn_to_page_mapping_map_range(struct gunyah_vm_binding *binding, u64 gfn,
					struct page *base, u64 count)
{
	u64 start_gfn = gfn;

	spin_lock(&binding->gfn_to_page_lock);

	for (; gfn < start_gfn + count; gfn++) {
		struct page *page = nth_page(base, gfn - start_gfn);

		gunyah_gfn_to_page_mapping_map_locked(binding, gfn, page);
	}

	spin_unlock(&binding->gfn_to_page_lock);
}

void gunyah_gfn_to_page_mapping_unmap_range(struct gunyah_vm_binding *binding, u64 gfn, u64 count)
{
	spin_lock(&binding->gfn_to_page_lock);
	while (count--) {
		gunyah_gfn_to_page_mapping_unmap_locked(binding, gfn);
		gfn++;
	}
	spin_unlock(&binding->gfn_to_page_lock);
}

int gunyah_cma_share_parcel(struct gunyah_vm *ghvm, struct gunyah_vm_parcel *vm_parcel,
				struct gunyah_vm_binding *b, u64 *gfn, u64 *nr)
{
	struct gunyah_rm_mem_parcel *parcel = &vm_parcel->parcel;
	unsigned long offset;
	struct gunyah_cma *cma;
	struct file *file;
	int ret;

	if ((*nr << PAGE_SHIFT) > b->size)
		return -EINVAL;

	file = fget(b->cma.fd);
	if (!file)
		return -EINVAL;

	if (file->f_op != &gunyah_cma_fops) {
		fput(file);
		return -EINVAL;
	}

	cma = file->private_data;
	b->cma.file = file;

	parcel->n_mem_entries = 1;
	parcel->mem_entries = kcalloc(parcel->n_mem_entries, sizeof(parcel->mem_entries[0]),
					GFP_KERNEL_ACCOUNT);
	if (!parcel->mem_entries) {
		fput(file);
		return -ENOMEM;
	}

	offset = gunyah_gfn_to_gpa(*gfn) - b->guest_phys_addr;
	parcel->mem_entries[0].size = cpu_to_le64(*nr << PAGE_SHIFT);
	parcel->mem_entries[0].phys_addr =
		cpu_to_le64(page_to_phys(cma->page + b->cma.offset + offset));

	ret = gunyah_rm_mem_share(ghvm->rm, parcel);
	if (ret)
		goto free_mem_entries;

	vm_parcel->start = *gfn;
	vm_parcel->pages = *nr;
	b->vm_parcel = vm_parcel;
	return ret;

free_mem_entries:
	kfree(parcel->mem_entries);
	parcel->mem_entries = NULL;
	parcel->n_mem_entries = 0;
	fput(file);
	return ret;
}

int gunyah_cma_unmap_from_userspace(struct gunyah_vm *ghvm)
{

	int ret = 0;
	struct gunyah_vm_binding *binding;

	unsigned long start = 0;
	unsigned long end = ULONG_MAX;

	down_read(&ghvm->bindings_lock);
	mt_for_each(&ghvm->bindings, binding, start, end) {
		if (binding && binding->mem_type == VM_MEM_CMA)
			break;
	}

	if (!binding) {
		ret = -ENOENT;
		goto out_unlock;
	}

	struct file *file = fget(binding->cma.fd);

	if (!file || !file->f_inode) {
		ret = -EINVAL;
		goto out_unlock;
	}

	struct vm_area_struct *vma;

	MA_STATE(mas, &current->mm->mm_mt, 0, 0);

	mmap_write_lock(current->mm);
	mas_for_each(&mas, vma, ULONG_MAX) {
		if (vma->vm_file == file) {
			unsigned long start_addr = vma->vm_start;
			unsigned long end_addr = vma->vm_end;
			unsigned long len = (end_addr - start_addr);

			zap_page_range_single(vma, start_addr, len, NULL);
		}
	}
	mmap_write_unlock(current->mm);

	fput(file);

out_unlock:
	up_read(&ghvm->bindings_lock);
	return ret;
}

int gunyah_vm_binding_cma_alloc(struct gunyah_vm *ghvm,
			    struct gunyah_map_cma_mem_args *cma_map)
{
	struct gunyah_vm_binding *binding;
	struct file *file;
	loff_t max_size;
	int ret = 0;

	if (!cma_map->size || !PAGE_ALIGNED(cma_map->size) ||
		!PAGE_ALIGNED(cma_map->guest_addr))
		return -EINVAL;

	if (overflows_type(cma_map->guest_addr + cma_map->size, u64))
		return -EOVERFLOW;

	file = fget(cma_map->guest_mem_fd);
	if (!file)
		return -EBADF;

	max_size = i_size_read(file_inode(file));
	if (cma_map->offset + cma_map->size > max_size) {
		fput(file);
		return -EOVERFLOW;
	}

	struct gunyah_cma *cma = file->private_data;

	if (!cma)
		return -ENODEV;

	fput(file);

	binding = kzalloc(sizeof(*binding), GFP_KERNEL_ACCOUNT);
	if (!binding)
		return -ENOMEM;

	binding->mem_type = VM_MEM_CMA;
	binding->cma.fd = cma_map->guest_mem_fd;
	binding->cma.offset = cma_map->offset;
	binding->guest_phys_addr = cma_map->guest_addr;
	binding->label = cma_map->label;
	binding->size = cma_map->size;
	binding->flags = cma_map->flags;
	binding->vm_parcel = NULL;

	cma->binding = binding;
	cma->ghvm = ghvm;

	if (binding->flags & GUNYAH_MEM_FORCE_LEND)
		binding->share_type = VM_MEM_LEND;
	else
		binding->share_type = VM_MEM_SHARE;

	down_write(&ghvm->bindings_lock);
	ret = mtree_insert_range(&ghvm->bindings,
				 gunyah_gpa_to_gfn(binding->guest_phys_addr),
				 gunyah_gpa_to_gfn(binding->guest_phys_addr + cma_map->size - 1),
				 binding, GFP_KERNEL);

	if (ret != 0)
		kfree(binding);

	up_write(&ghvm->bindings_lock);

	return ret;
}

static long gunyah_cma_create_mem_fd(struct gunyah_cma *cma)
{
	unsigned long flags = 0;
	struct inode *inode;
	struct file *file;
	int fd, err;

	if (cma->page)
		return -EBUSY;

	flags |= O_CLOEXEC;
	fd = get_unused_fd_flags(flags);
	if (fd < 0)
		return fd;

	file = anon_inode_create_getfile("[gunyah-cma]", &gunyah_cma_fops,
					cma, O_RDWR, NULL);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_put_fd;
	}

	inode = file->f_inode;
	inode->i_mode |= S_IFREG;
	/* Platform specific size of CMA per VM */
	i_size_write(inode, cma->max_size);

	file->f_flags |= O_LARGEFILE;
	file->f_mapping = inode->i_mapping;
	cma->file = file;
	fd_install(fd, file);

	return fd;
err_put_fd:
	put_unused_fd(fd);
	return err;
}

static long gunyah_cma_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	struct miscdevice *miscdev = filp->private_data;
	struct gunyah_cma *cma = container_of(miscdev, struct gunyah_cma, miscdev);

	switch (cmd) {
	case GH_ANDROID_CREATE_CMA_MEM_FD: {
		return gunyah_cma_create_mem_fd(cma);
	}
	default:
		return -ENOTTY;
	}
}

static const struct file_operations gunyah_cma_dev_fops = {
	/* clang-format off */
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= gunyah_cma_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= noop_llseek,
	/* clang-format on */
};

static void gunyah_cma_device_release(struct device *dev)
{
	struct gunyah_cma *cma = container_of(dev, struct gunyah_cma, dev);

	kfree(cma);
}

static int gunyah_cma_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	const char **mem_name;
	struct gunyah_cma_parent *pcma;
	int mem_count, i, ret, err = 0;
	struct device_node *mem_node;
	struct reserved_mem *rmem;

	mem_count = of_property_count_strings(node, "memory-region-names");
	if (!mem_count)
		return -EINVAL;

	mem_name = kmalloc_array(mem_count, sizeof(char *), GFP_KERNEL);
	if (!mem_name)
		return -ENOMEM;

	mem_count = of_property_read_string_array(node, "memory-region-names",
							mem_name, mem_count);
	if (mem_count < 0) {
		err = -EINVAL;
		goto err_mem_name;
	}

	pcma = devm_kzalloc(dev, sizeof(*pcma), GFP_KERNEL);
	if (!pcma) {
		err = -ENOMEM;
		goto err_mem_name;
	}
	INIT_LIST_HEAD(&pcma->gunyah_cma_children);

	for (i = 0; i < mem_count; i++) {
		struct gunyah_cma *cma;
		rmem = NULL;

		cma = kzalloc(sizeof(*cma), GFP_KERNEL);
		if (!cma) {
			ret = -ENOMEM;
			goto err_continue;
		}

		cma->miscdev.parent = &pdev->dev;
		cma->miscdev.name = mem_name[i];
		cma->miscdev.minor = MISC_DYNAMIC_MINOR;
		cma->miscdev.fops = &gunyah_cma_dev_fops;

		ret = misc_register(&cma->miscdev);
		if (ret) {
			kfree(cma);
			goto err_continue;
		}

		device_initialize(&cma->dev);
		cma->dev.parent = &pdev->dev;
		cma->dev.release = gunyah_cma_device_release;
		cma->dev.init_name = mem_name[i];

		ret = of_reserved_mem_device_init_by_name(&cma->dev,
						dev->of_node, mem_name[i]);
		if (ret)
			goto err_device;

		mem_node = of_parse_phandle(dev->of_node, "memory-region", i);
		if (mem_node)
			rmem = of_reserved_mem_lookup(mem_node);
		of_node_put(mem_node);
		if (!rmem) {
			dev_err(dev, "Failed to find reserved memory for %s\n", mem_name[i]);
			goto err_device;
		}
		cma->max_size = rmem->size;
		cma->page = NULL;
		list_add(&cma->list, &pcma->gunyah_cma_children);
		dev_dbg(dev, "Created a reserved cma pool for %s\n", mem_name[i]);
		continue;

err_device:
		misc_deregister(&cma->miscdev);
		put_device(&cma->dev);
err_continue:
		dev_err(dev, "Failed to create reserved cma pool for %s %d\n", mem_name[i], ret);
		continue;
	}

	platform_set_drvdata(pdev, pcma);

err_mem_name:
	kfree(mem_name);
	return err;
}

static void gunyah_cma_remove(struct platform_device *pdev)
{
	struct gunyah_cma_parent *pcma = platform_get_drvdata(pdev);
	struct gunyah_cma *cma, *iter;

	list_for_each_entry_safe(cma, iter, &pcma->gunyah_cma_children, list) {
		misc_deregister(&cma->miscdev);
		of_reserved_mem_device_release(&cma->dev);
		put_device(&cma->dev);
	}
}

static const struct of_device_id gunyah_cma_match_table[] = {
	{ .compatible = "gunyah-cma-vm-mem"},
	{}
};

static struct platform_driver gunyah_cma_driver = {
	.probe = gunyah_cma_probe,
	.remove_new = gunyah_cma_remove,
	.driver = {
		.name = "gunyah_cma_vm_mem_driver",
		.of_match_table = gunyah_cma_match_table,
	},
};

int gunyah_cma_mem_init(void)
{
	return platform_driver_register(&gunyah_cma_driver);
}

void gunyah_cma_mem_exit(void)
{
	platform_driver_unregister(&gunyah_cma_driver);
}
