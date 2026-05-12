# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_DMAR_TABLE) += dmar.o
<<<<<<< TARGET BRANCH (20a1d6f1c48a1cdea99c1ffff1d7812dbbf3e0d1 ANDROID: ABI: Update the symbol list for mtk am: 1c5ded243b)
obj-$(CONFIG_INTEL_IOMMU) += iommu.o pasid.o nested.o cache.o
obj-$(CONFIG_DMAR_TABLE) += trace.o
||||||| BASE          (1c5ded243bc242777bc62632c082c086b90ed490 ANDROID: ABI: Update the symbol list for mtk)
obj-$(CONFIG_INTEL_IOMMU) += iommu.o pasid.o nested.o cache.o
obj-$(CONFIG_DMAR_TABLE) += trace.o cap_audit.o
=======
obj-$(CONFIG_INTEL_IOMMU) += iommu.o pasid.o nested.o cache.o prq.o
obj-$(CONFIG_DMAR_TABLE) += trace.o cap_audit.o
>>>>>>> SOURCE BRANCH (1cbb81cdf8fc5fa7f6735899639a7a5d1bb5be35 Merge tag 'android16-6.12.77_r00' into android16-6.12)
obj-$(CONFIG_DMAR_PERF) += perf.o
obj-$(CONFIG_INTEL_IOMMU_DEBUGFS) += debugfs.o
obj-$(CONFIG_INTEL_IOMMU_SVM) += svm.o
ifdef CONFIG_INTEL_IOMMU
obj-$(CONFIG_IRQ_REMAP) += irq_remapping.o
endif
obj-$(CONFIG_INTEL_IOMMU_PERF_EVENTS) += perfmon.o
