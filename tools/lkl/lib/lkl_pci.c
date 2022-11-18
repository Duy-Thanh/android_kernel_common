// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <lkl_host.h>
#include <stdio.h>
#include <stdint.h>

#include "iomem.h"
#include "lkl_pci.h"

#include <lkl/linux/pci_regs.h>

struct lkl_pci_dev {
	int irq;
	uint64_t dma_map_vaddr;
	struct pci_config config;
	struct pci_resource resources[LKL_PCI_RESOURCE_NUM];
};

static struct lkl_pci_dev *_lkl_pci_dev = NULL;

static struct lkl_pci_dev *pci_add(const char *name, void *kernel_ram,
				   unsigned long ram_size)
{
	assert(_lkl_pci_dev != NULL);
	_lkl_pci_dev->dma_map_vaddr = (uint64_t)kernel_ram;
	return _lkl_pci_dev;
}

static void pci_remove(struct lkl_pci_dev *dev)
{
}

static int pci_irq_init(struct lkl_pci_dev *dev, int irq)
{
	dev->irq = irq;
	return 0;
}

static unsigned long long map_page(struct lkl_pci_dev *dev, void *vaddr,
				   unsigned long size)
{
	return (unsigned long long)vaddr - dev->dma_map_vaddr;
}

static void unmap_page(struct lkl_pci_dev *dev, unsigned long long dma_handle,
		       unsigned long size)
{
}

static int pci_read(struct lkl_pci_dev *dev, int where, int size, void *val)
{
	memcpy(val, dev->config.config_space + where, size);
	lkl_printf("In pci_read where %08X, size %08X, value %08X\n", where,
		   size, *(uint32_t *)val);
	return size;
}

static int pci_write(struct lkl_pci_dev *dev, int where, int size, void *val)
{
	lkl_printf("In pci_write where %08X, size %08X, value %08X\n", where,
		   size, *(uint32_t *)val);

	// When a value of 0xffffffff is written to a BAR field in a device
	// PCI configuration space, the next read operation from this BAR field
	// should return size of the corresponding resource. The code below
	// assumes 32-bit BARs (64-bit BARs are not implemented at the moment).
	if ((where == offsetof(struct pci_config_normal_header, bar[0]) ||
	     where == offsetof(struct pci_config_normal_header, bar[1]) ||
	     where == offsetof(struct pci_config_normal_header, bar[2]) ||
	     where == offsetof(struct pci_config_normal_header, bar[3]) ||
	     where == offsetof(struct pci_config_normal_header, bar[4]) ||
	     where == offsetof(struct pci_config_normal_header, bar[5])) &&
	    size == sizeof(uint32_t) && (*(uint32_t *)val == 0xffffffff)) {
		size_t i = (where -
			    offsetof(struct pci_config_normal_header, bar[0])) /
			   sizeof(uint32_t);
		dev->config.hdr.bar[i] = dev->resources[i].size;
	} else {
		memcpy(dev->config.config_space + where, val, size);
	}
	return size;
}

static void *resource_alloc(struct lkl_pci_dev *dev,
			    unsigned long resource_size, int resource_index)
{
	if ((unsigned int)resource_index >= LKL_PCI_RESOURCE_NUM)
		return NULL;

	return dev->resources[resource_index].bar_iomem;
}

struct lkl_dev_pci_ops lkl_pci_ops = {
	.add = pci_add,
	.remove = pci_remove,
	.irq_init = pci_irq_init,
	.read = pci_read,
	.write = pci_write,
	.resource_alloc = resource_alloc,
	.map_page = map_page,
	.unmap_page = unmap_page,
};

const char *_lkl_pci_bus = "lkl_pci";

static struct lkl_pci_dev *lkl_pci_dev_alloc(struct pci_config *config,
					     struct pci_resource *resources,
					     size_t resource_num)
{
	struct lkl_pci_dev *dev = malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	memcpy(dev->config.config_space, config->config_space,
	       PCI_CONFIG_SPACE_SIZE);

	for (size_t i = 0; i < resource_num; i++) {
		dev->resources[i] = resources[i];
		// Current implementation supports 32-bit BARs only.
		assert(((uint64_t)resources[i].bar_iomem &
			0xffffffff00000000L) == 0);
		// The least significant bits of BARs are flags: make sure they are
		// zeroes (i.e. BARs describe 32-bit memory-mapped resources).
		assert(((uint64_t)resources[i].bar_iomem & 0xfff) == 0);
		dev->config.hdr.bar[i] = (uint32_t)resources[i].bar_iomem;
	}

	return dev;
}

int lkl_pci_dev_add(struct pci_config *config, struct pci_resource *resources,
		    size_t resource_num)
{
	// LKL PCI bus supports only one single device at the moment.
	if (_lkl_pci_dev != NULL)
		return -LKL_EBUSY;

	if (resource_num > LKL_PCI_RESOURCE_NUM)
		return -LKL_EINVAL;

	_lkl_pci_dev = lkl_pci_dev_alloc(config, resources, resource_num);
	if (_lkl_pci_dev == NULL)
		return -LKL_ENOMEM;

	// Kick the LKL PCI driver to enable lkl_pci bus and start device
	// enumeration.
	int res =
		lkl_sys_lkl_pci_bus_add(_lkl_pci_bus, strlen(_lkl_pci_bus) + 1);
	if (res != 0) {
		free(_lkl_pci_dev);
		_lkl_pci_dev = NULL;
	}

	return res;
}

int lkl_pci_dev_remove(void)
{
	int res = lkl_sys_lkl_pci_bus_remove(_lkl_pci_bus,
					     strlen(_lkl_pci_bus) + 1);
	if (_lkl_pci_dev != NULL) {
		free(_lkl_pci_dev);
		_lkl_pci_dev = NULL;
	}

	return res;
}