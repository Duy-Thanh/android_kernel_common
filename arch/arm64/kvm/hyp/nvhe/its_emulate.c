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

	if (!region->priv)
		return;

	for (reg_handler = handlers; reg_handler->access_size; reg_handler++) {
		if (reg_handler->offset > offset ||
		    reg_handler->offset + reg_handler->access_size <= offset)
			continue;

		if (reg_handler->access_size & (reg_size - 1))
			continue;

		if (write && reg_handler->write) {
			if (lock)
				hyp_spin_lock(lock);
			reg_handler->write(region, offset, *reg);
			if (lock)
				hyp_spin_unlock(lock);
			return;
		}

		if (!write && reg_handler->read) {
			if (lock)
				hyp_spin_lock(lock);
			reg_handler->read(region, offset, reg);
			if (lock)
				hyp_spin_unlock(lock);
			return;
		}

		return;
	}

	pkvm_handle_forward_req(region, offset, write, reg, reg_size);
}

struct its_priv_state {
	void *base;
	void *cmd_hyp_base;
	void *cmd_host_base;
	void *cmd_host_cwriter;
	struct its_shadow_tables *shadow;
	hyp_spinlock_t its_lock;
};

DEFINE_HYP_SPINLOCK(its_setup_lock);

static void cwriter_write(struct pkvm_moveable_reg *region, u64 offset, u64 value)
{
	struct its_priv_state *its = region->priv;
	u64 cwriter_offset = value & GENMASK(19, 5);
	int cmd_len, cmd_offset;
	size_t cmdq_sz = its->shadow->cmdq_len;

	if (cwriter_offset > cmdq_sz)
		return;

	cmd_offset = its->cmd_host_cwriter - its->cmd_host_base;
	cmd_len = cwriter_offset - cmd_offset;
	if (cmd_len < 0)
		cmd_len = cmdq_sz - cmd_offset;

	if (cmd_offset + cmd_len > cmdq_sz)
		return;

	memcpy(its->cmd_hyp_base + cmd_offset, its->cmd_host_cwriter, cmd_len);

	its->cmd_host_cwriter = its->cmd_host_base +
		(cmd_offset + cmd_len) % cmdq_sz;
	if (its->cmd_host_cwriter == its->cmd_host_base) {
		memcpy(its->cmd_hyp_base, its->cmd_host_base, cwriter_offset);

		its->cmd_host_cwriter = its->cmd_host_base + cwriter_offset;
	}

	writeq_relaxed(value, its->base + GITS_CWRITER);
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

	handle_emulation(region, offset, write, reg, reg_size, its_handlers,
			 &its_priv->its_lock);
}

static struct pkvm_moveable_reg *get_region(phys_addr_t dev_addr)
{
	int i;

	for (i = 0; i < PKVM_NR_MOVEABLE_REGS; i++) {
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

int pkvm_init_gic_its_emulation(phys_addr_t dev_addr, void *host_priv_state,
				struct its_shadow_tables *host_shadow)
{
	int ret;
	struct its_priv_state *priv_state = kern_hyp_va(host_priv_state);
	struct its_shadow_tables *shadow = kern_hyp_va(host_shadow);
	struct pkvm_moveable_reg *its_reg;

	hyp_spin_lock(&its_setup_lock);
	its_reg = get_region(dev_addr);
	if (!its_reg) {
		ret = -ENODEV;
		goto err_unlock;
	}

	if (its_reg->priv) {
		ret = -EOPNOTSUPP;
		goto err_unlock;
	}

	ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(priv_state), 1);
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
		goto err_with_shadow;

	its_reg->priv = priv_state;

	hyp_spin_lock_init(&priv_state->its_lock);
	priv_state->shadow = shadow;
	priv_state->base = __hyp_va(dev_addr);

	priv_state->cmd_hyp_base = kern_hyp_va(shadow->cmd_original);
	priv_state->cmd_host_base = kern_hyp_va(shadow->cmd_shadow);
	priv_state->cmd_host_cwriter =
		priv_state->cmd_host_base +
		(kern_hyp_va(shadow->cmd_write) - priv_state->cmd_hyp_base);

	hyp_spin_unlock(&its_setup_lock);

	return 0;
err_with_shadow:
	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(shadow), 1));
err_with_state:
	WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(priv_state), 1));
err_unlock:
	hyp_spin_unlock(&its_setup_lock);
	return ret;
}
