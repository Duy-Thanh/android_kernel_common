/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LKL_PCI_LIB_H__
#define __LKL_PCI_LIB_H__

#include <stddef.h>
#include <stdint.h>

#include "iomem.h"

#define LKL_PCI_RESOURCE_NUM 6

struct pci_config_normal_header {
	uint16_t vendor_id;
	uint16_t device_id;

	uint16_t command;
	uint16_t status;

	uint8_t revision_id;
	uint8_t prog_if;
	uint8_t subclass;
	uint8_t class;

	uint8_t cache_line_size;
	uint8_t latency_timer;
	uint8_t header_type;
	uint8_t bist;

	uint32_t bar[LKL_PCI_RESOURCE_NUM];

	uint32_t cardbus_cis;

	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;

	uint32_t expansion_rom;

	uint8_t capabilities_pointer;
	uint8_t reserved_1;
	uint8_t reserved_2;
	uint8_t reserved_3;
	uint32_t reserved_4;

	uint8_t interrupt_line;
	uint8_t interrupt_pin;
	uint8_t min_grant;
	uint8_t max_latency;
};

#define PCI_CONFIG_SPACE_SIZE 256

struct pci_config {
	union {
		struct pci_config_normal_header hdr;
		uint8_t config_space[PCI_CONFIG_SPACE_SIZE];
	};
};

struct pci_resource {
	void *bar_iomem;
	size_t size;
};

int lkl_pci_dev_add(struct pci_config *config, struct pci_resource *resources,
		    size_t resource_num);

int lkl_pci_dev_remove(void);

#endif // __LKL_PCI_LIB_H__