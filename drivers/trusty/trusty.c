/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/compiler.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/sm_err.h>
#include <linux/trusty/spci.h>
#include <linux/trusty/trusty.h>

#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

struct trusty_state;
static struct platform_driver trusty_driver;

struct trusty_work {
	struct trusty_state *ts;
	struct work_struct work;
};

struct trusty_state {
	struct mutex smc_lock;
	struct atomic_notifier_head notifier;
	struct completion cpu_idle_completion;
	char *version_str;
	u32 api_version;
	bool trusty_panicked;
	struct device *dev;
	struct workqueue_struct *nop_wq;
	struct trusty_work __percpu *nop_works;
	struct list_head nop_queue;
	spinlock_t nop_lock; /* protects nop_queue */
	struct device_dma_parameters dma_parms;
	void *spci_tx;
	void *spci_rx;
	u16 spci_local_id;
	u16 spci_remote_id;
	struct mutex share_memory_msg_lock; /* protects share_memory_msg */
};

#ifdef CONFIG_ARM64
#define SMC_ARG0		"x0"
#define SMC_ARG1		"x1"
#define SMC_ARG2		"x2"
#define SMC_ARG3		"x3"
#define SMC_ARG4		"x4"
#define SMC_ARG5		"x5"
#define SMC_ARG6		"x6"
#define SMC_ARG7		"x7"
#define SMC_ARCH_EXTENSION	""
#define SMC_REGISTERS_TRASHED	"x8", "x9", "x10", "x11", \
				"x12","x13","x14","x15","x16","x17"
#else
#define SMC_ARG0		"r0"
#define SMC_ARG1		"r1"
#define SMC_ARG2		"r2"
#define SMC_ARG3		"r3"
#define SMC_ARG4		"r4"
#define SMC_ARG5		"r5"
#define SMC_ARG6		"r6"
#define SMC_ARG7		"r7"
#define SMC_ARCH_EXTENSION	".arch_extension sec\n"
#define SMC_REGISTERS_TRASHED	"ip"
#endif

struct smc_ret8 {
	ulong r0;
	ulong r1;
	ulong r2;
	ulong r3;
	ulong r4;
	ulong r5;
	ulong r6;
	ulong r7;
};

static inline struct smc_ret8 smc8(ulong r0, ulong r1, ulong r2, ulong r3,
				   ulong r4, ulong r5, ulong r6, ulong r7)
{
	register ulong _r0 asm(SMC_ARG0) = r0;
	register ulong _r1 asm(SMC_ARG1) = r1;
	register ulong _r2 asm(SMC_ARG2) = r2;
	register ulong _r3 asm(SMC_ARG3) = r3;
	register ulong _r4 asm(SMC_ARG4) = r4;
	register ulong _r5 asm(SMC_ARG5) = r5;
	register ulong _r6 asm(SMC_ARG6) = r6;
	register ulong _r7 asm(SMC_ARG7) = r7;

	asm volatile(
		__asmeq("%0", SMC_ARG0)
		__asmeq("%1", SMC_ARG1)
		__asmeq("%2", SMC_ARG2)
		__asmeq("%3", SMC_ARG3)
		__asmeq("%4", SMC_ARG4)
		__asmeq("%5", SMC_ARG5)
		__asmeq("%6", SMC_ARG6)
		__asmeq("%7", SMC_ARG7)
		__asmeq("%8", SMC_ARG0)
		__asmeq("%9", SMC_ARG1)
		__asmeq("%10", SMC_ARG2)
		__asmeq("%11", SMC_ARG3)
		__asmeq("%12", SMC_ARG4)
		__asmeq("%13", SMC_ARG5)
		__asmeq("%14", SMC_ARG6)
		__asmeq("%15", SMC_ARG7)
		SMC_ARCH_EXTENSION
		"smc	#0"	/* switch to secure world */
		: "=r" (_r0), "=r" (_r1), "=r" (_r2), "=r" (_r3), "=r" (_r4),
		  "=r" (_r5), "=r" (_r6), "=r" (_r7)
		: "r" (_r0), "r" (_r1), "r" (_r2), "r" (_r3), "r" (_r4),
		  "r" (_r5), "r" (_r6), "r" (_r7)
		: SMC_REGISTERS_TRASHED);

	{
		struct smc_ret8 ret = {_r0, _r1, _r2, _r3, _r4, _r5, _r6, _r7};
		return ret;
	}
}

static inline ulong smc(ulong r0, ulong r1, ulong r2, ulong r3)
{
	return smc8(r0, r1, r2, r3, 0, 0, 0, 0).r0;
}

s32 trusty_fast_call32(struct device *dev, u32 smcnr, u32 a0, u32 a1, u32 a2)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	BUG_ON(!s);
	BUG_ON(!SMC_IS_FASTCALL(smcnr));
	BUG_ON(SMC_IS_SMC64(smcnr));

	return smc(smcnr, a0, a1, a2);
}
EXPORT_SYMBOL(trusty_fast_call32);

#ifdef CONFIG_64BIT
s64 trusty_fast_call64(struct device *dev, u64 smcnr, u64 a0, u64 a1, u64 a2)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	BUG_ON(!s);
	BUG_ON(!SMC_IS_FASTCALL(smcnr));
	BUG_ON(!SMC_IS_SMC64(smcnr));

	return smc(smcnr, a0, a1, a2);
}
#endif

static ulong trusty_std_call_inner(struct device *dev, ulong smcnr,
				   ulong a0, ulong a1, ulong a2)
{
	ulong ret;
	int retry = 5;

	dev_dbg(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx)\n",
		__func__, smcnr, a0, a1, a2);
	while (true) {
		ret = smc(smcnr, a0, a1, a2);
		while ((s32)ret == SM_ERR_FIQ_INTERRUPTED)
			ret = smc(SMC_SC_RESTART_FIQ, 0, 0, 0);
		if ((int)ret != SM_ERR_BUSY || !retry)
			break;

		dev_dbg(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx) returned busy, retry\n",
			__func__, smcnr, a0, a1, a2);
		retry--;
	}

	return ret;
}

static ulong trusty_std_call_helper(struct device *dev, ulong smcnr,
				    ulong a0, ulong a1, ulong a2)
{
	ulong ret;
	int sleep_time = 1;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	while (true) {
		local_irq_disable();
		atomic_notifier_call_chain(&s->notifier, TRUSTY_CALL_PREPARE,
					   NULL);
		ret = trusty_std_call_inner(dev, smcnr, a0, a1, a2);
		atomic_notifier_call_chain(&s->notifier, TRUSTY_CALL_RETURNED,
					   NULL);
		if (ret == SM_ERR_INTERRUPTED) {
			/*
			 * Make sure this cpu will eventually re-enter trusty
			 * even if the std_call resumes on another cpu.
			 */
			trusty_enqueue_nop(dev, NULL);
		}
		local_irq_enable();

		if ((int)ret != SM_ERR_BUSY)
			break;

		if (sleep_time == 256)
			dev_warn(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx) returned busy\n",
				 __func__, smcnr, a0, a1, a2);
		dev_dbg(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx) returned busy, wait %d ms\n",
			__func__, smcnr, a0, a1, a2, sleep_time);

		msleep(sleep_time);
		if (sleep_time < 1000)
			sleep_time <<= 1;

		dev_dbg(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx) retry\n",
			__func__, smcnr, a0, a1, a2);
	}

	if (sleep_time > 256)
		dev_warn(dev, "%s(0x%lx 0x%lx 0x%lx 0x%lx) busy cleared\n",
			 __func__, smcnr, a0, a1, a2);

	return ret;
}

static void trusty_std_call_cpu_idle(struct trusty_state *s)
{
	int ret;

	ret = wait_for_completion_timeout(&s->cpu_idle_completion, HZ * 10);
	if (!ret) {
		pr_warn("%s: timed out waiting for cpu idle to clear, retry anyway\n",
			__func__);
	}
}

s32 trusty_std_call32(struct device *dev, u32 smcnr, u32 a0, u32 a1, u32 a2)
{
	int ret;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	BUG_ON(SMC_IS_FASTCALL(smcnr));
	BUG_ON(SMC_IS_SMC64(smcnr));

	if (s->trusty_panicked) {
		/*
		 * Avoid calling the notifiers if trusty has panicked as they
		 * can trigger more calls.
		 */
		return SM_ERR_PANIC;
	}

	if (smcnr != SMC_SC_NOP) {
		mutex_lock(&s->smc_lock);
		reinit_completion(&s->cpu_idle_completion);
	}

	dev_dbg(dev, "%s(0x%x 0x%x 0x%x 0x%x) started\n",
		__func__, smcnr, a0, a1, a2);

	ret = trusty_std_call_helper(dev, smcnr, a0, a1, a2);
	while (ret == SM_ERR_INTERRUPTED || ret == SM_ERR_CPU_IDLE) {
		dev_dbg(dev, "%s(0x%x 0x%x 0x%x 0x%x) interrupted\n",
			__func__, smcnr, a0, a1, a2);
		if (ret == SM_ERR_CPU_IDLE)
			trusty_std_call_cpu_idle(s);
		ret = trusty_std_call_helper(dev, SMC_SC_RESTART_LAST, 0, 0, 0);
	}
	dev_dbg(dev, "%s(0x%x 0x%x 0x%x 0x%x) returned 0x%x\n",
		__func__, smcnr, a0, a1, a2, ret);

	if (WARN_ONCE(ret == SM_ERR_PANIC, "trusty crashed"))
		s->trusty_panicked = true;

	if (smcnr == SMC_SC_NOP)
		complete(&s->cpu_idle_completion);
	else
		mutex_unlock(&s->smc_lock);

	return ret;
}
EXPORT_SYMBOL(trusty_std_call32);

int trusty_share_memory(struct device *dev, uint64_t *id,
			struct scatterlist *sglist, unsigned int nents,
			pgprot_t pgprot)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));
	int ret;
	struct ns_mem_page_info pg_inf;
	struct scatterlist *sg;
	size_t count;
	unsigned int i;
	size_t len;
	u32 spci_handle = 0;
	u16 spci_mem_attr;
	size_t spci_len_arg;
	struct spci_memory_region_descriptor *mrd = s->spci_tx;
	size_t cmrd_offset = offsetof(struct spci_memory_region_descriptor,
				      memory_region_attributes_descriptors[1]);
	struct spci_constituent_memory_region_descriptor *cmrd = s->spci_tx +
								 cmrd_offset;
	struct smc_ret8 smc_ret;

	dev_dbg(s->dev, "%s\n", __func__);

	if (WARN_ON(dev->driver != &trusty_driver.driver))
		return -EINVAL;

	if (WARN_ON(nents < 1))
		return -EINVAL;

	if (nents != 1 && s->api_version < TRUSTY_API_VERSION_MEM_OBJ) {
		dev_err(s->dev, "%s: old trusty version does not support non-contiguous memory objects\n",
			__func__);
		return -ENOTSUPP;
	}

	count = dma_map_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);
	if (count != nents) {
		dev_err(s->dev, "failed to dma map sg_table\n");
		return -EINVAL;
	}

	sg = sglist;
	ret = trusty_encode_page_info(&pg_inf, phys_to_page(sg_dma_address(sg)),
				      pgprot);
	if (ret) {
		dev_err(s->dev, "%s: trusty_encode_page_info failed\n",
			__func__);
		goto err_encode_page_info;
	}

	if (s->api_version < TRUSTY_API_VERSION_MEM_OBJ) {
		*id = pg_inf.attr;
		return 0;
	}

	len = 0;
	for_each_sg(sglist, sg, nents, i)
		len += sg_dma_len(sg);

	mutex_lock(&s->share_memory_msg_lock);

	mrd->tag = 0;
	mrd->flags = 0;
	mrd->sender_id = s->spci_local_id;
	mrd->reserved = 0;
	mrd->total_page_count = len / PAGE_SIZE;
	mrd->constituent_memory_region_count = nents;
	mrd->constituent_memory_region_descriptor_offset = cmrd_offset;
	mrd->memory_region_attributes_descriptor_count = 1;
	mrd->memory_region_attributes_descriptors[0].receiver_id =
		s->spci_remote_id;

	/* TODO: move to helper, e.g. trusty_encode_page_info */
	spci_mem_attr = SPCI_MEM_ATTR_NORMAL_MEMORY_CACHED_WB |
			SPCI_MEM_ATTR_INNER_SHAREABLE;
	if (!(pg_inf.attr & (1 << 7)))
		spci_mem_attr |= SPCI_MEM_ATTR_RW;

	mrd->memory_region_attributes_descriptors[0].memory_attributes =
		spci_mem_attr;

	spci_len_arg = cmrd_offset + nents * sizeof(*cmrd);
	sg = sglist;
	while (count) {
		size_t i;
		size_t lcount = min(count,
				    (PAGE_SIZE - cmrd_offset) / sizeof(*cmrd));
		size_t fragment_len = lcount * sizeof(*cmrd) + cmrd_offset;

		for (i = 0; i < lcount; i++) {
			cmrd[i].address = sg_dma_address(sg);
			cmrd[i].page_count = sg_dma_len(sg) / PAGE_SIZE;
			sg = sg_next(sg);
		}
		count -= lcount;
		smc_ret = smc8(SMC_FC_SPCI_MEM_SHARE, 0, 0, fragment_len,
			       spci_len_arg, 0, 0, 0);
		if (smc_ret.r0 == SMC_FC_SPCI_SUCCESS) {
			dev_dbg(s->dev, "%s: fragment_len %zd/%zd, got handle 0x%lx\n",
				__func__, fragment_len, spci_len_arg,
				smc_ret.r2);
			if (cmrd_offset) {
				spci_handle = smc_ret.r2;
			} else if (smc_ret.r2 != spci_handle) {
				dev_err(s->dev, "%s: fragment_len %zd/%zd, handle mismatch 0x%lx != 0x%x\n",
					__func__, fragment_len, spci_len_arg,
					smc_ret.r2, spci_handle);
			}
		} else {
			dev_err(s->dev, "%s: SMC_FC_SPCI_MEM_SHARE failed 0x%x 0x%x 0x%x",
				__func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
			break;
		}

		cmrd = s->spci_tx;
		cmrd_offset = 0;
		spci_len_arg = 0;
	}

	count = nents;

	ret = trusty_std_call32(dev, SMC_SC_MSG_SHARE_MEMORY, spci_handle,
				spci_mem_attr, 0);
	if (!ret)
		*id = spci_handle | (u64)spci_mem_attr << 32;

	mutex_unlock(&s->share_memory_msg_lock);

	if (!ret) {
		dev_dbg(s->dev, "%s: done\n", __func__);
		return 0;
	}

	dev_err(s->dev, "%s: SMC_SC_MSG_SHARE_MEMORY failed %d",
		__func__, ret);

err_encode_page_info:
	dma_unmap_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);
	return ret;
}
EXPORT_SYMBOL(trusty_share_memory);

/*
 * trusty_share_memory_compat - trusty_share_memory wrapper for old apis
 *
 * Call trusty_share_memory and filter out memory attributes if trusty version
 * is old. Used by clients that used to pass just a physical address to trusty
 * instead of a physical address plus memory attributes value.
 */
int trusty_share_memory_compat(struct device *dev, uint64_t *id,
			       struct scatterlist *sglist, unsigned int nents,
			       pgprot_t pgprot)
{
	int ret;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	ret = trusty_share_memory(dev, id, sglist, nents, pgprot);
	if (!ret && s->api_version < TRUSTY_API_VERSION_PHYS_MEM_OBJ)
		*id &= 0x0000FFFFFFFFF000ull;

	return ret;
}
EXPORT_SYMBOL(trusty_share_memory_compat);

int trusty_reclaim_memory(struct device *dev, uint64_t id,
			  struct scatterlist *sglist, unsigned int nents)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));
	int ret;
	struct smc_ret8 smc_ret;

	dev_dbg(s->dev, "%s\n", __func__);

	if (WARN_ON(dev->driver != &trusty_driver.driver))
		return -EINVAL;

	if (WARN_ON(nents < 1))
		return -EINVAL;

	if (s->api_version < TRUSTY_API_VERSION_MEM_OBJ) {
		if (nents != 1) {
			dev_err(s->dev, "%s: not supported\n", __func__);
			return -ENOTSUPP;
		}

		dma_unmap_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);

		dev_dbg(s->dev, "%s: done\n", __func__);
		return 0;
	}

	mutex_lock(&s->share_memory_msg_lock);

	ret = trusty_std_call32(dev, SMC_SC_MSG_REVOKE_MEMORY, (u32)id,
				id >> 32, 0);
	if (ret) {
		dev_err(s->dev, "%s: SMC_SC_MSG_REVOKE_MEMORY failed %d",
			__func__, ret);
		if (ret == SM_ERR_NOT_ALLOWED)
			ret = -EBUSY;
		else
			ret = -EIO;
	}

	smc_ret = smc8(SMC_FC_SPCI_MEM_RECLAIM, id, 0, 0, 0, 0, 0, 0);
	if (smc_ret.r0 != SMC_FC_SPCI_SUCCESS) {
		dev_err(s->dev, "%s: SMC_FC_SPCI_MEM_RECLAIM failed 0x%x 0x%x 0x%x",
			__func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
		if (smc_ret.r0 == SMC_FC_SPCI_ERROR &&
		    smc_ret.r2 == SPCI_ERROR_DENIED)
			ret = -EBUSY;
		else
			ret = -EIO;
	}

	mutex_unlock(&s->share_memory_msg_lock);

	if (ret != 0)
		return ret;

	dma_unmap_sg(dev, sglist, nents, DMA_BIDIRECTIONAL);

	dev_dbg(s->dev, "%s: done\n", __func__);
	return 0;
}
EXPORT_SYMBOL(trusty_reclaim_memory);

int trusty_call_notifier_register(struct device *dev, struct notifier_block *n)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	return atomic_notifier_chain_register(&s->notifier, n);
}
EXPORT_SYMBOL(trusty_call_notifier_register);

int trusty_call_notifier_unregister(struct device *dev,
				    struct notifier_block *n)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	return atomic_notifier_chain_unregister(&s->notifier, n);
}
EXPORT_SYMBOL(trusty_call_notifier_unregister);

static int trusty_remove_child(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

ssize_t trusty_version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	return scnprintf(buf, PAGE_SIZE, "%s\n", s->version_str);
}

DEVICE_ATTR(trusty_version, S_IRUSR, trusty_version_show, NULL);

const char *trusty_version_str_get(struct device *dev)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	return s->version_str;
}
EXPORT_SYMBOL(trusty_version_str_get);

static int trusty_init_msg_buf(struct trusty_state *s, struct device *dev)
{
	phys_addr_t tx_paddr;
	phys_addr_t rx_paddr;
	int ret;
	struct smc_ret8 smc_ret;

	if (s->api_version < TRUSTY_API_VERSION_MEM_OBJ)
		return 0;

	/*
	 * Set SPCI endpoint IDs.
	 *
	 * Hardcode 0x8000 for the secure os.
	 * TODO: Use SPCI call or device tree to configure this dynamically
	 */
	smc_ret = smc8(SMC_FC_SPCI_ID_GET, 0, 0, 0, 0, 0, 0,
		       0);
	if (smc_ret.r0 != SMC_FC_SPCI_SUCCESS) {
		dev_err(s->dev, "%s: SMC_FC_SPCI_ID_GET failed 0x%x 0x%x 0x%x",
			__func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
		ret = -EIO;
		goto err_id_get;
	}

	s->spci_local_id = smc_ret.r2;
	s->spci_remote_id = 0x8000;

	s->spci_tx = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!s->spci_tx) {
		ret = -ENOMEM;
		goto err_alloc_tx;
	}
	tx_paddr = virt_to_phys(s->spci_tx);
	if (WARN_ON(tx_paddr & (PAGE_SIZE - 1))) {
		ret = -EINVAL;
		goto err_unaligned_tx_buf;
	}

	s->spci_rx = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!s->spci_rx) {
		ret = -ENOMEM;
		goto err_alloc_rx;
	}
	rx_paddr = virt_to_phys(s->spci_rx);
	if (WARN_ON(rx_paddr & (PAGE_SIZE - 1))) {
		ret = -EINVAL;
		goto err_unaligned_rx_buf;
	}

	smc_ret = smc8(SMC_FC64_SPCI_RXTX_MAP, tx_paddr, rx_paddr, 1, 0, 0, 0,
		       0);
	if (smc_ret.r0 != SMC_FC_SPCI_SUCCESS) {
		dev_err(s->dev, "%s: SMC_FC64_SPCI_RXTX_MAP failed 0x%x 0x%x 0x%x",
			__func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
		ret = -EIO;
		goto err_rxtx_map;
	}

	return 0;

err_rxtx_map:
err_unaligned_rx_buf:
	kfree(s->spci_rx);
	s->spci_rx = NULL;
err_alloc_rx:
err_unaligned_tx_buf:
	kfree(s->spci_tx);
	s->spci_tx = NULL;
err_alloc_tx:
err_id_get:
	return ret;
}

static void trusty_free_msg_buf(struct trusty_state *s, struct device *dev)
{
	struct smc_ret8 smc_ret;

	smc_ret = smc8(SMC_FC_SPCI_RXTX_UNMAP, 0, 0, 0, 0, 0, 0, 0);
	if (smc_ret.r0 != SMC_FC_SPCI_SUCCESS) {
		dev_err(s->dev, "%s: SMC_FC_SPCI_RXTX_UNMAP failed 0x%x 0x%x 0x%x",
			__func__, smc_ret.r0, smc_ret.r1, smc_ret.r2);
	} else {
		kfree(s->spci_rx);
		kfree(s->spci_tx);
	}
}

static void trusty_init_version(struct trusty_state *s, struct device *dev)
{
	int ret;
	int i;
	int version_str_len;

	ret = trusty_fast_call32(dev, SMC_FC_GET_VERSION_STR, -1, 0, 0);
	if (ret <= 0)
		goto err_get_size;

	version_str_len = ret;

	s->version_str = kmalloc(version_str_len + 1, GFP_KERNEL);
	for (i = 0; i < version_str_len; i++) {
		ret = trusty_fast_call32(dev, SMC_FC_GET_VERSION_STR, i, 0, 0);
		if (ret < 0)
			goto err_get_char;
		s->version_str[i] = ret;
	}
	s->version_str[i] = '\0';

	dev_info(dev, "trusty version: %s\n", s->version_str);

	ret = device_create_file(dev, &dev_attr_trusty_version);
	if (ret)
		goto err_create_file;
	return;

err_create_file:
err_get_char:
	kfree(s->version_str);
	s->version_str = NULL;
err_get_size:
	dev_err(dev, "failed to get version: %d\n", ret);
}

u32 trusty_get_api_version(struct device *dev)
{
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	return s->api_version;
}
EXPORT_SYMBOL(trusty_get_api_version);

static int trusty_init_api_version(struct trusty_state *s, struct device *dev)
{
	u32 api_version;
	api_version = trusty_fast_call32(dev, SMC_FC_API_VERSION,
					 TRUSTY_API_VERSION_CURRENT, 0, 0);
	if (api_version == SM_ERR_UNDEFINED_SMC)
		api_version = 0;

	if (api_version > TRUSTY_API_VERSION_CURRENT) {
		dev_err(dev, "unsupported api version %u > %u\n",
			api_version, TRUSTY_API_VERSION_CURRENT);
		return -EINVAL;
	}

	dev_info(dev, "selected api version: %u (requested %u)\n",
		 api_version, TRUSTY_API_VERSION_CURRENT);
	s->api_version = api_version;

	return 0;
}

static bool dequeue_nop(struct trusty_state *s, u32 *args)
{
	unsigned long flags;
	struct trusty_nop *nop = NULL;

	spin_lock_irqsave(&s->nop_lock, flags);
	if (!list_empty(&s->nop_queue)) {
		nop = list_first_entry(&s->nop_queue,
				       struct trusty_nop, node);
		list_del_init(&nop->node);
		args[0] = nop->args[0];
		args[1] = nop->args[1];
		args[2] = nop->args[2];
	} else {
		args[0] = 0;
		args[1] = 0;
		args[2] = 0;
	}
	spin_unlock_irqrestore(&s->nop_lock, flags);
	return nop;
}

static void locked_nop_work_func(struct work_struct *work)
{
	int ret;
	struct trusty_work *tw = container_of(work, struct trusty_work, work);
	struct trusty_state *s = tw->ts;

	dev_dbg(s->dev, "%s\n", __func__);

	ret = trusty_std_call32(s->dev, SMC_SC_LOCKED_NOP, 0, 0, 0);
	if (ret != 0)
		dev_err(s->dev, "%s: SMC_SC_LOCKED_NOP failed %d",
			__func__, ret);

	dev_dbg(s->dev, "%s: done\n", __func__);
}

static void nop_work_func(struct work_struct *work)
{
	int ret;
	bool next;
	u32 args[3];
	u32 last_arg0;
	struct trusty_work *tw = container_of(work, struct trusty_work, work);
	struct trusty_state *s = tw->ts;

	dev_dbg(s->dev, "%s:\n", __func__);

	dequeue_nop(s, args);
	do {
		dev_dbg(s->dev, "%s: %x %x %x\n",
			__func__, args[0], args[1], args[2]);

		last_arg0 = args[0];
		ret = trusty_std_call32(s->dev, SMC_SC_NOP,
					args[0], args[1], args[2]);

		next = dequeue_nop(s, args);

		if (ret == SM_ERR_NOP_INTERRUPTED) {
			next = true;
		} else if (ret != SM_ERR_NOP_DONE) {
			dev_err(s->dev, "%s: SMC_SC_NOP %x failed %d",
				__func__, last_arg0, ret);
			if (last_arg0) {
				/*
				 * Don't break out of the loop if a non-default
				 * nop-handler returns an error.
				 */
				next = true;
			}
		}
	} while (next);

	dev_dbg(s->dev, "%s: done\n", __func__);
}

void trusty_enqueue_nop(struct device *dev, struct trusty_nop *nop)
{
	unsigned long flags;
	struct trusty_work *tw;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	preempt_disable();
	tw = this_cpu_ptr(s->nop_works);
	if (nop) {
		WARN_ON(s->api_version < TRUSTY_API_VERSION_SMP_NOP);

		spin_lock_irqsave(&s->nop_lock, flags);
		if (list_empty(&nop->node))
			list_add_tail(&nop->node, &s->nop_queue);
		spin_unlock_irqrestore(&s->nop_lock, flags);
	}
	queue_work(s->nop_wq, &tw->work);
	preempt_enable();
}
EXPORT_SYMBOL(trusty_enqueue_nop);

void trusty_dequeue_nop(struct device *dev, struct trusty_nop *nop)
{
	unsigned long flags;
	struct trusty_state *s = platform_get_drvdata(to_platform_device(dev));

	if (WARN_ON(!nop))
		return;

	spin_lock_irqsave(&s->nop_lock, flags);
	if (!list_empty(&nop->node))
		list_del_init(&nop->node);
	spin_unlock_irqrestore(&s->nop_lock, flags);
}
EXPORT_SYMBOL(trusty_dequeue_nop);

static int trusty_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int cpu;
	work_func_t work_func;
	struct trusty_state *s;
	struct device_node *node = pdev->dev.of_node;

	if (!node) {
		dev_err(&pdev->dev, "of_node required\n");
		return -EINVAL;
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto err_allocate_state;
	}

	s->dev = &pdev->dev;
	spin_lock_init(&s->nop_lock);
	INIT_LIST_HEAD(&s->nop_queue);
	mutex_init(&s->smc_lock);
	mutex_init(&s->share_memory_msg_lock);
	ATOMIC_INIT_NOTIFIER_HEAD(&s->notifier);
	init_completion(&s->cpu_idle_completion);

	s->dev->dma_parms = &s->dma_parms;
	dma_set_max_seg_size(s->dev, 0xfffff000); /* dma_parms limit */

	platform_set_drvdata(pdev, s);

	trusty_init_version(s, &pdev->dev);

	ret = trusty_init_api_version(s, &pdev->dev);
	if (ret < 0)
		goto err_api_version;

	ret = trusty_init_msg_buf(s, &pdev->dev);
	if (ret < 0)
		goto err_init_msg_buf;

	s->nop_wq = alloc_workqueue("trusty-nop-wq", WQ_CPU_INTENSIVE, 0);
	if (!s->nop_wq) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed create trusty-nop-wq\n");
		goto err_create_nop_wq;
	}

	s->nop_works = alloc_percpu(struct trusty_work);
	if (!s->nop_works) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate works\n");
		goto err_alloc_works;
	}

	if (s->api_version < TRUSTY_API_VERSION_SMP)
		work_func = locked_nop_work_func;
	else
		work_func = nop_work_func;

	for_each_possible_cpu(cpu) {
		struct trusty_work *tw = per_cpu_ptr(s->nop_works, cpu);

		tw->ts = s;
		INIT_WORK(&tw->work, work_func);
	}

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add children: %d\n", ret);
		goto err_add_children;
	}

	return 0;

err_add_children:
	for_each_possible_cpu(cpu) {
		struct trusty_work *tw = per_cpu_ptr(s->nop_works, cpu);

		flush_work(&tw->work);
	}
	free_percpu(s->nop_works);
err_alloc_works:
	destroy_workqueue(s->nop_wq);
err_create_nop_wq:
	trusty_free_msg_buf(s, &pdev->dev);
err_init_msg_buf:
err_api_version:
	s->dev->dma_parms = NULL;
	if (s->version_str) {
		device_remove_file(&pdev->dev, &dev_attr_trusty_version);
		kfree(s->version_str);
	}
	device_for_each_child(&pdev->dev, NULL, trusty_remove_child);
	mutex_destroy(&s->share_memory_msg_lock);
	mutex_destroy(&s->smc_lock);
	kfree(s);
err_allocate_state:
	return ret;
}

static int trusty_remove(struct platform_device *pdev)
{
	unsigned int cpu;
	struct trusty_state *s = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, trusty_remove_child);

	for_each_possible_cpu(cpu) {
		struct trusty_work *tw = per_cpu_ptr(s->nop_works, cpu);

		flush_work(&tw->work);
	}
	free_percpu(s->nop_works);
	destroy_workqueue(s->nop_wq);

	mutex_destroy(&s->share_memory_msg_lock);
	mutex_destroy(&s->smc_lock);
	trusty_free_msg_buf(s, &pdev->dev);
	s->dev->dma_parms = NULL;
	if (s->version_str) {
		device_remove_file(&pdev->dev, &dev_attr_trusty_version);
		kfree(s->version_str);
	}
	kfree(s);
	return 0;
}

static const struct of_device_id trusty_of_match[] = {
	{ .compatible = "android,trusty-smc-v1", },
	{},
};

static struct platform_driver trusty_driver = {
	.probe = trusty_probe,
	.remove = trusty_remove,
	.driver	= {
		.name = "trusty",
		.owner = THIS_MODULE,
		.of_match_table = trusty_of_match,
	},
};

static int __init trusty_driver_init(void)
{
	return platform_driver_register(&trusty_driver);
}

static void __exit trusty_driver_exit(void)
{
	platform_driver_unregister(&trusty_driver);
}

subsys_initcall(trusty_driver_init);
module_exit(trusty_driver_exit);
