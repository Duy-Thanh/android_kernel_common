// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <lkl/linux/pci_regs.h>
#include <lkl/linux/virtio_pci.h>

#include <lkl.h>
#include <lkl_host.h>

#include "endian.h"
#include "iomem.h"
#include "lkl_pci.h"
#include "virtio_pci.h"

#define BIT(x) (1ULL << x)

#define htole8(x) ((uint8_t)x)

#define PCI_READ_UINTX_CHECK_SIZE(to, from, size, bits)                        \
	{                                                                      \
		assert(size == sizeof(uint##bits##_t));                        \
		*(uint##bits##_t *)(to) = htole##bits(from);                   \
	}

#define PCI_READ_UINT8_CHECK_SIZE(...) PCI_READ_UINTX_CHECK_SIZE(__VA_ARGS__, 8)
#define PCI_READ_UINT16_CHECK_SIZE(...)                                        \
	PCI_READ_UINTX_CHECK_SIZE(__VA_ARGS__, 16)
#define PCI_READ_UINT32_CHECK_SIZE(...)                                        \
	PCI_READ_UINTX_CHECK_SIZE(__VA_ARGS__, 32)

#define PCI_WRITE_UINTX_CHECK_SIZE(to, from, size, bits)                       \
	{                                                                      \
		assert(size == sizeof(uint##bits##_t));                        \
		(to) = le##bits##toh(*(uint##bits##_t *)(from));               \
	}

#define PCI_WRITE_UINT16_CHECK_SIZE(...)                                       \
	PCI_WRITE_UINTX_CHECK_SIZE(__VA_ARGS__, 16)
#define PCI_WRITE_UINT32_CHECK_SIZE(...)                                       \
	PCI_WRITE_UINTX_CHECK_SIZE(__VA_ARGS__, 32)

static inline uint32_t
virtio_read_device_features(struct virtio_pci_dev *virtio_pci_dev)
{
	if (virtio_pci_dev->device_features_sel)
		return (uint32_t)(virtio_pci_dev->device_features >> 32);

	return (uint32_t)virtio_pci_dev->device_features;
}

static inline void
virtio_write_guest_features(struct virtio_pci_dev *virtio_pci_dev, uint32_t val)
{
	uint64_t tmp;

	if (virtio_pci_dev->guest_features_sel) {
		tmp = virtio_pci_dev->guest_features & 0xFFFFFFFF;
		virtio_pci_dev->guest_features = tmp | (uint64_t)val << 32;
	} else {
		tmp = virtio_pci_dev->guest_features & 0xFFFFFFFF00000000;
		virtio_pci_dev->guest_features = tmp | val;
	}
}

static inline void set_status(struct virtio_pci_dev *virtio_pci_dev,
			      uint8_t val)
{
	if ((val & LKL_VIRTIO_CONFIG_S_FEATURES_OK) &&
	    (!(virtio_pci_dev->guest_features & BIT(LKL_VIRTIO_F_VERSION_1)) ||
	     !(virtio_pci_dev->guest_features &
	       BIT(LKL_VIRTIO_RING_F_EVENT_IDX))))
		val &= ~LKL_VIRTIO_CONFIG_S_FEATURES_OK;
	virtio_pci_dev->status = val;
}

static int pci_common_read(struct virtio_pci_dev *virtio_pci_dev, int offset,
			   void *value, int size)
{
	switch (offset) {
	case offsetof(struct lkl_virtio_pci_common_cfg, device_feature):
		PCI_READ_UINT32_CHECK_SIZE(
			value, virtio_read_device_features(virtio_pci_dev),
			size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, device_status):
		PCI_READ_UINT8_CHECK_SIZE(value, virtio_pci_dev->status, size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, num_queues):
		PCI_READ_UINT16_CHECK_SIZE(
			value, virtio_pci_dev->number_of_queues, size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, config_generation):
		PCI_READ_UINT8_CHECK_SIZE(value, virtio_pci_dev->config_gen,
					  size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_size):
		PCI_READ_UINT16_CHECK_SIZE(
			value,
			virtio_pci_dev->queue[virtio_pci_dev->queue_sel]
				.virtio_queue.num_max,
			size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_enable):
		PCI_READ_UINT16_CHECK_SIZE(
			value,
			virtio_pci_dev->queue[virtio_pci_dev->queue_sel]
				.virtio_queue.ready,
			size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_notify_off):
		PCI_READ_UINT16_CHECK_SIZE(
			value,
			virtio_pci_dev->queue[virtio_pci_dev->queue_sel]
				.notification_offset,
			size);
		break;
	default:
		lkl_printf(
			"Unsupported read from common config at %x, size %x\n",
			offset, size);
		return -1;
	}

	lkl_printf("In pci_common_read offset %016lX, size %08X, value %08X\n",
		   offset, size, *(uint32_t *)value);
	return 0;
}

static int pci_common_write(struct virtio_pci_dev *virtio_pci_dev, int offset,
			    void *value, int size)
{
	lkl_printf("In pci_common_write offset %016lX, size %08X, value %08X\n",
		   offset, size, *(uint32_t *)value);

	switch (offset) {
	case offsetof(struct lkl_virtio_pci_common_cfg, device_feature_select):
		PCI_WRITE_UINT32_CHECK_SIZE(virtio_pci_dev->device_features_sel,
					    value, size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, guest_feature_select):
		PCI_WRITE_UINT32_CHECK_SIZE(virtio_pci_dev->guest_features_sel,
					    value, size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, guest_feature):
		assert(size == sizeof(uint32_t));
		virtio_write_guest_features(virtio_pci_dev,
					    le32toh(*(uint32_t *)value));
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, device_status):
		assert(size == sizeof(uint8_t));
		set_status(virtio_pci_dev, *(uint8_t *)value);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_select):
		PCI_WRITE_UINT16_CHECK_SIZE(virtio_pci_dev->queue_sel, value,
					    size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_size):
		PCI_WRITE_UINT16_CHECK_SIZE(
			virtio_pci_dev->queue[virtio_pci_dev->queue_sel]
				.virtio_queue.num_max,
			value, size);
		break;
	case offsetof(struct lkl_virtio_pci_common_cfg, queue_enable):
		PCI_WRITE_UINT16_CHECK_SIZE(
			virtio_pci_dev->queue[virtio_pci_dev->queue_sel]
				.virtio_queue.ready,
			value, size);
		break;
	default:
		lkl_printf(
			"Unsupported write to common config at %x, size %x\n",
			offset, size);
		return -1;
	}
	return 0;
}

static int pci_notify_read(struct virtio_pci_dev *virtio_pci_dev, int offset,
			   void *value, int size)
{
	// TODO(b/225222618): implement routine
	lkl_printf("In pci_notify_read addr %016lX, size %08X\n", offset, size);
	return -1;
}

static int pci_notify_write(struct virtio_pci_dev *virtio_pci_dev, int offset,
			    void *value, int size)
{
	// TODO(b/225222618): implement routine
	lkl_printf("In pci_notify_write addr %016lX, size %08X\n", offset,
		   size);
	return -1;
}

static int pci_isr_read(struct virtio_pci_dev *virtio_pci_dev, int offset,
			void *value, int size)
{
	// TODO(b/225222618): implement routine
	lkl_printf("In pci_isr_read addr %016lX, size %08X\n", offset, size);
	return -1;
}

static int pci_isr_write(struct virtio_pci_dev *virtio_pci_dev, int offset,
			 void *value, int size)
{
	// TODO(b/225222618): implement routine
	lkl_printf("In pci_isr_write addr %016lX, size %08X\n", offset, size);
	return -1;
}

static int pci_device_read(struct virtio_pci_dev *virtio_pci_dev, int offset,
			   void *value, int size)
{
	lkl_printf("In pci_device_read addr %016lX, size %08X\n", offset, size);
	memcpy(value, virtio_pci_dev->device_config_data + offset, size);
	return 0;
}

static int pci_device_write(struct virtio_pci_dev *virtio_pci_dev, int offset,
			    void *value, int size)
{
	lkl_printf("In pci_device_write addr %016lX, size %08X\n", offset,
		   size);
	memcpy(virtio_pci_dev->device_config_data + offset, value, size);
	return 0;
}

static int pci_resource_read(void *data, int offset, void *value, int size)
{
	struct virtio_pci_resource *resource =
		(struct virtio_pci_resource *)data;
	struct virtio_pci_dev *dev = resource->virtio_pci_dev;

	int res = 0;
	switch (resource->capability_type) {
	case LKL_VIRTIO_PCI_CAP_COMMON_CFG:
		res = pci_common_read(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_NOTIFY_CFG:
		res = pci_notify_read(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_ISR_CFG:
		res = pci_isr_read(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_DEVICE_CFG:
		res = pci_device_read(dev, offset, value, size);
		break;
	default:
		lkl_printf("Invalid capability type %d\n",
			   resource->capability_type);
		lkl_host_ops.panic();
	}

	return res;
}

static int pci_resource_write(void *data, int offset, void *value, int size)
{
	struct virtio_pci_resource *resource =
		(struct virtio_pci_resource *)data;
	struct virtio_pci_dev *dev = resource->virtio_pci_dev;

	int res = 0;
	switch (resource->capability_type) {
	case LKL_VIRTIO_PCI_CAP_COMMON_CFG:
		res = pci_common_write(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_NOTIFY_CFG:
		res = pci_notify_write(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_ISR_CFG:
		res = pci_isr_write(dev, offset, value, size);
		break;
	case LKL_VIRTIO_PCI_CAP_DEVICE_CFG:
		res = pci_device_write(dev, offset, value, size);
		break;
	default:
		lkl_printf("Invalid capability type %d\n",
			   resource->capability_type);
		lkl_host_ops.panic();
	}

	return res;
}

static const struct lkl_iomem_ops pci_resource_ops = {
	.read = pci_resource_read,
	.write = pci_resource_write,
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int alloc_pci_resource(struct virtio_pci_resource *resource,
			      struct virtio_pci_dev *virtio_pci_dev,
			      uint32_t size, uint32_t capability_type)
{
	resource->virtio_pci_dev = virtio_pci_dev;
	resource->resource_size = size;
	resource->capability_type = capability_type;
	resource->pci_resource_iomem =
		register_iomem(resource, size, &pci_resource_ops);
	if (resource->pci_resource_iomem == NULL)
		return -LKL_ENOMEM;

	return 0;
}

static void free_pci_resource(struct virtio_pci_resource *resource)
{
	if (resource->pci_resource_iomem != NULL) {
		unregister_iomem(resource->pci_resource_iomem);
		resource->pci_resource_iomem = NULL;
	}
}

int virtio_pci_dev_add(struct virtio_pci_dev *virtio_pci_dev,
		       uint16_t number_of_queues, uint16_t num_max,
		       void *device_config_data, size_t device_config_len)
{
	memset(&virtio_pci_dev->common_cap_resource, 0x00,
	       sizeof(virtio_pci_dev->common_cap_resource));
	memset(&virtio_pci_dev->notify_cap_resource, 0x00,
	       sizeof(virtio_pci_dev->notify_cap_resource));
	memset(&virtio_pci_dev->isr_cap_resource, 0x00,
	       sizeof(virtio_pci_dev->isr_cap_resource));
	memset(&virtio_pci_dev->device_cap_resource, 0x00,
	       sizeof(virtio_pci_dev->device_cap_resource));
	virtio_pci_dev->queue = NULL;

	// Default size to accommodate all the required features. There seem to be
	// minimal value of size or alignment requirement below which the PCI bus
	// code will consider the resource invalid. Thus, use the value
	// of 0x100 which is sufficient to accommodate needed data and passes PCI
	// bus checks.
	uint32_t default_capability_size = 0x100;
	if (alloc_pci_resource(&virtio_pci_dev->common_cap_resource,
			       virtio_pci_dev, default_capability_size,
			       LKL_VIRTIO_PCI_CAP_COMMON_CFG) != 0)
		goto error_release_resources;
	if (alloc_pci_resource(&virtio_pci_dev->notify_cap_resource,
			       virtio_pci_dev, default_capability_size,
			       LKL_VIRTIO_PCI_CAP_NOTIFY_CFG) != 0)
		goto error_release_resources;
	if (alloc_pci_resource(&virtio_pci_dev->isr_cap_resource,
			       virtio_pci_dev, default_capability_size,
			       LKL_VIRTIO_PCI_CAP_ISR_CFG) != 0)
		goto error_release_resources;
	if (alloc_pci_resource(&virtio_pci_dev->device_cap_resource,
			       virtio_pci_dev, default_capability_size,
			       LKL_VIRTIO_PCI_CAP_DEVICE_CFG) != 0)
		goto error_release_resources;

	size_t qsize = number_of_queues * sizeof(*virtio_pci_dev->queue);
	virtio_pci_dev->queue = lkl_host_ops.mem_alloc(qsize);
	if (virtio_pci_dev->queue == NULL)
		goto error_release_resources;

	memset(virtio_pci_dev->queue, 0x00, qsize);
	for (size_t i = 0; i < number_of_queues; i++) {
		// TODO(b/225222618): ring handling for virtio_pci isn't implemented
		// yet, thus, set `packed_ring` field to a default `false` value.
		virtio_pci_dev->queue[i].virtio_queue.packed_ring = false;
		virtio_pci_dev->queue[i].virtio_queue.num_max = num_max;
		virtio_pci_dev->queue[i].notification_offset = i;
	}

	virtio_pci_dev->number_of_queues = number_of_queues;

	virtio_pci_dev->device_config_data = device_config_data;
	virtio_pci_dev->device_config_len = device_config_len;

	virtio_pci_dev->device_features |=
		BIT(LKL_VIRTIO_F_VERSION_1) | BIT(LKL_VIRTIO_RING_F_EVENT_IDX);

	virtio_pci_dev->config_gen = 0;

	struct pci_resource pci_resources[] = {
		{ virtio_pci_dev->common_cap_resource.pci_resource_iomem,
		  virtio_pci_dev->common_cap_resource.resource_size },

		{ virtio_pci_dev->notify_cap_resource.pci_resource_iomem,
		  virtio_pci_dev->notify_cap_resource.resource_size },

		{ virtio_pci_dev->isr_cap_resource.pci_resource_iomem,
		  virtio_pci_dev->isr_cap_resource.resource_size },

		{ virtio_pci_dev->device_cap_resource.pci_resource_iomem,
		  virtio_pci_dev->device_cap_resource.resource_size }
	};

	if (lkl_pci_dev_add(&virtio_pci_dev->config.common_config,
			    pci_resources, ARRAY_SIZE(pci_resources)) != 0)
		goto error_release_resources;

	return 0;

error_release_resources:
	if (virtio_pci_dev->queue != NULL) {
		lkl_host_ops.mem_free(virtio_pci_dev->queue);
		virtio_pci_dev->queue = NULL;
	}
	free_pci_resource(&virtio_pci_dev->common_cap_resource);
	free_pci_resource(&virtio_pci_dev->notify_cap_resource);
	free_pci_resource(&virtio_pci_dev->isr_cap_resource);
	free_pci_resource(&virtio_pci_dev->device_cap_resource);
	return -LKL_ENOMEM;
}

int virtio_pci_dev_remove(struct virtio_pci_dev *virtio_pci_dev)
{
	// First, stop the bus and remove the device
	int res = lkl_pci_dev_remove();
	// Then, it's safe to unregister iomem resources and free memory.
	if (virtio_pci_dev->queue != NULL) {
		lkl_host_ops.mem_free(virtio_pci_dev->queue);
		virtio_pci_dev->queue = NULL;
	}

	free_pci_resource(&virtio_pci_dev->common_cap_resource);
	free_pci_resource(&virtio_pci_dev->notify_cap_resource);
	free_pci_resource(&virtio_pci_dev->isr_cap_resource);
	free_pci_resource(&virtio_pci_dev->device_cap_resource);

	return res;
}