// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtio message transport - FFA based channel interface.
 *
 * Copyright (C) 2024 Google LLC and Linaro.
 * Lei Zhou <lei.zhou@linaro.org>
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This implements the channel interface for Virtio msg transport via FFA (Arm
 * Firmware Framework).
 */

#define pr_fmt(fmt) "virtio-msg-ffa: " fmt

#include <linux/arm_ffa.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/virtio.h>

#include <uapi/linux/virtio_msg_ffa.h>

#include "virtio_msg.h"

static inline dma_addr_t ffa_to_dma(u32 area_id, dma_addr_t offset)
{
	return ((u64) area_id << VIRTIO_MSG_FFA_AREA_ID_OFFSET) |
		(offset & VIRTIO_MSG_FFA_OFFSET_MASK);
}

static inline u32 dma_to_ffa(dma_addr_t dma_handle, dma_addr_t *offset)
{
	*offset = dma_handle & VIRTIO_MSG_FFA_OFFSET_MASK;

	return dma_handle >> VIRTIO_MSG_FFA_AREA_ID_OFFSET;
}

/* Represents area shared with a partition */
struct shared_area {
	u64 handle;
	u32 id;
	void *vaddr;
	dma_addr_t dma_handle;
	size_t n_pages;
	u32 count;
	struct list_head list;
};

/* Represents channel bus corresponding to a partition */
struct virtio_msg_ffa_device {
	struct virtio_msg_device *vmdevs;
	int vmdev_count;
	struct ffa_device *ffa_dev;
	struct ida area_id_map;
	struct list_head area_list;
	struct mutex lock; /* to protect area_list */
	struct virtio_msg_async async;
	void *response;

	bool indirect;
	bool reserved_mem;
};

#define to_vmfdev(_vmdev) ((struct virtio_msg_ffa_device *) vmdev->priv)

static void vmsg_ffa_prepare(struct virtio_msg_ffa *msg, u8 msg_id)
{
	/*
	 * Since the structure headers are same, lets reuse the same helper for
	 * bus messages too.
	 */
	virtio_msg_prepare((struct virtio_msg *) msg, true, msg_id, 0);
}

static int vmsg_ffa_send_direct(struct ffa_device *ffa_dev, void *request,
				void *response)
{
	struct ffa_send_direct_data2 ffa_data;
	int ret;

	memcpy(&ffa_data, request, VIRTIO_MSG_MAX_SIZE);

	ret = ffa_dev->ops->msg_ops->sync_send_receive2(ffa_dev, &ffa_data);
	if (ret) {
		dev_err(&ffa_dev->dev, "Unable to send direct FFA message: %d\n", ret);
		return ret;
	}

	if (response)
		memcpy(response, &ffa_data, VIRTIO_MSG_MAX_SIZE);

	return 0;
}

static int vmsg_ffa_send_indirect(struct virtio_msg_ffa_device *vmfdev,
				  struct virtio_msg_async *async,
				  void *request, void *response)
{
	struct ffa_device *ffa_dev = vmfdev->ffa_dev;
	int ret;

	/* Save buffer so it can be filled by vmsg_ffa_notifier_cb() */
	vmfdev->response = response;

	ret = ffa_dev->ops->msg_ops->indirect_send(ffa_dev, request, VIRTIO_MSG_MAX_SIZE);
	if (ret) {
		dev_err(&ffa_dev->dev, "Unable to send in-direct FFA message: %d\n", ret);
		return ret;
	}

	/*
	 * Always wait for the operation to finish, otherwise we may start
	 * another operation while a previous one is still on the fly.
	 */
	ret = virtio_msg_async_wait(async, &ffa_dev->dev, 1000);
	vmfdev->response = NULL;

	return ret;
}

static int vmsg_ffa_send(struct virtio_msg_ffa_device *vmfdev,
		struct virtio_msg_async *async, void *request, void *response)
{
	struct ffa_device *ffa_dev = vmfdev->ffa_dev;
	int ret;

	/* Try direct messaging first, fallback to indirect */
	if (!vmfdev->indirect) {
		ret = vmsg_ffa_send_direct(ffa_dev, request, response);
		if (!ret)
			return 0;

		/* Fallback to indirect messaging */
		vmfdev->indirect = true;
	}

	return vmsg_ffa_send_indirect(vmfdev, async, request, response);
}

static struct virtio_msg_device *
find_vmdev(struct virtio_msg_ffa_device *vmfdev, u16 dev_id)
{
	int i;

	/* Find the device corresponding to the message */
	for (i = 0; i < vmfdev->vmdev_count; i++) {
		if (vmfdev->vmdevs[i].dev_id == dev_id)
			return &vmfdev->vmdevs[i];
	}

	dev_err(&vmfdev->ffa_dev->dev, "Couldn't find matching vmdev: %d\n",
		dev_id);
	return NULL;
}

static void handle_async_event(struct virtio_msg_ffa_device *vmfdev,
			       struct virtio_msg *msg)
{
	struct ffa_device *ffa_dev = vmfdev->ffa_dev;
	struct virtio_msg_device *vmdev;
	struct virtio_msg_vq *info;

	/*
	 * We can either receive a response message (to a previously
	 * sent request) here, or a EVENT_USED request message.
	 */
	if (msg->type & VIRTIO_MSG_TYPE_RESPONSE) {
		struct virtio_msg_async *async;

		if (vmfdev->response)
			memcpy(vmfdev->response, msg, VIRTIO_MSG_MAX_SIZE);

		if (msg->type & VIRTIO_MSG_TYPE_BUS) {
			async = &vmfdev->async;
		} else {
			vmdev = find_vmdev(vmfdev, msg->dev_id);
			if (!vmdev)
				return;

			async = &vmdev->async;
		}

		virtio_msg_async_complete(async);
		return;
	}

	/* Only support EVENT_USED virtio request messages */
	if (msg->type & VIRTIO_MSG_TYPE_BUS || msg->id != VIRTIO_MSG_EVENT_USED) {
		dev_err(&ffa_dev->dev, "Unsupported message received\n");
		return;
	}

	vmdev = find_vmdev(vmfdev, msg->dev_id);
	if (!vmdev)
		return;

	/*
	 * Received EVENT_USED request, but the index field can't be really used
	 * as the backend doesn't fill it. Reset the field anyway to avoid
	 * confusion.
	 */
	msg->event_used.index = 0;

	/*
	 * Receive the message for each virtqueue until one
	 * accepts it.
	 */
	list_for_each_entry(info, &vmdev->virtqueues, node) {
		if (!virtio_msg_receive(vmdev, msg))
			return;

		msg->event_used.index++;
	}

	/* Interrupt should belong to one of the virtqueues at least */
	dev_err(&ffa_dev->dev,
		"Failed to find virtqueue for EVENT_USED message\n");
}

static void vmsg_ffa_notifier_cb(int notify_id, void *cb_data, void *buf)
{
	struct virtio_msg_ffa_device *vmfdev = cb_data;

	handle_async_event(vmfdev, buf);
}

static int vmsg_ffa_indirect_notify_setup(struct virtio_msg_ffa_device *vmfdev)
{
	struct ffa_device *ffa_dev = vmfdev->ffa_dev;
	int ret;

	ret = ffa_dev->ops->notifier_ops->fwk_notify_request(ffa_dev,
			&vmsg_ffa_notifier_cb, vmfdev, 0);
	if (ret)
		dev_err(&ffa_dev->dev, "Unable to set notifier: %d\n", ret);

	return ret;
}

static int virtio_msg_ffa_send(struct virtio_msg_device *vmdev,
			       struct virtio_msg *request,
			       struct virtio_msg *response)
{
	struct virtio_msg_ffa_device *vmfdev = to_vmfdev(vmdev);

	return vmsg_ffa_send(vmfdev, &vmdev->async, request, response);
}

static int vmsg_ffa_bus_error(struct virtio_msg_ffa_device *vmfdev)
{
	struct virtio_msg_ffa request;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_ERROR);
	return vmsg_ffa_send(vmfdev, &vmfdev->async, &request, NULL);
}

static int vmsg_ffa_bus_activate(struct virtio_msg_ffa_device *vmfdev,
				 u64 *features, u64 *devices)
{
	struct virtio_msg_ffa request, response;
	int ret;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_ACTIVATE);
	request.bus_activate.driver_version = cpu_to_le32(VIRTIO_MSG_FFA_VERSION_1_0);

	ret = vmsg_ffa_send(vmfdev, &vmfdev->async, &request, &response);
	if (ret < 0)
		return ret;

	if (le32_to_cpu(response.bus_activate_resp.device_version) != VIRTIO_MSG_FFA_VERSION_1_0)
		return -EINVAL;

	if (features)
		*features = le64_to_cpu(response.bus_activate_resp.features);
	if (devices)
		*devices = le64_to_cpu(response.bus_activate_resp.num);

	return 0;
}

static void vmsg_ffa_bus_deactivate(struct virtio_msg_ffa_device *vmfdev)
{
	struct virtio_msg_ffa request;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_DEACTIVATE);
	vmsg_ffa_send(vmfdev, &vmfdev->async, &request, NULL);
}

static int vmsg_ffa_bus_configure(struct virtio_msg_ffa_device *vmfdev,
				  u64 features)
{
	struct virtio_msg_ffa request;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_CONFIGURE);
	request.bus_configure.features = cpu_to_le64(features);

	return vmsg_ffa_send(vmfdev, &vmfdev->async, &request, NULL);
}

static int vmsg_ffa_bus_area_share_single(struct ffa_device *ffa_dev, void *vaddr,
		size_t n_pages, dma_addr_t *dma_handle)
{
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;
	struct virtio_msg_ffa request;
	struct ffa_mem_region_attributes mem_attr = {
		.receiver = ffa_dev->vm_id,
		.attrs = FFA_MEM_RW,
	};
	struct ffa_mem_ops_args args = {
		.use_txbuf = true,
		.attrs = &mem_attr,
		.nattrs = 1,
	};
	struct shared_area *area;
	struct page **pages;
	struct sg_table sgt;
	int ret, i;

	pages = kmalloc(sizeof(*pages) * n_pages, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	for (i = 0; i < n_pages; i++)
		pages[i] = virt_to_page((void *)((u64)vaddr + PAGE_SIZE * i));

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area) {
		ret = -ENOMEM;
		goto free_pages;
	}

	ret = ida_alloc_range(&vmfdev->area_id_map, 1, U32_MAX - 1, GFP_KERNEL);
	if (ret < 0)
		goto free_area;
	area->id = ret;

	/* Share the pages */
	ret = sg_alloc_table_from_pages(&sgt, pages, n_pages, 0,
					n_pages * PAGE_SIZE, GFP_KERNEL);
	if (ret)
		goto free_ida;

	args.sg = sgt.sgl;
	ret = ffa_dev->ops->mem_ops->memory_share(&args);
	sg_free_table(&sgt);

	if (ret)
		goto free_ida;

	area->handle = args.g_handle;
	area->vaddr = vaddr;
	area->dma_handle = virt_to_phys(vaddr);
	area->n_pages = n_pages;
	area->count = 1;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_AREA_SHARE);
	request.bus_area_share.area_id = cpu_to_le32(area->id);
	request.bus_area_share.mem_handle = cpu_to_le64(area->handle);

	ret = vmsg_ffa_send(vmfdev, &vmfdev->async, &request, NULL);
	if (ret < 0)
		goto mem_reclaim;

	if (dma_handle)
		*dma_handle = ffa_to_dma(area->id, 0);

	mutex_lock(&vmfdev->lock);
	list_add(&area->list, &vmfdev->area_list);
	mutex_unlock(&vmfdev->lock);

	kfree(pages);
	return 0;

mem_reclaim:
	ffa_dev->ops->mem_ops->memory_reclaim(area->handle, 0);
free_ida:
	ida_free(&vmfdev->area_id_map, area->id);
free_area:
	kfree(area);
free_pages:
	kfree(pages);

	return ret;
}

/* vaddr is always page aligned */
int vmsg_ffa_bus_area_share(struct device *dev, void *vaddr, size_t n_pages,
			    dma_addr_t *dma_handle)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;
	struct shared_area *area;
	int ret = 0;

	mutex_lock(&vmfdev->lock);
	/*
	 * If "restricted-dma-pool" is supported, we should have already mapped
	 * a big enough area at initialization time. Make sure that "dma_handle"
	 * lies within that and update dma_handle properly.
	 */
	if (vmfdev->reserved_mem) {
		BUG_ON(!list_is_singular(&vmfdev->area_list));

		area = list_first_entry(&vmfdev->area_list, struct shared_area,
					list);

		if ((*dma_handle < area->dma_handle) ||
		    ((*dma_handle + n_pages * PAGE_SIZE) >
		     (area->dma_handle + area->n_pages * PAGE_SIZE))) {
			dev_err(dev, "Vaddr out of range\n");
			ret = -EINVAL;
		} else {
			*dma_handle = ffa_to_dma(area->id, *dma_handle - area->dma_handle);
			area->count++;
		}

		mutex_unlock(&vmfdev->lock);
		return ret;
	}

	/* Check if area is already mapped */
	list_for_each_entry(area, &vmfdev->area_list, list) {
		/* TODO: Only support exact page match for now */
		if (area->vaddr == vaddr && area->n_pages == n_pages) {
			*dma_handle = ffa_to_dma(area->id, 0);
			area->count++;
			mutex_unlock(&vmfdev->lock);
			return 0;
		}
	}
	mutex_unlock(&vmfdev->lock);

	return vmsg_ffa_bus_area_share_single(ffa_dev, vaddr, n_pages, dma_handle);
}
EXPORT_SYMBOL_GPL(vmsg_ffa_bus_area_share);

static int vmsg_ffa_bus_area_unshare_single(struct ffa_device *ffa_dev,
		struct shared_area *area)
{
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;
	struct virtio_msg_ffa request;
	int ret;

	vmsg_ffa_prepare(&request, VIRTIO_MSG_FFA_AREA_UNSHARE);
	request.bus_area_unshare.area_id = cpu_to_le32(area->id);
	request.bus_area_unshare.mem_handle = cpu_to_le32(area->handle);

	ret = vmsg_ffa_send(vmfdev, &vmfdev->async, &request, NULL);
	if (!ret) {
		/* reclaim shared memory */
		ret = ffa_dev->ops->mem_ops->memory_reclaim(area->handle, 0);
	}

	ida_free(&vmfdev->area_id_map, area->id);
	kfree(area);

	return ret;
}

int vmsg_ffa_bus_area_unshare(struct device *dev, dma_addr_t *dma_handle,
			      size_t n_pages)
{
	struct ffa_device *ffa_dev = to_ffa_dev(dev);
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;
	struct shared_area *area;
	dma_addr_t offset;
	u32 area_id;

	area_id = dma_to_ffa(*dma_handle, &offset);

	mutex_lock(&vmfdev->lock);
	list_for_each_entry(area, &vmfdev->area_list, list) {
		if (area->id == area_id) {
			*dma_handle = area->dma_handle + offset;

			if (--area->count) {
				mutex_unlock(&vmfdev->lock);
				return 0;
			}

			WARN_ON(area->n_pages != n_pages);
			list_del(&area->list);
			mutex_unlock(&vmfdev->lock);

			return vmsg_ffa_bus_area_unshare_single(ffa_dev, area);
		}
	}
	mutex_unlock(&vmfdev->lock);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(vmsg_ffa_bus_area_unshare);

static const char *virtio_msg_ffa_bus_name(struct virtio_msg_device *vmdev)
{
	struct virtio_msg_ffa_device *vmfdev = to_vmfdev(vmdev);

	return dev_name(&vmfdev->ffa_dev->dev);
}

static struct virtio_msg_ops vmf_ops = {
	.send = virtio_msg_ffa_send,
	.bus_name = virtio_msg_ffa_bus_name,
};

static int virtio_msg_ffa_probe(struct ffa_device *ffa_dev)
{
	struct virtio_msg_ffa_device *vmfdev;
	struct device *dev = &ffa_dev->dev;
	struct virtio_msg_device *vmdev;
#ifdef CONFIG_VIRTIO_MSG_FFA_DMA_OPS
	struct reserved_mem *rmem;
	struct device_node *np;
#endif
	u64 features, count;
	int ret, i;

	vmfdev = devm_kzalloc(dev, sizeof(*vmfdev), GFP_KERNEL);
	if (!vmfdev)
		return -ENOMEM;

	ida_init(&vmfdev->area_id_map);

	vmfdev->indirect = false;
	vmfdev->ffa_dev = ffa_dev;
	ffa_dev_set_drvdata(ffa_dev, vmfdev);
	INIT_LIST_HEAD(&vmfdev->area_list);
	virtio_msg_async_init(&vmfdev->async);
	mutex_init(&vmfdev->lock);

	ret = dma_set_mask_and_coherent(&ffa_dev->dev, DMA_BIT_MASK(64));
	if (ret)
		ret = dma_set_mask_and_coherent(&ffa_dev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(&ffa_dev->dev, "Failed to enable 64-bit or 32-bit DMA\n");

	/* Setup notifier for async (indirect) messages */
	vmsg_ffa_indirect_notify_setup(vmfdev);

	/* Set DMA OPs for the channel bus device */
#ifdef CONFIG_VIRTIO_MSG_FFA_DMA_OPS
	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np)
		goto ida_free;

	rmem = of_reserved_mem_lookup(np);
	if (rmem) {
		ret = vmsg_ffa_bus_area_share(dev, phys_to_virt(rmem->base),
					      PFN_UP(rmem->size), NULL);
		if (ret)
			goto ida_free;
		vmfdev->reserved_mem = true;
	}

	dev->dma_ops = &virtio_msg_ffa_dma_ops;
#endif

	ret = vmsg_ffa_bus_activate(vmfdev, &features, &count);
	if (ret)
		goto ida_free;

	if (!count) {
		ret = -ENODEV;
		goto out;
	}

	/* Direct message must be supported if it already worked */
	if (!vmfdev->indirect && !(features & VIRTIO_MSG_FFA_FEATURE_DIRECT_MSG_SUPP)) {
		ret = -EINVAL;
		goto out;
	}

	/* In-direct message must be supported if it already worked */
	if (vmfdev->indirect && !(features & VIRTIO_MSG_FFA_FEATURE_INDIRECT_MSG_SUPP)) {
		ret = -EINVAL;
		goto out;
	}

	ret = vmsg_ffa_bus_configure(vmfdev, features);
	if (ret)
		goto out;

	vmfdev->vmdevs = devm_kcalloc(dev, count, sizeof(*vmfdev->vmdevs), GFP_KERNEL);
	if (!vmfdev->vmdevs) {
		ret = -ENOMEM;
		goto out;
	}
	vmfdev->vmdev_count = count;

	for (i = 0; i < count; i++) {
		vmdev = &vmfdev->vmdevs[i];
		vmdev->dev_id = i;
		vmdev->ops = &vmf_ops;
		vmdev->vdev.dev.parent = &ffa_dev->dev;
		vmdev->priv = vmfdev;

		/*
		 * Register all virtio devices, they will fetch their ids and
		 * register the right device type.
		 */
		ret = virtio_msg_register(vmdev);
		if (ret)
			dev_err(&ffa_dev->dev, "Failed to register virtio msg device with id: %d\n", i);
	}

	return 0;

out:
	vmsg_ffa_bus_error(vmfdev);
	vmsg_ffa_bus_deactivate(vmfdev);
ida_free:
	ida_destroy(&vmfdev->area_id_map);
	return ret;
}

static void virtio_msg_ffa_remove(struct ffa_device *ffa_dev)
{
	struct virtio_msg_ffa_device *vmfdev = ffa_dev->dev.driver_data;

	vmsg_ffa_bus_deactivate(vmfdev);
}

static const struct ffa_device_id virtio_msg_ffa_device_ids[] = {
	/* FIXME: Use the correct UUIDs */
	{ UUID_INIT(0xc5b82091, 0xd4fe, 0x48bb,
		    0xb7, 0xe7, 0x4d, 0x24, 0x6e, 0xbb, 0x28, 0xbe) },
	{}
};

static struct ffa_driver virtio_msg_ffa_driver = {
	.name = "virtio-msg-ffa",
	.probe = virtio_msg_ffa_probe,
	.remove = virtio_msg_ffa_remove,
	.id_table = virtio_msg_ffa_device_ids,
};

static int virtio_msg_ffa_init(void)
{
	virtio_msg_ffa_dma_init();

	if (IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT))
		return ffa_register(&virtio_msg_ffa_driver);
	else
		return -EOPNOTSUPP;
}
module_init(virtio_msg_ffa_init);

static void virtio_msg_ffa_exit(void)
{
	if (IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT))
		ffa_unregister(&virtio_msg_ffa_driver);
}
module_exit(virtio_msg_ffa_exit);

MODULE_AUTHOR("Lei Zhou <lei.zhou@linaro.org>");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("Virtio_MSG_FFA channel driver");
MODULE_LICENSE("GPL");
