// SPDX-License-Identifier: GPL-2.0-only

#include <asm/kvm_pkvm.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <nvhe/its_emulate.h>
#include <nvhe/mem_protect.h>

struct emu_handler {
	u64 offset;
	u8 access_size;
	void (*write)(struct pkvm_moveable_reg *region, u64 offset, u64 value);
	void (*read)(struct pkvm_moveable_reg *region, u64 offset, u64 *read);
};

#define EMU_HANDLER(off, sz, write_cb, read_cb)	\
{							\
	.offset = (off),				\
	.access_size = (sz),				\
	.write = (write_cb),				\
	.read = (read_cb),				\
}

static void handle_emulation(struct pkvm_moveable_reg *region, u64 offset,
			     bool write, u64 *reg, u8 reg_size,
			     struct emu_handler *handlers, hyp_spinlock_t *lock)
{
	struct emu_handler *reg_handler;

	for (reg_handler = handlers; reg_handler->access_size; reg_handler++) {
		if (reg_handler->offset > offset ||
		    reg_handler->offset + reg_handler->access_size <= offset)
			continue;

		if (reg_handler->access_size < reg_size)
			return;

		if (write && reg_handler->write) {
			hyp_spin_lock(lock);
			reg_handler->write(region, offset, *reg);
			hyp_spin_unlock(lock);
			return;
		}

		if (!write && reg_handler->read) {
			hyp_spin_lock(lock);
			reg_handler->read(region, offset, reg);
			hyp_spin_unlock(lock);
			return;
		}

		return;
	}

	pkvm_handle_forward_req(region, offset, write, reg, reg_size);
}

static int donate_and_clear_host_page(void *addr, size_t num_pages)
{
	int ret;

	if (!PAGE_ALIGNED(addr))
		return -EINVAL;

	ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(addr), num_pages);
	if (ret)
		return ret;

	memset(addr, 0, num_pages << PAGE_SHIFT);
	return 0;
}

struct its_priv_state {
	void *base;
	void *cmd_hyp_base;
	void *cmd_host_base;
	u64 cmd_offset;
	struct its_shadow_tables *shadow;
	hyp_spinlock_t its_lock;
	bool needs_flush;
};

DEFINE_HYP_SPINLOCK(its_setup_lock);

#define GITS_CWRITER_RETRY	BIT_ULL(0)
#define GITS_CWRITER_OFFSET	GENMASK_ULL(19, 5)

#define GITS_CREADR_STALLED	BIT_ULL(0)
#define GITS_CREADR_OFFSET	GENMASK_ULL(19, 5)

static int submit_single_cmd(struct its_priv_state *its, bool retry)
{
	size_t cmdq_sz = its->shadow->cmdq_len;
	u64 timeout = 1000;
	u64 offset, cwriter, creadr;

	offset = (its->cmd_offset + sizeof(struct its_cmd_block)) % cmdq_sz;

	cwriter = offset & GITS_CWRITER_OFFSET;
	cwriter |= FIELD_PREP(GITS_CWRITER_RETRY, retry);
	writeq_relaxed(cwriter, its->base + GITS_CWRITER);

	while (its->cmd_offset != offset) {
		creadr = readq_relaxed(its->base + GITS_CREADR);

		/* Command failed. */
		if (FIELD_GET(GITS_CREADR_STALLED, creadr))
			return -EIO;

		its->cmd_offset = creadr & GITS_CREADR_OFFSET;
		if (its->cmd_offset == offset)
			return 0;

		/*
		 * We can't spin here forever and we can't roll back
		 * the cmd queue pointer. Let's revert the cmd effects in the
		 * emulation layer and then go back to the driver to let it
		 * decide what to do next.
		 */
		if (!timeout--)
			return -EBUSY;

		pkvm_udelay(100);
	}

	return 0;
}

static int process_cmd(struct its_priv_state *its, struct its_cmd_block *cmd,
		       bool rollback)
{
	/* Passthrough everything for now */
	return 0;
}

static void cwriter_write(struct pkvm_moveable_reg *region, u64 offset, u64 value)
{
	struct its_priv_state *its = region->priv;
	struct its_cmd_block cmd, raw;
	u64 new_offset;
	bool retry;
	int i;

	new_offset = value & GITS_CWRITER_OFFSET;
	if (new_offset >= its->shadow->cmdq_len)
		return;

	retry = FIELD_GET(GITS_CWRITER_RETRY, value);
	while (its->cmd_offset != new_offset) {
		memcpy(&raw, its->cmd_host_base + its->cmd_offset, sizeof(raw));

		for (i = 0; i < ARRAY_SIZE(cmd.raw_cmd); i++)
			cmd.raw_cmd[i] = le64_to_cpu(raw.raw_cmd_le[i]);

		if (process_cmd(its, &cmd, /* rollback */ false))
			return;

		memcpy(its->cmd_hyp_base + its->cmd_offset, &raw, sizeof(struct its_cmd_block));

		if (its->needs_flush)
			gic_flush_dcache_to_poc(its->cmd_hyp_base + its->cmd_offset, sizeof(cmd));
		else
			dsb(ishst);

		if (submit_single_cmd(its, retry)) {
			WARN_ON(process_cmd(its, &cmd, /* rollback */ true));
			return;
		}
	}
}

static void cwriter_read(struct pkvm_moveable_reg *region, u64 offset, u64 *read)
{
	struct its_priv_state *its = region->priv;
	*read = readq_relaxed(its->base + GITS_CWRITER);
}

static struct emu_handler its_handlers[] = {
	EMU_HANDLER(GITS_CWRITER, sizeof(u64), cwriter_write, cwriter_read),
	{},
};

void pkvm_handle_forward_req(struct pkvm_moveable_reg *region, u64 offset, bool write,
			     u64 *reg, u8 reg_size)
{
	void __iomem *addr = __hyp_va(region->start + offset);

	if (reg_size == sizeof(u32)) {
		if (!write)
			*reg = readl_relaxed(addr);
		else
			writel_relaxed(*reg, addr);
	} else if (reg_size == sizeof(u64)) {
		if (!write)
			*reg = readq_relaxed(addr);
		else
			writeq_relaxed(*reg, addr);
	}
}

void pkvm_handle_gic_emulation(struct pkvm_moveable_reg *region, u64 offset, bool write,
			       u64 *reg, u8 reg_size)
{
	struct its_priv_state *its_priv = region->priv;

	if (!its_priv)
		return;

	if (!IS_ALIGNED(offset, reg_size))
		return;

	handle_emulation(region, offset, write, reg, reg_size, its_handlers,
			 &its_priv->its_lock);
}

static struct pkvm_moveable_reg *get_region(phys_addr_t dev_addr)
{
	int i;

	for (i = 0; i < pkvm_moveable_regs_nr; i++) {
		if (pkvm_moveable_regs[i].start == dev_addr)
			return &pkvm_moveable_regs[i];
	}

	return NULL;
}

static int pkvm_host_unmap_last_level(void *shadow, size_t num_pages, u32 psz)
{
	u64 *table = shadow;
	int ret, i, end = (num_pages << PAGE_SHIFT) / sizeof(*table);
	phys_addr_t table_addr;

	for (i = 0; i < end; i++) {
		if (!(table[i] & GITS_BASER_VALID))
			continue;

		table_addr = table[i] & PHYS_MASK;
		ret = __pkvm_host_donate_hyp(hyp_phys_to_pfn(table_addr), psz >> PAGE_SHIFT);
		if (ret)
			goto err_donate;
	}

	return 0;
err_donate:
	for (i = i - 1; i >= 0; i--) {
		if (!(table[i] & GITS_BASER_VALID))
			continue;

		table_addr = table[i] & PHYS_MASK;
		__pkvm_hyp_donate_host(hyp_phys_to_pfn(table_addr), psz >> PAGE_SHIFT);
	}
	return ret;
}

static int pkvm_share_shadow_table(void *shadow, u64 nr_pages)
{
	u64 i, ret, start_pfn = hyp_virt_to_pfn(shadow);

	for (i = 0; i < nr_pages; i++) {
		ret = __pkvm_host_share_hyp(start_pfn + i);
		if (ret)
			goto unshare;
	}

	ret = hyp_pin_shared_mem(shadow, shadow + (nr_pages << PAGE_SHIFT));
	if (ret)
		goto unshare;

	return ret;
unshare:
	while (i--)
		__pkvm_host_unshare_hyp(start_pfn + i);
	return ret;
}

static void pkvm_unshare_shadow_table(void *shadow, u64 nr_pages)
{
	u64 i, start_pfn = hyp_virt_to_pfn(shadow);

	hyp_unpin_shared_mem(shadow, shadow + (nr_pages << PAGE_SHIFT));

	for (i = 0; i < nr_pages; i++)
		WARN_ON(__pkvm_host_unshare_hyp(start_pfn + i));
}

static void pkvm_host_map_last_level(void *shadow, size_t num_pages, u32 psz)
{
	u64 *table = shadow;
	int i, end = (num_pages << PAGE_SHIFT) / sizeof(*table);
	phys_addr_t table_addr;

	for (i = 0; i < end; i++) {
		if (!(table[i] & GITS_BASER_VALID))
			continue;

		table_addr = table[i] & PHYS_MASK;
		WARN_ON(__pkvm_hyp_donate_host(hyp_phys_to_pfn(table_addr), psz >> PAGE_SHIFT));
	}
}

static int pkvm_setup_its_shadow_baser(struct its_shadow_tables *shadow)
{
	int i, ret;
	u64 baser_val, num_pages, type;
	void *base, *host_base;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		baser_val = shadow->tables[i].val;
		if (!(baser_val & GITS_BASER_VALID))
			continue;

		base = kern_hyp_va(shadow->tables[i].base);
		num_pages = (1 << shadow->tables[i].order);

		ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(base), num_pages);
		if (ret)
			goto err_donate;

		if (baser_val & GITS_BASER_INDIRECT) {
			host_base = kern_hyp_va(shadow->tables[i].shadow);
			ret = pkvm_share_shadow_table(host_base, num_pages);
			if (ret)
				goto err_with_donation;

			type = GITS_BASER_TYPE(baser_val);
			if (type == GITS_BASER_TYPE_COLLECTION)
				continue;

			ret = pkvm_host_unmap_last_level(base, num_pages,
							 shadow->tables[i].psz);
			if (ret)
				goto err_with_share;
		}
	}

	return 0;
err_with_share:
	pkvm_unshare_shadow_table(host_base, num_pages);
err_with_donation:
	__pkvm_hyp_donate_host(hyp_virt_to_pfn(base), num_pages);
err_donate:
	for (i = i - 1; i >= 0; i--) {
		baser_val = shadow->tables[i].val;
		if (!(baser_val & GITS_BASER_VALID))
			continue;

		base = kern_hyp_va(shadow->tables[i].base);
		num_pages = (1 << shadow->tables[i].order);

		if (baser_val & GITS_BASER_INDIRECT) {
			host_base = kern_hyp_va(shadow->tables[i].shadow);
			pkvm_unshare_shadow_table(host_base, num_pages);

			type = GITS_BASER_TYPE(baser_val);
			if (type == GITS_BASER_TYPE_COLLECTION) {
				WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(base), num_pages));
				continue;
			}

			pkvm_host_map_last_level(base, num_pages, shadow->tables[i].psz);
		}

		WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(base), num_pages));
	}

	return ret;
}

static int pkvm_setup_its_shadow_cmdq(struct its_shadow_tables *shadow)
{
	int ret, i, num_pages;
	u64 shadow_start_pfn, original_start_pfn;
	void *cmd_shadow_va = kern_hyp_va(shadow->cmd_shadow);

	shadow_start_pfn = hyp_virt_to_pfn(cmd_shadow_va);
	original_start_pfn = hyp_virt_to_pfn(kern_hyp_va(shadow->cmd_original));
	num_pages = shadow->cmdq_len >> PAGE_SHIFT;

	for (i = 0; i < num_pages; i++) {
		ret = __pkvm_host_share_hyp(shadow_start_pfn + i);
		if (ret)
			goto unshare_shadow;
	}

	ret = hyp_pin_shared_mem(cmd_shadow_va, cmd_shadow_va + shadow->cmdq_len);
	if (ret)
		goto unshare_shadow;

	ret = __pkvm_host_donate_hyp(original_start_pfn, num_pages);
	if (ret)
		goto unpin_shadow;

	return ret;

unpin_shadow:
	hyp_unpin_shared_mem(cmd_shadow_va, cmd_shadow_va + shadow->cmdq_len);

unshare_shadow:
	for (i = i - 1; i >= 0; i--)
		__pkvm_host_unshare_hyp(shadow_start_pfn + i);

	return ret;
}

static void pkvm_teardown_its_shadow_cmdq(struct its_shadow_tables *shadow)
{
	u64 i, start_pfn, num_pages = shadow->cmdq_len >> PAGE_SHIFT;
	void *cmd_shadow_va = kern_hyp_va(shadow->cmd_shadow);
	void *cmd_original = kern_hyp_va(shadow->cmd_original);

	start_pfn = hyp_virt_to_pfn(cmd_shadow_va);
	hyp_unpin_shared_mem(cmd_shadow_va,
			     cmd_shadow_va + shadow->cmdq_len);

	for (i = 0; i < num_pages; i++)
		WARN_ON(__pkvm_host_unshare_hyp(start_pfn + i));

	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(cmd_original), num_pages));
}

int pkvm_init_gic_its_emulation(phys_addr_t dev_addr, void *host_priv_state, size_t priv_num_pages,
				struct its_shadow_tables *host_shadow)
{
	int ret;
	struct its_priv_state *priv_state = kern_hyp_va(host_priv_state);
	struct its_shadow_tables *shadow = kern_hyp_va(host_shadow);
	struct pkvm_moveable_reg *its_reg;

	if (!PAGE_ALIGNED(shadow) || !priv_num_pages)
		return -EINVAL;

	hyp_spin_lock(&its_setup_lock);
	its_reg = get_region(dev_addr);
	if (!its_reg) {
		ret = -ENODEV;
		goto err_unlock;
	}

	if (its_reg->priv || its_reg->type != PKVM_MREG_EMULATE_MMIO) {
		ret = -EOPNOTSUPP;
		goto err_unlock;
	}

	ret = donate_and_clear_host_page(priv_state, priv_num_pages);
	if (ret)
		goto err_unlock;

	ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(shadow), 1);
	if (ret)
		goto err_with_state;

	ret = pkvm_setup_its_shadow_cmdq(shadow);
	if (ret)
		goto err_with_shadow;

	ret = pkvm_setup_its_shadow_baser(shadow);
	if (ret)
		goto err_with_cmdq;

	hyp_spin_lock_init(&priv_state->its_lock);
	priv_state->shadow = shadow;
	priv_state->base = __hyp_va(dev_addr);

	priv_state->cmd_hyp_base = kern_hyp_va(shadow->cmd_original);
	priv_state->cmd_host_base = kern_hyp_va(shadow->cmd_shadow);
	priv_state->cmd_offset = readq_relaxed(priv_state->base + GITS_CREADR) &
		GITS_CREADR_OFFSET;
	priv_state->needs_flush =
		(readq_relaxed(priv_state->base + GITS_CBASER) & GITS_CBASER_SHAREABILITY_MASK) !=
		GITS_CBASER_InnerShareable;

	its_reg->priv = priv_state;

	hyp_spin_unlock(&its_setup_lock);

	return 0;
err_with_cmdq:
	pkvm_teardown_its_shadow_cmdq(shadow);
err_with_shadow:
	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(shadow), 1));
err_with_state:
	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(priv_state), priv_num_pages));
err_unlock:
	hyp_spin_unlock(&its_setup_lock);
	return ret;
}
