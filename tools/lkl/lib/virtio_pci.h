/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LKL_VIRTIO_PCI_H__
#define __LKL_VIRTIO_PCI_H__

#include <stddef.h>

#include <lkl/linux/pci_regs.h>
#include <lkl/linux/virtio_pci.h>

#include "lkl_pci.h"
#include "virtio.h"

struct virtio_pci_config {
	struct pci_config_normal_header hdr;
	struct lkl_virtio_pci_cap common;
	struct lkl_virtio_pci_cap isr;
	struct lkl_virtio_pci_notify_cap notify;
	struct lkl_virtio_pci_cap device;
};

struct virtio_pci_resource {
	struct virtio_pci_dev *virtio_pci_dev;
	uint32_t capability_type;
	void *pci_resource_iomem;
	uint32_t resource_size;
};

struct virtio_pci_queue {
	struct virtio_queue virtio_queue;
	uint16_t notification_offset;
};

struct virtio_pci_dev {
	struct virtio_pci_resource common_cap_resource;
	struct virtio_pci_resource notify_cap_resource;
	struct virtio_pci_resource isr_cap_resource;
	struct virtio_pci_resource device_cap_resource;

	union {
		struct virtio_pci_config virtio_pci_config;
		struct pci_config common_config;
	} config;

	uint64_t device_features;
	uint32_t device_features_sel;
	uint64_t guest_features;
	uint32_t guest_features_sel;
	uint16_t number_of_queues;
	uint16_t queue_sel;
	uint8_t status;
	uint8_t config_gen;

	struct virtio_pci_queue *queue;

	void *device_config_data;
	size_t device_config_len;
};

int virtio_pci_dev_add(struct virtio_pci_dev *virtio_pci_dev, uint16_t queues,
		       uint16_t num_max, void *device_config_data,
		       size_t device_config_len);

int virtio_pci_dev_remove(struct virtio_pci_dev *virtio_pci_dev);

#endif // __LKL_VIRTIO_PCI_H__