// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/iopoll.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci_regs.h>
#include <linux/platform_device.h>

#include "../../pci.h"
#include "pcie-designware.h"

static struct pci_ops dw_pcie_ops;
static struct pci_ops dw_child_pcie_ops;

static void dw_msi_ack_irq(struct irq_data *d)
{
	irq_chip_ack_parent(d);
}

static void dw_msi_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void dw_msi_unmask_irq(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static struct irq_chip dw_pcie_msi_irq_chip = {
	.name = "PCI-MSI",
	.irq_ack = dw_msi_ack_irq,
	.irq_mask = dw_msi_mask_irq,
	.irq_unmask = dw_msi_unmask_irq,
};

static struct msi_domain_info dw_pcie_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX | MSI_FLAG_MULTI_PCI_MSI),
	.chip	= &dw_pcie_msi_irq_chip,
};

/*
 * Some chips are configured for edge-triggered interrupts,
 * but the underlying signal is level-based. So with multiple
 * msi interrupts come in back to back, there is possibility
 * that the interrupt signal didn't goes low to high to create
 * edge for each and every incoming msi to allow GIC layer to
 * detect. To avoid that, we have this workaround to set irqchip
 * state to PENDING if necessary.
 */
static int dw_pci_check_if_need_retrigger(int msi_ctrl, struct dw_pcie_rp *pp)
{
	int err = 0, virq;
	unsigned long flags;
	u32 status, mask, irq_type;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	raw_spin_lock_irqsave(&pp->lock, flags);
	virq = pp->msi_irq[msi_ctrl];
	irq_type = irq_get_trigger_type(virq);
	if (!(irq_type & IRQ_TYPE_EDGE_BOTH))
		goto unlock;
	status = dw_pcie_readl_dbi(pci, PCIE_MSI_INTR0_STATUS +
					   (msi_ctrl * MSI_REG_CTRL_BLOCK_SIZE));
	mask = pp->irq_mask[msi_ctrl];

	status &= ~mask;
	if (!status)
		goto unlock;
	err = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, true);
unlock:
	raw_spin_unlock_irqrestore(&pp->lock, flags);
	return err;
}

/* MSI int handler */
irqreturn_t dw_handle_msi_irq(struct dw_pcie_rp *pp)
{
	int i, pos, err;
	unsigned long val;
	u32 status, num_ctrls;
	irqreturn_t ret = IRQ_NONE;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	for (i = 0; i < num_ctrls; i++) {
		status = dw_pcie_readl_dbi(pci, PCIE_MSI_INTR0_STATUS +
					   (i * MSI_REG_CTRL_BLOCK_SIZE));
		if (!status)
			continue;

		ret = IRQ_HANDLED;
		val = status;
		pos = 0;
		while ((pos = find_next_bit(&val, MAX_MSI_IRQS_PER_CTRL,
					    pos)) != MAX_MSI_IRQS_PER_CTRL) {
			generic_handle_domain_irq(pp->irq_domain,
						  (i * MAX_MSI_IRQS_PER_CTRL) +
						  pos);
			pos++;
		}

		err = dw_pci_check_if_need_retrigger(i, pp);
		if (err)
			dev_err(pci->dev, "Failed to set irqchip state %d\n", err);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(dw_handle_msi_irq);

/* Chained MSI interrupt service routine */
static void dw_chained_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct dw_pcie_rp *pp;

	chained_irq_enter(chip, desc);

	pp = irq_desc_get_handler_data(desc);
	dw_handle_msi_irq(pp);

	chained_irq_exit(chip, desc);
}

static void dw_pci_setup_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target;

	msi_target = (u64)pp->msi_data;

	msg->address_lo = lower_32_bits(msi_target);
	msg->address_hi = upper_32_bits(msi_target);

	msg->data = d->hwirq;

	dev_dbg(pci->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)d->hwirq, msg->address_hi, msg->address_lo);
}

/*
 * The algo here honor if there is any intersection of mask of
 * the existing msi vectors and the requesting msi vector. So we
 * could handle both narrow (1 bit set mask) and wide (0xffff...)
 * cases, return -EINVAL and reject the request if the result of
 * cpumask is empty, otherwise return 0 and have the calculated
 * result on the mask_to_check to pass down to the irq_chip.
 */
static int dw_pci_check_mask_compatibility(struct dw_pcie_rp *pp,
					   unsigned long msi_irq_index,
					   unsigned long hwirq_to_check,
					   struct cpumask *mask_to_check)
{
	unsigned long end, hwirq;
	const struct cpumask *mask;
	unsigned int virq;

	hwirq = msi_irq_index * MAX_MSI_IRQS_PER_CTRL;
	end = hwirq + MAX_MSI_IRQS_PER_CTRL;
	for_each_set_bit_from(hwirq, pp->msi_irq_in_use, end) {
		if (hwirq == hwirq_to_check)
			continue;
		virq = irq_find_mapping(pp->irq_domain, hwirq);
		if (!virq)
			continue;
		mask = irq_get_affinity_mask(virq);
		if (!cpumask_and(mask_to_check, mask, mask_to_check))
			return -EINVAL;
	}

	return 0;
}

static void dw_pci_update_effective_affinity(struct dw_pcie_rp *pp,
					     unsigned long msi_irq_index,
					     const struct cpumask *effective_mask,
					     unsigned long hwirq_to_check)
{
	struct irq_desc *desc_downstream;
	unsigned int virq_downstream;
	unsigned long end, hwirq;

	/*
	 * update all the irq_data's effective mask
	 * bind to this msi controller, so the correct
	 * affinity would reflect on
	 * /proc/irq/XXX/effective_affinity
	 */
	hwirq = msi_irq_index * MAX_MSI_IRQS_PER_CTRL;
	end = hwirq + MAX_MSI_IRQS_PER_CTRL;
	for_each_set_bit_from(hwirq, pp->msi_irq_in_use, end) {
		virq_downstream = irq_find_mapping(pp->irq_domain, hwirq);
		if (!virq_downstream)
			continue;
		desc_downstream = irq_to_desc(virq_downstream);
		irq_data_update_effective_affinity(&desc_downstream->irq_data,
						   effective_mask);
	}
}

static int dw_pci_msi_set_affinity(struct irq_data *d,
				   const struct cpumask *mask, bool force)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;
	int virq_parent;
	unsigned long hwirq = d->hwirq;
	unsigned long flags, msi_irq_index;
	struct irq_desc *desc_parent;
	const struct cpumask *effective_mask;
	cpumask_var_t mask_result;

	/*
	 * The msi irq vectors are 32:1 aggregator to GIC SPI
	 * line. so divid the hwirq by 32 to find out GIC SPI
	 * line this msi vector map to.
	 */
	msi_irq_index = hwirq / MAX_MSI_IRQS_PER_CTRL;
	if (!alloc_cpumask_var(&mask_result, GFP_ATOMIC))
		return -ENOMEM;

	/*
	 * Loop through all possible msi vector to check if the
	 * request one is compatible with all of them
	 */
	raw_spin_lock_irqsave(&pp->lock, flags);
	cpumask_copy(mask_result, mask);
	ret = dw_pci_check_mask_compatibility(pp, msi_irq_index, hwirq, mask_result);
	if (ret) {
		dev_dbg(pci->dev, "Incompatible mask, request %*pbl, irq num %u\n",
			cpumask_pr_args(mask), d->irq);
		goto unlock;
	}

	dev_dbg(pci->dev, "Final mask, request %*pbl, irq num %u\n",
		cpumask_pr_args(mask_result), d->irq);

	virq_parent = pp->msi_irq[msi_irq_index];
	desc_parent = irq_to_desc(virq_parent);
	raw_spin_lock(&desc_parent->lock);
	ret = desc_parent->irq_data.chip->irq_set_affinity(&desc_parent->irq_data,
							   mask_result, force);

	if (ret < 0) {
		raw_spin_unlock(&desc_parent->lock);
		goto unlock;
	}

	switch (ret) {
	case IRQ_SET_MASK_OK:
	case IRQ_SET_MASK_OK_DONE:
		cpumask_copy(desc_parent->irq_common_data.affinity, mask);
		fallthrough;
	case IRQ_SET_MASK_OK_NOCOPY:
		break;
	}
	raw_spin_unlock(&desc_parent->lock);

	effective_mask = irq_data_get_effective_affinity_mask(&desc_parent->irq_data);
	dw_pci_update_effective_affinity(pp, msi_irq_index, effective_mask, hwirq);

unlock:
	free_cpumask_var(mask_result);
	raw_spin_unlock_irqrestore(&pp->lock, flags);
	return ret < 0 ? ret : IRQ_SET_MASK_OK_NOCOPY;
}

static void dw_pci_bottom_mask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] |= BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_unmask(struct irq_data *d)
{
	struct dw_pcie_rp *pp = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	pp->irq_mask[ctrl] &= ~BIT(bit);
	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK + res, pp->irq_mask[ctrl]);

	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static void dw_pci_bottom_ack(struct irq_data *d)
{
	struct dw_pcie_rp *pp  = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	unsigned int res, bit, ctrl;

	ctrl = d->hwirq / MAX_MSI_IRQS_PER_CTRL;
	res = ctrl * MSI_REG_CTRL_BLOCK_SIZE;
	bit = d->hwirq % MAX_MSI_IRQS_PER_CTRL;

	dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_STATUS + res, BIT(bit));
}

static struct irq_chip dw_pci_msi_bottom_irq_chip = {
	.name = "DWPCI-MSI",
	.irq_ack = dw_pci_bottom_ack,
	.irq_compose_msi_msg = dw_pci_setup_msi_msg,
	.irq_set_affinity = dw_pci_msi_set_affinity,
	.irq_mask = dw_pci_bottom_mask,
	.irq_unmask = dw_pci_bottom_unmask,
};

static int dw_pcie_irq_domain_alloc(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs,
				    void *args)
{
	struct dw_pcie_rp *pp = domain->host_data;
	const struct cpumask *mask;
	unsigned long flags, index, start, size;
	int irq, ctrl, p_irq, *msi_vec_index;
	unsigned int controller_count = (pp->num_vectors / MAX_MSI_IRQS_PER_CTRL);

	/*
	 * All IRQs on a given controller will use the same parent interrupt,
	 * and therefore the same CPU affinity. We try to honor any CPU spreading
	 * requests by assigning distinct affinity masks to distinct vectors.
	 * So instead of always allocating the msi vectors in a contiguous fashion,
	 * the algo here honor whoever comes first can bind the msi controller to
	 * its irq affinity mask, or compare its cpumask against
	 * currently recorded to decide if binding to this msi controller.
	 */

	msi_vec_index = kcalloc(nr_irqs, sizeof(*msi_vec_index), GFP_KERNEL);
	if (!msi_vec_index)
		return -ENOMEM;

	raw_spin_lock_irqsave(&pp->lock, flags);

	for (irq = 0; irq < nr_irqs; irq++) {
		mask = irq_get_affinity_mask(virq + irq);
		for (ctrl = 0; ctrl < controller_count; ctrl++) {
			start = ctrl * MAX_MSI_IRQS_PER_CTRL;
			size = start + MAX_MSI_IRQS_PER_CTRL;
			if (find_next_bit(pp->msi_irq_in_use, size, start) >= size) {
				cpumask_copy(&pp->msi_ctrl_to_cpu[ctrl], mask);
				break;
			}

			if (cpumask_equal(&pp->msi_ctrl_to_cpu[ctrl], mask) &&
			    find_next_zero_bit(pp->msi_irq_in_use, size, start) < size)
				break;
		}

		/*
		 * no msi controller matches, we would error return (no space) and
		 * clear those previously allocated bit, because all those msi vectors
		 * didn't really successfully allocated, so we shouldn't occupied that
		 * position in the bitmap in case other endpoint may still make use of
		 * those. An extra step when choosing to not allocate in contiguous
		 * fashion.
		 */
		if (ctrl == controller_count) {
			for (p_irq = irq - 1; p_irq >= 0; p_irq--)
				bitmap_clear(pp->msi_irq_in_use, msi_vec_index[p_irq], 1);
			raw_spin_unlock_irqrestore(&pp->lock, flags);
			kfree(msi_vec_index);
			return -ENOSPC;
		}

		index = bitmap_find_next_zero_area(pp->msi_irq_in_use,
						   size,
						   start,
						   1,
						   0);
		bitmap_set(pp->msi_irq_in_use, index, 1);
		msi_vec_index[irq] = index;
	}

	raw_spin_unlock_irqrestore(&pp->lock, flags);

	for (irq = 0; irq < nr_irqs; irq++)
		irq_domain_set_info(domain, virq + irq, msi_vec_index[irq],
				    pp->msi_irq_chip,
				    pp, handle_edge_irq,
				    NULL, NULL);
	kfree(msi_vec_index);

	return 0;
}

static void dw_pcie_irq_domain_free(struct irq_domain *domain,
				    unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d;
	struct dw_pcie_rp *pp = domain->host_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&pp->lock, flags);
	for (int i = 0; i < nr_irqs; i++) {
		d = irq_domain_get_irq_data(domain, virq + i);
		bitmap_clear(pp->msi_irq_in_use, d->hwirq, 1);
	}
	raw_spin_unlock_irqrestore(&pp->lock, flags);
}

static const struct irq_domain_ops dw_pcie_msi_domain_ops = {
	.alloc	= dw_pcie_irq_domain_alloc,
	.free	= dw_pcie_irq_domain_free,
};

int dw_pcie_allocate_domains(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct fwnode_handle *fwnode = of_node_to_fwnode(pci->dev->of_node);

	pp->irq_domain = irq_domain_create_linear(fwnode, pp->num_vectors,
					       &dw_pcie_msi_domain_ops, pp);
	if (!pp->irq_domain) {
		dev_err(pci->dev, "Failed to create IRQ domain\n");
		return -ENOMEM;
	}

	irq_domain_update_bus_token(pp->irq_domain, DOMAIN_BUS_NEXUS);

	pp->msi_domain = pci_msi_create_irq_domain(fwnode,
						   &dw_pcie_msi_domain_info,
						   pp->irq_domain);
	if (!pp->msi_domain) {
		dev_err(pci->dev, "Failed to create MSI domain\n");
		irq_domain_remove(pp->irq_domain);
		return -ENOMEM;
	}

	return 0;
}

static void dw_pcie_free_msi(struct dw_pcie_rp *pp)
{
	u32 ctrl;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl],
							 NULL, NULL);
	}

	irq_domain_remove(pp->msi_domain);
	irq_domain_remove(pp->irq_domain);
}

static void dw_pcie_msi_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u64 msi_target = (u64)pp->msi_data;

	if (!pci_msi_enabled() || !pp->has_msi_ctrl)
		return;

	/* Program the msi_data */
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_LO, lower_32_bits(msi_target));
	dw_pcie_writel_dbi(pci, PCIE_MSI_ADDR_HI, upper_32_bits(msi_target));
}

static int dw_pcie_parse_split_msi_irq(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u32 ctrl, max_vectors;
	int irq;

	/* Parse any "msiX" IRQs described in the devicetree */
	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++) {
		char msi_name[] = "msiX";

		msi_name[3] = '0' + ctrl;
		irq = platform_get_irq_byname_optional(pdev, msi_name);
		if (irq == -ENXIO)
			break;
		if (irq < 0)
			return dev_err_probe(dev, irq,
					     "Failed to parse MSI IRQ '%s'\n",
					     msi_name);

		pp->msi_irq[ctrl] = irq;
	}

	/* If no "msiX" IRQs, caller should fallback to "msi" IRQ */
	if (ctrl == 0)
		return -ENXIO;

	max_vectors = ctrl * MAX_MSI_IRQS_PER_CTRL;
	if (pp->num_vectors > max_vectors) {
		dev_warn(dev, "Exceeding number of MSI vectors, limiting to %u\n",
			 max_vectors);
		pp->num_vectors = max_vectors;
	}
	if (!pp->num_vectors)
		pp->num_vectors = max_vectors;

	return 0;
}

static int dw_pcie_msi_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);
	u64 *msi_vaddr = NULL;
	int ret;
	u32 ctrl, num_ctrls;

	for (ctrl = 0; ctrl < MAX_MSI_CTRLS; ctrl++)
		pp->irq_mask[ctrl] = ~0;

	if (!pp->msi_irq[0]) {
		ret = dw_pcie_parse_split_msi_irq(pp);
		if (ret < 0 && ret != -ENXIO)
			return ret;
	}

	if (!pp->num_vectors)
		pp->num_vectors = MSI_DEF_NUM_VECTORS;
	num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

	if (!pp->msi_irq[0]) {
		pp->msi_irq[0] = platform_get_irq_byname_optional(pdev, "msi");
		if (pp->msi_irq[0] < 0) {
			pp->msi_irq[0] = platform_get_irq(pdev, 0);
			if (pp->msi_irq[0] < 0)
				return pp->msi_irq[0];
		}
	}

	dev_dbg(dev, "Using %d MSI vectors\n", pp->num_vectors);

	pp->msi_irq_chip = &dw_pci_msi_bottom_irq_chip;

	ret = dw_pcie_allocate_domains(pp);
	if (ret)
		return ret;

	for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
		if (pp->msi_irq[ctrl] > 0)
			irq_set_chained_handler_and_data(pp->msi_irq[ctrl],
						    dw_chained_msi_isr, pp);
	}

	/*
	 * Even though the iMSI-RX Module supports 64-bit addresses some
	 * peripheral PCIe devices may lack 64-bit message support. In
	 * order not to miss MSI TLPs from those devices the MSI target
	 * address has to be within the lowest 4GB.
	 *
	 * Note until there is a better alternative found the reservation is
	 * done by allocating from the artificially limited DMA-coherent
	 * memory.
	 */
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (!ret)
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);

	if (!msi_vaddr) {
		dev_warn(dev, "Failed to allocate 32-bit MSI address\n");
		dma_set_coherent_mask(dev, DMA_BIT_MASK(64));
		msi_vaddr = dmam_alloc_coherent(dev, sizeof(u64), &pp->msi_data,
						GFP_KERNEL);
		if (!msi_vaddr) {
			dev_err(dev, "Failed to allocate MSI address\n");
			dw_pcie_free_msi(pp);
			return -ENOMEM;
		}
	}

	return 0;
}

int dw_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource_entry *win;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int ret;

	raw_spin_lock_init(&pp->lock);

	ret = dw_pcie_get_resources(pci);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (res) {
		pp->cfg0_size = resource_size(res);
		pp->cfg0_base = res->start;

		pp->va_cfg0_base = devm_pci_remap_cfg_resource(dev, res);
		if (IS_ERR(pp->va_cfg0_base))
			return PTR_ERR(pp->va_cfg0_base);
	} else {
		dev_err(dev, "Missing *config* reg space\n");
		return -ENODEV;
	}

	bridge = devm_pci_alloc_host_bridge(dev, 0);
	if (!bridge)
		return -ENOMEM;

	pp->bridge = bridge;

	/* Get the I/O range from DT */
	win = resource_list_first_type(&bridge->windows, IORESOURCE_IO);
	if (win) {
		pp->io_size = resource_size(win->res);
		pp->io_bus_addr = win->res->start - win->offset;
		pp->io_base = pci_pio_to_address(win->res->start);
	}

	/* Set default bus ops */
	bridge->ops = &dw_pcie_ops;
	bridge->child_ops = &dw_child_pcie_ops;

	if (pp->ops->host_init) {
		ret = pp->ops->host_init(pp);
		if (ret)
			return ret;
	}

	if (pci_msi_enabled()) {
		pp->has_msi_ctrl = !(pp->ops->msi_host_init ||
				     of_property_read_bool(np, "msi-parent") ||
				     of_property_read_bool(np, "msi-map"));

		/*
		 * For the has_msi_ctrl case the default assignment is handled
		 * in the dw_pcie_msi_host_init().
		 */
		if (!pp->has_msi_ctrl && !pp->num_vectors) {
			pp->num_vectors = MSI_DEF_NUM_VECTORS;
		} else if (pp->num_vectors > MAX_MSI_IRQS) {
			dev_err(dev, "Invalid number of vectors\n");
			ret = -EINVAL;
			goto err_deinit_host;
		}

		if (pp->ops->msi_host_init) {
			ret = pp->ops->msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		} else if (pp->has_msi_ctrl) {
			ret = dw_pcie_msi_host_init(pp);
			if (ret < 0)
				goto err_deinit_host;
		}
	}

	dw_pcie_version_detect(pci);

	dw_pcie_iatu_detect(pci);

	ret = dw_pcie_edma_detect(pci);
	if (ret)
		goto err_free_msi;

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		goto err_remove_edma;

	if (!dw_pcie_link_up(pci)) {
		ret = dw_pcie_start_link(pci);
		if (ret)
			goto err_remove_edma;
	}

	/* Ignore errors, the link may come up later */
	dw_pcie_wait_for_link(pci);

	bridge->sysdata = pp;

	ret = pci_host_probe(bridge);
	if (ret)
		goto err_stop_link;

	return 0;

err_stop_link:
	dw_pcie_stop_link(pci);

err_remove_edma:
	dw_pcie_edma_remove(pci);

err_free_msi:
	if (pp->has_msi_ctrl)
		dw_pcie_free_msi(pp);

err_deinit_host:
	if (pp->ops->host_deinit)
		pp->ops->host_deinit(pp);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_host_init);

void dw_pcie_host_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	pci_stop_root_bus(pp->bridge->bus);
	pci_remove_root_bus(pp->bridge->bus);

	dw_pcie_stop_link(pci);

	dw_pcie_edma_remove(pci);

	if (pp->has_msi_ctrl)
		dw_pcie_free_msi(pp);

	if (pp->ops->host_deinit)
		pp->ops->host_deinit(pp);
}
EXPORT_SYMBOL_GPL(dw_pcie_host_deinit);

static void __iomem *dw_pcie_other_conf_map_bus(struct pci_bus *bus,
						unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int type, ret;
	u32 busdev;

	/*
	 * Checking whether the link is up here is a last line of defense
	 * against platforms that forward errors on the system bus as
	 * SError upon PCI configuration transactions issued when the link
	 * is down. This check is racy by definition and does not stop
	 * the system from triggering an SError if the link goes down
	 * after this check is performed.
	 */
	if (!dw_pcie_link_up(pci))
		return NULL;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	ret = dw_pcie_prog_outbound_atu(pci, 0, type, pp->cfg0_base, busdev,
					pp->cfg0_size);
	if (ret)
		return NULL;

	return pp->va_cfg0_base + where;
}

static int dw_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;

	ret = pci_generic_config_read(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int dw_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	int ret;

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (pp->cfg0_io_shared) {
		ret = dw_pcie_prog_outbound_atu(pci, 0, PCIE_ATU_TYPE_IO,
						pp->io_base, pp->io_bus_addr,
						pp->io_size);
		if (ret)
			return PCIBIOS_SET_FAILED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dw_child_pcie_ops = {
	.map_bus = dw_pcie_other_conf_map_bus,
	.read = dw_pcie_rd_other_conf,
	.write = dw_pcie_wr_other_conf,
};

void __iomem *dw_pcie_own_conf_map_bus(struct pci_bus *bus, unsigned int devfn, int where)
{
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	if (PCI_SLOT(devfn) > 0)
		return NULL;

	return pci->dbi_base + where;
}
EXPORT_SYMBOL_GPL(dw_pcie_own_conf_map_bus);

static struct pci_ops dw_pcie_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static int dw_pcie_iatu_setup(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct resource_entry *entry;
	int i, ret;

	/* Note the very first outbound ATU is used for CFG IOs */
	if (!pci->num_ob_windows) {
		dev_err(pci->dev, "No outbound iATU found\n");
		return -EINVAL;
	}

	/*
	 * Ensure all out/inbound windows are disabled before proceeding with
	 * the MEM/IO (dma-)ranges setups.
	 */
	for (i = 0; i < pci->num_ob_windows; i++)
		dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_OB, i);

	for (i = 0; i < pci->num_ib_windows; i++)
		dw_pcie_disable_atu(pci, PCIE_ATU_REGION_DIR_IB, i);

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->windows) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pci->num_ob_windows <= ++i)
			break;

		ret = dw_pcie_prog_outbound_atu(pci, i, PCIE_ATU_TYPE_MEM,
						entry->res->start,
						entry->res->start - entry->offset,
						resource_size(entry->res));
		if (ret) {
			dev_err(pci->dev, "Failed to set MEM range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pp->io_size) {
		if (pci->num_ob_windows > ++i) {
			ret = dw_pcie_prog_outbound_atu(pci, i, PCIE_ATU_TYPE_IO,
							pp->io_base,
							pp->io_bus_addr,
							pp->io_size);
			if (ret) {
				dev_err(pci->dev, "Failed to set IO range %pr\n",
					entry->res);
				return ret;
			}
		} else {
			pp->cfg0_io_shared = true;
		}
	}

	if (pci->num_ob_windows <= i)
		dev_warn(pci->dev, "Ranges exceed outbound iATU size (%d)\n",
			 pci->num_ob_windows);

	i = 0;
	resource_list_for_each_entry(entry, &pp->bridge->dma_ranges) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;

		if (pci->num_ib_windows <= i)
			break;

		ret = dw_pcie_prog_inbound_atu(pci, i++, PCIE_ATU_TYPE_MEM,
					       entry->res->start,
					       entry->res->start - entry->offset,
					       resource_size(entry->res));
		if (ret) {
			dev_err(pci->dev, "Failed to set DMA range %pr\n",
				entry->res);
			return ret;
		}
	}

	if (pci->num_ib_windows <= i)
		dev_warn(pci->dev, "Dma-ranges exceed inbound iATU size (%u)\n",
			 pci->num_ib_windows);

	return 0;
}

int dw_pcie_setup_rc(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u32 val, ctrl, num_ctrls;
	int ret;

	/*
	 * Enable DBI read-only registers for writing/updating configuration.
	 * Write permission gets disabled towards the end of this function.
	 */
	dw_pcie_dbi_ro_wr_en(pci);

	dw_pcie_setup(pci);

	if (pp->has_msi_ctrl) {
		num_ctrls = pp->num_vectors / MAX_MSI_IRQS_PER_CTRL;

		/* Initialize IRQ Status array */
		for (ctrl = 0; ctrl < num_ctrls; ctrl++) {
			dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_MASK +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    pp->irq_mask[ctrl]);
			dw_pcie_writel_dbi(pci, PCIE_MSI_INTR0_ENABLE +
					    (ctrl * MSI_REG_CTRL_BLOCK_SIZE),
					    ~0);
		}
	}

	dw_pcie_msi_init(pp);

	/* Setup RC BARs */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000004);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0x00000000);

	/* Setup interrupt pins */
	val = dw_pcie_readl_dbi(pci, PCI_INTERRUPT_LINE);
	val &= 0xffff00ff;
	val |= 0x00000100;
	dw_pcie_writel_dbi(pci, PCI_INTERRUPT_LINE, val);

	/* Setup bus numbers */
	val = dw_pcie_readl_dbi(pci, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00ff0100;
	dw_pcie_writel_dbi(pci, PCI_PRIMARY_BUS, val);

	/* Setup command register */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	/*
	 * If the platform provides its own child bus config accesses, it means
	 * the platform uses its own address translation component rather than
	 * ATU, so we should not program the ATU here.
	 */
	if (pp->bridge->child_ops == &dw_child_pcie_ops) {
		ret = dw_pcie_iatu_setup(pp);
		if (ret)
			return ret;
	}

	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0);

	/* Program correct class for RC */
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_PCI);

	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_setup_rc);

int dw_pcie_suspend_noirq(struct dw_pcie *pci)
{
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;
	int ret;

	/*
	 * If L1SS is supported, then do not put the link into L2 as some
	 * devices such as NVMe expect low resume latency.
	 */
	if (dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKCTL) & PCI_EXP_LNKCTL_ASPM_L1)
		return 0;

	if (dw_pcie_get_ltssm(pci) <= DW_PCIE_LTSSM_DETECT_ACT)
		return 0;

	if (!pci->pp.ops->pme_turn_off)
		return 0;

	pci->pp.ops->pme_turn_off(&pci->pp);

	ret = read_poll_timeout(dw_pcie_get_ltssm, val, val == DW_PCIE_LTSSM_L2_IDLE,
				PCIE_PME_TO_L2_TIMEOUT_US/10,
				PCIE_PME_TO_L2_TIMEOUT_US, false, pci);
	if (ret) {
		dev_err(pci->dev, "Timeout waiting for L2 entry! LTSSM: 0x%x\n", val);
		return ret;
	}

	if (pci->pp.ops->host_deinit)
		pci->pp.ops->host_deinit(&pci->pp);

	pci->suspended = true;

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_suspend_noirq);

int dw_pcie_resume_noirq(struct dw_pcie *pci)
{
	int ret;

	if (!pci->suspended)
		return 0;

	pci->suspended = false;

	if (pci->pp.ops->host_init) {
		ret = pci->pp.ops->host_init(&pci->pp);
		if (ret) {
			dev_err(pci->dev, "Host init failed: %d\n", ret);
			return ret;
		}
	}

	dw_pcie_setup_rc(&pci->pp);

	ret = dw_pcie_start_link(pci);
	if (ret)
		return ret;

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		return ret;

	return ret;
}
EXPORT_SYMBOL_GPL(dw_pcie_resume_noirq);
