// SPDX-License-Identifier: GPL-2.0-only

#include <nvhe/pkvm.h>

#include "module.h"
#include "emulate.h"
#include "gic-v3-its.h"
#include "memory_util.h"

#include <nvhe/spinlock.h>
#include <linux/irqchip/arm-gic-v3.h>

#define GITS_TRANSLATER_PAGE ALIGN_DOWN(GITS_TRANSLATER, PAGE_SIZE)
#define GITS_TRANSLATER_PFN (GITS_TRANSLATER_PAGE >> PAGE_SHIFT)

DEFINE_MEM_TRACKER(tracker_its, &region_tracker_shared_ops);

struct hyp_gic_v3_its_baser {
	int baser_n;
	u64 value;
	void __iomem *table;

	struct emulate emulate;
};

struct hyp_gic_v3_its {
	void __iomem *base;
	struct emulate emulate;
	u64 saved_cbaser;
	void *host_cmd_base_va;
	void *host_cmd_cwriter_va;
	void *shadow_cmd;
	u64 typer;

	struct hyp_gic_v3_its_baser basers[GITS_BASER_NR_REGS];
};

struct hyp_gic_v3_its_handler {
	u64 offset;
	u8 access_size;
	int (*write)(struct hyp_gic_v3_its *its, u64 offset, u64 value);
	int (*read)(struct hyp_gic_v3_its *its, u64 offset, u64 *read);
};

static int forbidden_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	return -EINVAL;
}

static int cbaser_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	size_t cmdq_len = value & GENMASK(7, 0);

	if ((its->saved_cbaser & GENMASK(7, 0)) != cmdq_len ||
	    GITS_CBASER_ADDRESS(value) != GITS_CBASER_ADDRESS(its->saved_cbaser))
	    return -EPERM;

	its->saved_cbaser = value;
	writeq_relaxed(value, its->base + GITS_CBASER);
	return 0;
}

static int cbaser_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = its->saved_cbaser;
	return 0;
}

static int creadr_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = readq_relaxed(its->base + GITS_CREADR);
	return 0;
}

struct its_cmd_block {
	union {
		u64	raw_cmd[4];
		__le64	raw_cmd_le[4];
	};
};

static int parse_its_mapd(struct hyp_gic_v3_its *its, struct its_cmd_block *cmd)
{
	int nr_ites, len = cmd->raw_cmd[1] & GENMASK(4, 0);
	phys_addr_t itt_addr_end, itt_addr = cmd->raw_cmd[2] & GENMASK(51, 8);
	bool remove = (cmd->raw_cmd[2] & BIT(63)) == 0;
	u64 sz;

	nr_ites = 1 << (len + 1);
	sz = nr_ites * (FIELD_GET(GITS_TYPER_ITT_ENTRY_SIZE, its->typer) + 1);
	sz = max(sz, ITS_ITT_ALIGN) + ITS_ITT_ALIGN - 1;
	sz = max(sz, PAGE_SIZE);

	itt_addr = PAGE_ALIGN_DOWN(itt_addr);
	if (check_add_overflow(itt_addr, sz, &itt_addr_end))
		return -EINVAL;

	if (remove)
		return region_tracker_dec(&tracker_its, itt_addr, itt_addr_end);

	return region_tracker_inc(&tracker_its, itt_addr, itt_addr_end);
}

static int parse_its_cmdq(struct hyp_gic_v3_its *its, int cmd_offset, size_t len)
{
	struct its_cmd_block *cmd = its->shadow_cmd + cmd_offset;
	u8 cmd_req;
	int ret;

	while (len > 0) {
		cmd_req = cmd->raw_cmd[0] & GENMASK(7, 0);

		/* Reject GICv4 ITS commands for now */
		if (cmd_req & GITS_CMD_GICv4(0))
			return -EPERM;

		switch (cmd_req) {
		case GITS_CMD_MAPD:
			ret = parse_its_mapd(its, cmd);
			if (ret)
				return ret;
			break;
		default:
			break;
		}

		cmd++;
		len -= sizeof(struct its_cmd_block);
	}

	return 0;
}

static int cwriter_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	u64 cwriter_offset = value & GENMASK(19, 5);
	int cmd_len, cmd_offset;
	int ret;

	if (cwriter_offset >= ITS_CMD_QUEUE_SZ)
		return -EINVAL;

	cmd_offset = its->host_cmd_cwriter_va - its->host_cmd_base_va;
	cmd_len = cwriter_offset - cmd_offset;
	if (cmd_len < 0) {
		/* Detected command queue wrap around */
		its->host_cmd_cwriter_va = its->host_cmd_base_va;
		cmd_len = cwriter_offset;
		cmd_offset = 0;
	}

	if (cmd_offset + cmd_len > ITS_CMD_QUEUE_SZ) {
		mod_ops->puts("ITS shadow dropping command");
		return -EPERM;
	}

	memcpy(its->shadow_cmd + cmd_offset, its->host_cmd_cwriter_va, cmd_len);

	ret = parse_its_cmdq(its, cmd_offset, cmd_len);
	if (ret)
		return ret;

	its->host_cmd_cwriter_va += cmd_len;
	writeq_relaxed(value, its->base + GITS_CWRITER);
	return 0;
}

static int cwriter_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = readq_relaxed(its->base + GITS_CWRITER);
	return 0;
}

static int baser_write(struct hyp_gic_v3_its *its, u64 offset, u64 value)
{
	writeq_relaxed(value, its->base + offset);
	return 0;
}

static int baser_read(struct hyp_gic_v3_its *its, u64 offset, u64 *read)
{
	*read = readq_relaxed(its->base + offset);
	return 0;
}

#define GIC_V3_ITS_HANDLER(off, sz, write_cb, read_cb)	\
{							\
	.offset = (off),				\
	.access_size = (sz),				\
	.write = (write_cb),				\
	.read = (read_cb),					\
}

#define GIC_V3_ITS_HANDLER_64(off, write_cb, read_cb) \
	GIC_V3_ITS_HANDLER(off, sizeof(u64), write_cb, read_cb)

#define GIC_V3_ITS_HANDLER_RONLY(off, sz, read) \
	GIC_V3_ITS_HANDLER(off, sz, forbidden_write, read)

#define GIC_V3_ITS_HANDLER_RONLY_64(off, read) \
	GIC_V3_ITS_HANDLER_RONLY(off, sizeof(u64), read)

#define GIT_V3_ITS_BASERn_HANDER(n) \
	GIC_V3_ITS_HANDLER_64(GITS_BASER + ((n) << 3), baser_write, baser_read)

static const struct hyp_gic_v3_its_handler gic_v3_its_handlers[] =
{
	GIC_V3_ITS_HANDLER_64(GITS_CBASER, cbaser_write, cbaser_read),
	GIC_V3_ITS_HANDLER_RONLY_64(GITS_CREADR, creadr_read),
	GIC_V3_ITS_HANDLER_64(GITS_CWRITER, cwriter_write, cwriter_read),

	GIT_V3_ITS_BASERn_HANDER(0),
	GIT_V3_ITS_BASERn_HANDER(1),
	GIT_V3_ITS_BASERn_HANDER(2),
	GIT_V3_ITS_BASERn_HANDER(3),
	GIT_V3_ITS_BASERn_HANDER(4),
	GIT_V3_ITS_BASERn_HANDER(5),
	GIT_V3_ITS_BASERn_HANDER(6),
	GIT_V3_ITS_BASERn_HANDER(7),
	{},
};

static struct hyp_gic_v3_its its_devs[8];
static size_t its_dev_count;
static DEFINE_HYP_SPINLOCK(its_devs_lock);
static DEFINE_HYP_SPINLOCK(its_lock);

#define for_each_its(__its) \
	for (__its = its_devs; __its != &its_devs[its_dev_count]; __its++)

static int its_emulate_handler(struct emulate *emulate, u64 offset, bool write,
			       u64 *reg, int reg_size)
{
	struct hyp_gic_v3_its *its = emulate->priv;
	const struct hyp_gic_v3_its_handler *handler;
	int ret;

	for (handler = gic_v3_its_handlers; handler->access_size != 0; handler++) {
		if (offset < handler->offset ||
		    offset >= handler->offset + handler->access_size)
			continue;

		/* Check for unaligned register access */
		if (offset != handler->offset || (handler->access_size & (reg_size - 1)))
			continue;

		if (write && handler->write) {
			hyp_spin_lock(&its_lock);
			ret = handler->write(its, offset, *reg);
			hyp_spin_unlock(&its_lock);
			return ret;
		}

		if (!write && handler->read) {
			hyp_spin_lock(&its_lock);
			ret = handler->read(its, offset, reg);
			hyp_spin_unlock(&its_lock);
			return ret;
		}

		break;
	}

	hyp_emulate_passthrough(its->base, offset, write, reg, reg_size);
	return 0;
}

static int hyp_gic_v3_its_shadow_cmdq(struct hyp_gic_v3_its *its, u64 host_cmd_base_pa)
{
	int ret, num_pages, i;
	phys_addr_t shadow_cmd_pa;
	u64 pfn, cbaser = readq_relaxed(its->base + GITS_CBASER);
	its->saved_cbaser = cbaser;

	shadow_cmd_pa = GITS_CBASER_ADDRESS(cbaser);
	num_pages = ITS_CMD_QUEUE_SZ >> PAGE_SHIFT;
	ret = host_donate_hyp(shadow_cmd_pa >> PAGE_SHIFT, num_pages);
	if (ret)
		return ret;

	its->shadow_cmd = hyp_phys_to_virt(shadow_cmd_pa);
	pfn = host_cmd_base_pa >> PAGE_SHIFT;
	for (i = 0; i < num_pages; i++) {
		ret = host_share_hyp(pfn);
		if (ret)
			goto remove_donation;

		pfn++;
	}

	its->host_cmd_base_va = hyp_phys_to_virt(host_cmd_base_pa);
	its->host_cmd_cwriter_va = its->host_cmd_base_va;

	ret = hyp_pin_shared_mem(its->host_cmd_base_va,
				 its->host_cmd_base_va + ITS_CMD_QUEUE_SZ);
	if (ret)
		goto remove_donation;

	return 0;
remove_donation:
	hyp_donate_host(shadow_cmd_pa >> PAGE_SHIFT, num_pages);
	for (i = i - 1; i >= 0; i--)
		host_unshare_hyp(pfn--);
	return ret;
}

DEFINE_MEM_TRACKER(tracker_devtab, &region_tracker_donated_ops);

static int table_emulate_handler(struct emulate *emulate, u64 offset,
				 bool write, u64 *reg, int reg_size)
{
	struct hyp_gic_v3_its_baser *baser = emulate->priv;
	struct hyp_gic_v3_its *its;
	phys_addr_t addr, len;
	int ret;

	/* In a flat configuration the table is only populated with commands */
	if (!(baser->value & GITS_BASER_INDIRECT))
		return -EFAULT;

	if (reg_size != sizeof(u64))
		return -EINVAL;

	if (!write) {
		*reg = readq_relaxed(baser->table + offset);
		return 0;
	}

	/* We can only protect in page granularity - make sure the driver
	 * allocates the second level page aligned.
	 */
	if (!PAGE_ALIGNED(*reg))
		return -EINVAL;

	addr = *reg & GENMASK_ULL(52, PAGE_SHIFT);
	its = container_of(baser, struct hyp_gic_v3_its, basers[baser->baser_n]);
	len = (baser->value & (3 << GITS_BASER_PAGE_SIZE_SHIFT)) >> GITS_BASER_PAGE_SIZE_SHIFT;
	/* The size of the level 2 table is determined by GITS_BASER<n>.Page_Size */
	len = SZ_4K << len;

	if (*reg & GITS_BASER_VALID)
		ret = region_tracker_inc(&tracker_devtab, addr, addr + len);
	else
		ret = region_tracker_dec(&tracker_devtab, addr, addr + len);

	if (!ret)
		writeq_relaxed(*reg, baser->table + offset);

	return ret;
}

static int setup_first_lvl_table_traps(struct hyp_gic_v3_its *its, int baser_n, u64 baser_val)
{
	struct hyp_gic_v3_its_baser *baser;
	int ret;
	u64 len, page_sz;
	phys_addr_t paddr;

	paddr = GITS_BASER_ADDR_48_to_52(baser_val);
	page_sz = (baser_val & (3 << GITS_BASER_PAGE_SIZE_SHIFT)) >> GITS_BASER_PAGE_SIZE_SHIFT;
	len = GITS_BASER_NR_PAGES(baser_val) * (SZ_4K << page_sz);

	baser = &its->basers[baser_n];
	baser->baser_n = baser_n;
	baser->emulate.base = paddr;
	baser->emulate.size = len;
	baser->value = baser_val;
	baser->emulate.handler = table_emulate_handler;
	baser->emulate.priv = baser;

	ret = create_private_mapping(paddr, len, KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE,
				     (unsigned long *)&baser->table);
	if (ret)
		return ret;

	return hyp_add_emulate(&baser->emulate);
}

static int hyp_gic_v3_its_init_baser_traps(struct hyp_gic_v3_its *its)
{
	int i;
	int ret;
	u64 baser_val;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		baser_val = readq_relaxed(its->base + GITS_BASER + (i << 3));

		if (!(baser_val & GITS_BASER_VALID))
			continue;

		switch (GITS_BASER_TYPE(baser_val)) {
		case GITS_BASER_TYPE_DEVICE:
		case GITS_BASER_TYPE_VCPU:
		case GITS_BASER_TYPE_COLLECTION:
			break;
		default:
			return -EINVAL;
		}

		ret = setup_first_lvl_table_traps(its, i, baser_val);
		if (ret)
			return ret;
	}

	return 0;
}

static int hyp_gic_v3_its_protect(u64 paddr, u64 size, u64 host_cmd_base_pa)
{
	struct hyp_gic_v3_its *its;
	int ret;

	its = &its_devs[its_dev_count];
	its_dev_count++;

	ret = create_private_mapping(
		paddr, size, KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE,
		(unsigned long *)&its->base);
	if (ret)
		return ret;

	its->emulate.base = paddr;
	its->emulate.size = size;
	its->emulate.handler = its_emulate_handler;
	its->emulate.priv = its;
	its->typer = gic_read_typer(its->base + GITS_TYPER);

	ret = hyp_gic_v3_its_shadow_cmdq(its, host_cmd_base_pa);
	if (ret)
		return ret;

	ret = hyp_add_emulate(&its->emulate);
	if (ret)
		return ret;

	/* Allow DMA/IO access back to the GITS_TRANSLATER */
	ret = host_stage2_mod_prot(
		(paddr >> PAGE_SHIFT) + GITS_TRANSLATER_PFN,
		KVM_PGTABLE_PROT_RW | KVM_PGTABLE_PROT_DEVICE, 1, true);

	ret = hyp_gic_v3_its_init_baser_traps(its);
	if (ret)
		return ret;

	return ret;
}

void hyp_gic_v3_its_protect_hvc(struct user_pt_regs *regs)
{
	int ret;
	u64 paddr, size;
	u64 host_cmd_base_pa;

	/* regs->regs[0] is the HVC function ID */
	paddr = regs->regs[1];
	size = regs->regs[2];
	host_cmd_base_pa = regs->regs[3];

	hyp_spin_lock(&its_devs_lock);
	ret = hyp_gic_v3_its_protect(paddr, size, host_cmd_base_pa);
	hyp_spin_unlock(&its_devs_lock);

	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}
