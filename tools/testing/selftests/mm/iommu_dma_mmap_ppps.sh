#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run iommu_dma_mmap_ppps with its fixture module loaded.  The fixture binds
# to an IOMMU-mapped QEMU EDU PCI device, so skip when there is none.

ksft_skip=4
here=$(dirname "$0")
runner=$here/ppps_run_module.sh
[ -x "$runner" ] || runner=$here/../ppps/ppps_run_module.sh
found=

for device in /sys/bus/pci/devices/*; do
	if [ "$(cat "$device/vendor" 2>/dev/null)" = "0x1234" ] &&
	   [ "$(cat "$device/device" 2>/dev/null)" = "0x11e8" ] &&
	   [ -e "$device/iommu_group" ]; then
		found=1
		break
	fi
done
if [ -z "$found" ]; then
	echo "$0: an IOMMU-mapped QEMU EDU device is required"
	exit "$ksft_skip"
fi

exec "$runner" "$here/ppps_modules/iommu_dma_mmap_ppps_module.ko" \
	/dev/iommu_dma_mmap_ppps -- "$here/iommu_dma_mmap_ppps"
