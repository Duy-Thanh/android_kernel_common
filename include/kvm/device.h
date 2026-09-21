// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */

#ifndef __KVM_DEVICE_H
#define __KVM_DEVICE_H

#include <asm/kvm_host.h>

/*
 * @base:      physical address of MMIO resource.
 * @size:      size of resource in bytes.
 * @guest_ipa: IPA the owning guest sees this resource at. Only el2_managed
 *             devices use it. The VMM picks the IPA, so the hypervisor learns
 *             it on the first guest access and clears it at VM teardown.
 *             Zero means "not known yet".
 */
struct pkvm_dev_resource {
	u64 base;
	u64 size;
	u64 guest_ipa;
};

/*
 * @id: hypervisor ID of the IOMMU as defined by the driver.
 * @endpoint: endpoint ID of the device.
 */
struct pkvm_dev_iommu {
	u64 id;
	u64 endpoint;
};

#define PKVM_DEVICE_MAX_RESOURCE	32
#define PKVM_DEVICE_MAX_IOMMU		32

struct pkvm_device {
	struct pkvm_dev_resource resources[PKVM_DEVICE_MAX_RESOURCE];
	struct pkvm_dev_iommu iommus[PKVM_DEVICE_MAX_IOMMU];
	u32 nr_resources;
	u32 nr_iommus;
	u32 group_id;
	void *ctxt; /* Current context of the device */
	unsigned short refcount;
	bool power_locked;
	/*
	 * When true, the hypervisor never installs a stage-2 mapping for this
	 * device, so every guest access traps to EL2 and an EL2 module emulates
	 * it. Set from the "pkvm,el2-managed-mmio" DT property.
	 */
	bool el2_managed;
	struct pkvm_device_ops *ops;
	void *cookie; /* cookie from drivers. */
};

#endif /* #ifndef __KVM_DEVICE_H */
