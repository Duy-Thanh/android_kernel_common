// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO platform devices interrupt handling
 *
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 */

#include <linux/eventfd.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/of.h>

#include "vfio_platform_private.h"

static void vfio_platform_mask(struct vfio_platform_irq *irq_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (!irq_ctx->masked) {
		disable_irq_nosync(irq_ctx->hwirq);
		irq_ctx->masked = true;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);
}

static int vfio_platform_mask_handler(void *opaque, void *unused)
{
	struct vfio_platform_irq *irq_ctx = opaque;

	vfio_platform_mask(irq_ctx);

	return 0;
}

static int vfio_platform_set_irq_mask(struct vfio_platform_device *vdev,
				      unsigned index, unsigned start,
				      unsigned count, uint32_t flags,
				      void *data)
{
	if (start != 0 || count != 1)
		return -EINVAL;

	if (!(vdev->irqs[index].flags & VFIO_IRQ_INFO_MASKABLE))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		if (fd >= 0)
			return vfio_virqfd_enable((void *) &vdev->irqs[index],
						  vfio_platform_mask_handler,
						  NULL, NULL,
						  &vdev->irqs[index].mask, fd);

		vfio_virqfd_disable(&vdev->irqs[index].mask);
		return 0;
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_platform_mask(&vdev->irqs[index]);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t mask = *(uint8_t *)data;

		if (mask)
			vfio_platform_mask(&vdev->irqs[index]);
	}

	return 0;
}

static void vfio_platform_unmask(struct vfio_platform_irq *irq_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (irq_ctx->masked) {
		enable_irq(irq_ctx->hwirq);
		irq_ctx->masked = false;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);
}

static int vfio_platform_unmask_handler(void *opaque, void *unused)
{
	struct vfio_platform_irq *irq_ctx = opaque;

	vfio_platform_unmask(irq_ctx);

	return 0;
}

static int vfio_platform_set_irq_unmask(struct vfio_platform_device *vdev,
					unsigned index, unsigned start,
					unsigned count, uint32_t flags,
					void *data)
{
	if (start != 0 || count != 1)
		return -EINVAL;

	if (!(vdev->irqs[index].flags & VFIO_IRQ_INFO_MASKABLE))
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		if (fd >= 0)
			return vfio_virqfd_enable((void *) &vdev->irqs[index],
						  vfio_platform_unmask_handler,
						  NULL, NULL,
						  &vdev->irqs[index].unmask,
						  fd);

		vfio_virqfd_disable(&vdev->irqs[index].unmask);
		return 0;
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		vfio_platform_unmask(&vdev->irqs[index]);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t unmask = *(uint8_t *)data;

		if (unmask)
			vfio_platform_unmask(&vdev->irqs[index]);
	}

	return 0;
}

/*
 * The trigger eventfd is guaranteed valid in the interrupt path
 * and protected by the igate mutex when triggered via ioctl.
 */
static void vfio_send_eventfd(struct vfio_platform_irq *irq_ctx)
{
	if (likely(irq_ctx->trigger))
		eventfd_signal(irq_ctx->trigger);
}

static irqreturn_t vfio_automasked_irq_handler(int irq, void *dev_id)
{
	struct vfio_platform_irq *irq_ctx = dev_id;
	unsigned long flags;
	int ret = IRQ_NONE;

	spin_lock_irqsave(&irq_ctx->lock, flags);

	if (!irq_ctx->masked) {
		ret = IRQ_HANDLED;

		/* automask maskable interrupts */
		disable_irq_nosync(irq_ctx->hwirq);
		irq_ctx->masked = true;
	}

	spin_unlock_irqrestore(&irq_ctx->lock, flags);

	if (ret == IRQ_HANDLED)
		vfio_send_eventfd(irq_ctx);

	return ret;
}

static irqreturn_t vfio_irq_handler(int irq, void *dev_id)
{
	struct vfio_platform_irq *irq_ctx = dev_id;

	vfio_send_eventfd(irq_ctx);

	return IRQ_HANDLED;
}

static int vfio_set_trigger(struct vfio_platform_device *vdev, int index,
			    int fd)
{
	struct vfio_platform_irq *irq = &vdev->irqs[index];
	struct eventfd_ctx *trigger;

	if (irq->trigger) {
		disable_irq(irq->hwirq);
		eventfd_ctx_put(irq->trigger);
		irq->trigger = NULL;
	}

	if (fd < 0) /* Disable only */
		return 0;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger))
		return PTR_ERR(trigger);

	irq->trigger = trigger;

	/*
	 * irq->masked effectively provides nested disables within the overall
	 * enable relative to trigger.  Specifically request_irq() is called
	 * with NO_AUTOEN, therefore the IRQ is initially disabled.  The user
	 * may only further disable the IRQ with a MASK operations because
	 * irq->masked is initially false.
	 */
	enable_irq(irq->hwirq);

	return 0;
}

static int vfio_platform_set_irq_trigger(struct vfio_platform_device *vdev,
					 unsigned index, unsigned start,
					 unsigned count, uint32_t flags,
					 void *data)
{
	struct vfio_platform_irq *irq = &vdev->irqs[index];
	irq_handler_t handler;

	if (vdev->irqs[index].flags & VFIO_IRQ_INFO_AUTOMASKED)
		handler = vfio_automasked_irq_handler;
	else
		handler = vfio_irq_handler;

	if (!count && (flags & VFIO_IRQ_SET_DATA_NONE))
		return vfio_set_trigger(vdev, index, -1);

	if (start != 0 || count != 1)
		return -EINVAL;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t fd = *(int32_t *)data;

		return vfio_set_trigger(vdev, index, fd);
	}

	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		handler(irq->hwirq, irq);

	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		uint8_t trigger = *(uint8_t *)data;

		if (trigger)
			handler(irq->hwirq, irq);
	}

	return 0;
}

static irqreturn_t vfio_platform_msihandler(int irq, void *arg)
{
	struct eventfd_ctx *trigger = arg;

	eventfd_signal(trigger);
	return IRQ_HANDLED;
}

static void vfio_platform_msi_disable(struct vfio_platform_device *vdev,
				      struct vfio_platform_irq *irq)
{
	int i;

	if (!irq->ctx)
		return;

	for (i = 0; i < irq->nr_ctx; i++) {
		if (!irq->ctx[i].trigger)
			continue;
		free_irq(irq->ctx[i].hwirq, irq->ctx[i].trigger);
		eventfd_ctx_put(irq->ctx[i].trigger);
		kfree(irq->ctx[i].name);
	}

	platform_device_msi_free_irqs_all(vdev->device);
	kfree(irq->ctx);
	irq->ctx = NULL;
	irq->nr_ctx = 0;
}

/*
 * For VFIO Platform, we don't have a standard configuration space (like PCI)
 * to program the device's MSI doorbell and dynamically allocated EventIDs.
 * This empty message callback assumes that the underlying hardware/firmware
 * is either pre-programmed or hardwired to emit the correct EventIDs that
 * align with the generic kernel's LPI allocations. If a device requires
 * dynamic programming of its MSI registers, a device-specific VFIO reset
 * driver extension is required.
 */
static void vfio_platform_msi_msg(struct msi_desc *desc,
				  struct msi_msg *msg)
{
}

static int vfio_platform_msi_enable(struct vfio_platform_device *vdev,
				    struct vfio_platform_irq *irq, int nvec)
{
	int ret, i, n = irq->count;

	irq->ctx = kcalloc(n, sizeof(struct vfio_platform_irq_ctx),
			   GFP_KERNEL_ACCOUNT);
	if (!irq->ctx)
		return -ENOMEM;

	ret = platform_device_msi_init_and_alloc_irqs(vdev->device, n,
						      vfio_platform_msi_msg);
	if (ret) {
		kfree(irq->ctx);
		irq->ctx = NULL;
		return ret;
	}

	for (i = 0; i < n; i++) {
		irq->ctx[i].hwirq = msi_get_virq(vdev->device, i);
	}
	irq->nr_ctx = n;

	return 0;
}

static int vfio_platform_set_msi_trigger(struct vfio_platform_device *vdev,
					 unsigned index, unsigned start,
					 unsigned count, uint32_t flags, void *data)
{
	struct vfio_platform_irq *irq = &vdev->irqs[index];
	int i, ret;

	if (start + count > irq->count)
		return -EINVAL;

	if (!count && (flags & VFIO_IRQ_SET_DATA_NONE)) {
		vfio_platform_msi_disable(vdev, irq);
		return 0;
	}

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int32_t *fds = data;

		if (!irq->ctx) {
			ret = vfio_platform_msi_enable(vdev, irq, start + count);
			if (ret)
				return ret;
		}

		if (start + count > irq->nr_ctx)
			return -EINVAL;

		for (i = start; i < start + count; i++) {
			if (irq->ctx[i].trigger) {
				free_irq(irq->ctx[i].hwirq, irq->ctx[i].trigger);
				eventfd_ctx_put(irq->ctx[i].trigger);
				kfree(irq->ctx[i].name);
				irq->ctx[i].trigger = NULL;
			}
			if (fds && fds[i - start] >= 0) {
				struct eventfd_ctx *trigger;
				trigger = eventfd_ctx_fdget(fds[i - start]);
				if (IS_ERR(trigger))
					continue;
				irq->ctx[i].trigger = trigger;
				irq->ctx[i].name = kasprintf(GFP_KERNEL_ACCOUNT,
							     "vfio-msi[%d](%s)", i, vdev->name);
				ret = request_irq(irq->ctx[i].hwirq, vfio_platform_msihandler,
						  0, irq->ctx[i].name, trigger);
				if (ret) {
					kfree(irq->ctx[i].name);
					eventfd_ctx_put(trigger);
					irq->ctx[i].trigger = NULL;
				}
			}
		}
		return 0;
	}
	return -EINVAL;
}

int vfio_platform_set_irqs_ioctl(struct vfio_platform_device *vdev,
				 uint32_t flags, unsigned index, unsigned start,
				 unsigned count, void *data)
{
	int (*func)(struct vfio_platform_device *vdev, unsigned index,
		    unsigned start, unsigned count, uint32_t flags,
		    void *data) = NULL;

	/*
	 * For compatibility, errors from request_irq() are local to the
	 * SET_IRQS path and reflected in the name pointer.  This allows,
	 * for example, polling mode fallback for an exclusive IRQ failure.
	 */
	if (IS_ERR(vdev->irqs[index].name))
		return PTR_ERR(vdev->irqs[index].name);

	switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
	case VFIO_IRQ_SET_ACTION_MASK:
		func = vfio_platform_set_irq_mask;
		break;
	case VFIO_IRQ_SET_ACTION_UNMASK:
		func = vfio_platform_set_irq_unmask;
		break;
	case VFIO_IRQ_SET_ACTION_TRIGGER:
		if (vdev->msi_enabled && index == vdev->num_irqs - 1)
			func = vfio_platform_set_msi_trigger;
		else
			func = vfio_platform_set_irq_trigger;
		break;
	}

	if (!func)
		return -ENOTTY;

	return func(vdev, index, start, count, flags, data);
}

int vfio_platform_irq_init(struct vfio_platform_device *vdev)
{
	int cnt = 0, i, ret = 0;
	bool has_msi = false;

	while (vdev->get_irq(vdev, cnt) >= 0)
		cnt++;

	if (vdev->device->of_node &&
	    of_get_property(vdev->device->of_node, "msi-parent", NULL))
		has_msi = true;

	vdev->irqs = kcalloc(has_msi ? cnt + 1 : cnt, sizeof(struct vfio_platform_irq),
			     GFP_KERNEL_ACCOUNT);
	if (!vdev->irqs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		int hwirq = vdev->get_irq(vdev, i);
		irq_handler_t handler = vfio_irq_handler;

		if (hwirq < 0) {
			ret = -EINVAL;
			goto err;
		}

		spin_lock_init(&vdev->irqs[i].lock);

		vdev->irqs[i].flags = VFIO_IRQ_INFO_EVENTFD;

		if (irq_get_trigger_type(hwirq) & IRQ_TYPE_LEVEL_MASK) {
			vdev->irqs[i].flags |= VFIO_IRQ_INFO_MASKABLE
						| VFIO_IRQ_INFO_AUTOMASKED;
			handler = vfio_automasked_irq_handler;
		}

		vdev->irqs[i].count = 1;
		vdev->irqs[i].hwirq = hwirq;
		vdev->irqs[i].masked = false;
		vdev->irqs[i].name = kasprintf(GFP_KERNEL_ACCOUNT,
					       "vfio-irq[%d](%s)", hwirq,
					       vdev->name);
		if (!vdev->irqs[i].name) {
			ret = -ENOMEM;
			goto err;
		}

		ret = request_irq(hwirq, handler, IRQF_NO_AUTOEN,
				  vdev->irqs[i].name, &vdev->irqs[i]);
		if (ret) {
			kfree(vdev->irqs[i].name);
			vdev->irqs[i].name = ERR_PTR(ret);
		}
	}

	if (has_msi) {
		vdev->irqs[cnt].flags = VFIO_IRQ_INFO_EVENTFD | VFIO_IRQ_INFO_NORESIZE;
		vdev->irqs[cnt].count = 256; /* Allow up to 256 MSIs */
		vdev->irqs[cnt].hwirq = 0;
		vdev->irqs[cnt].name = kasprintf(GFP_KERNEL_ACCOUNT,
					       "vfio-msi(%s)", vdev->name);
		if (!vdev->irqs[cnt].name) {
			ret = -ENOMEM;
			goto err;
		}
		vdev->msi_enabled = true;
		vdev->num_irqs = cnt + 1;
	} else {
		vdev->num_irqs = cnt;
	}

	return 0;

err:
	for (--i; i >= 0; i--) {
		if (!IS_ERR(vdev->irqs[i].name)) {
			free_irq(vdev->irqs[i].hwirq, &vdev->irqs[i]);
			kfree(vdev->irqs[i].name);
		}
	}
	kfree(vdev->irqs);
	return ret;
}

void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev)
{
	int i;

	for (i = 0; i < vdev->num_irqs; i++) {
		vfio_virqfd_disable(&vdev->irqs[i].mask);
		vfio_virqfd_disable(&vdev->irqs[i].unmask);

		if (vdev->msi_enabled && i == vdev->num_irqs - 1) {
			vfio_platform_msi_disable(vdev, &vdev->irqs[i]);
			kfree(vdev->irqs[i].name);
			continue;
		}

		if (!IS_ERR(vdev->irqs[i].name)) {
			free_irq(vdev->irqs[i].hwirq, &vdev->irqs[i]);
			if (vdev->irqs[i].trigger)
				eventfd_ctx_put(vdev->irqs[i].trigger);
			kfree(vdev->irqs[i].name);
		}
	}

	vdev->msi_enabled = false;
	vdev->num_irqs = 0;
	kfree(vdev->irqs);
}
