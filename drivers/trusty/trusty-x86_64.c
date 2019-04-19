// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 LK Trusty Authors.
 */

#include <asm/apic.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/trusty/trusty_custom_smc.h>

#define IKGT_SMC_HC_ID 0x74727500

static irqreturn_t stub_action(int cpl, void *dev_id)
{
	/* Trusty interrupt should not be received at this point */
	BUG_ON(1);

	return IRQ_NONE;
}

static struct irqaction tmp_action = {
	.handler = stub_action,
	.flags = IRQF_NO_THREAD,
	.name = "trusty-tmp",
};

/*
 * Production code should reserve vectors for Trusty based on platform
 * configuration, Trusty IRQs and vectors used here might conflict with
 * hardware device drivers.
 *
 * Linux kernel has reserved following IRQs for backward compatiblity.
 * 	IRQ 0 -- Legacy timer.
 * 	IRQ 2 -- Cascade interrupt to seconnd interrupt controller.
 * 	IRQ 4 -- Serial port controller for serial port 1(ttyS0).
 * More details about IRQs of Intel 8259 PIC please refer:
 * https://en.wikipedia.org/wiki/Interrupt_request_(PC_archotecture)
 *
 * Following IRQs would be used as Trusty interrupts since they are
 * obsolete in moden kernel usage:
 *  IRQ 1 -- Keyboard controller.
 *  IRQ 3 -- Serial port controller for serial port 2.
 *  IRQ 5 -- Parallel port 2 and 3 or sound card.
 *
 * Linux kernel has reserved following vector(s):
 *  Vector 0x20 -- Legacy timer.
 *
 * setup_irq is used to statically setup interrupts in the early boot
 * process, this function is used here to reserve IRQs and vectors for
 * Trusty interrupts.
 *
 * Since kernel assigns IRQ 0 and vector 0x20 to 8254 PIT timer at early
 * boot stage, when setup_irq is invoked to reserved IRQs and vectors
 * for Trusty at core init stage, vectors allocated begin from 0x21.
 *
 * IRQs/vectors to be reserved for Trusty:
 *  IRQ 1 -- Trusty timer interrupt, expected vector is 0x21.
 *  IRQ 3 -- Reserved for future, expected vector is 0x22.
 *  IRQ 5 -- Reserved for future, expected vector is 0x23.
 */
struct trusty_x86_64_irq_s {
	unsigned int irq;
	unsigned int vector;
	struct irqaction *action;
} trusty_x86_64_irq[] = {
	{
		.irq = 1,
		.vector = 0x21,
		.action = &tmp_action,
	},
	{
		.irq = 3,
		.vector = 0x22,
		.action = &tmp_action,
	},
	{
		.irq = 5,
		.vector = 0x23,
		.action = &tmp_action,
	},
};

static inline ulong smc_instr(ulong r0, ulong r1, ulong r2, ulong r3,
			      struct trusty_custom_smc *dummy_smc)
{
	register unsigned long smc_id asm("rax") = IKGT_SMC_HC_ID;

	__asm__ __volatile__ (
			"vmcall"
			: "=D" (r0)
			: "r" (smc_id),  "D" (r0), "S" (r1), "d" (r2), "b" (r3)
	);

	return r0;
}

struct trusty_custom_smc x86_64_smc = {
	.smc = smc_instr,
};

static const struct of_device_id trusty_x86_64_of_match[] = {
	{ .compatible = "android,trusty-x86_64-smc-v1"},
	{},
};

static int trusty_x86_64_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	dev_dbg(&pdev->dev, "Initializing trusty x86_64 driver\n");

	if (!node) {
		dev_err(&pdev->dev, "of_node required\n");
		return -EINVAL;
	}

	trusty_custom_smc_set_drvdata(&pdev->dev, &x86_64_smc);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add children: %d\n", ret);
		x86_64_smc.smc = NULL;
		trusty_custom_smc_set_drvdata(&pdev->dev, &x86_64_smc);
		return ret;
	}

	return 0;
}

static int trusty_x86_64_remove_child(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int trusty_x86_64_remove(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, trusty_x86_64_remove_child);

	return 0;
}

static struct platform_driver trusty_x86_64_driver = {
	.probe = trusty_x86_64_probe,
	.remove = trusty_x86_64_remove,
	.driver	= {
		.name = "trusty_x86_64",
		.owner = THIS_MODULE,
		.of_match_table = trusty_x86_64_of_match,
	},
};

static int __init trusty_x86_64_driver_init(void)
{
	return platform_driver_register(&trusty_x86_64_driver);
}

static void __exit trusty_x86_64_driver_exit(void)
{
	platform_driver_unregister(&trusty_x86_64_driver);
}

int trusty_x86_64_irq_get_mapping(unsigned int vector)
{
	int idx;
	int size = sizeof(trusty_x86_64_irq)/sizeof(struct trusty_x86_64_irq_s);
	struct trusty_x86_64_irq_s *irq_status = NULL;

	for (idx = 0; idx < size; idx++) {
		if (trusty_x86_64_irq[idx].vector == vector) {
			irq_status = &trusty_x86_64_irq[idx];
		}
	}

	BUG_ON(!irq_status);
	remove_irq(irq_status->irq, irq_status->action);

	return irq_status->irq;
}

void trusty_x86_64_irq_retrigger(unsigned int irq)
{
	int idx;
	int size = sizeof(trusty_x86_64_irq)/sizeof(struct trusty_x86_64_irq_s);
	struct trusty_x86_64_irq_s *irq_status = NULL;

	for (idx = 0; idx < size; idx++) {
		if (trusty_x86_64_irq[idx].irq == irq) {
			irq_status = &trusty_x86_64_irq[idx];
		}
	}

	BUG_ON(!irq_status);
	apic->send_IPI_self(irq_status->vector);
}

static int __init trusty_x86_64_irq_init(void)
{
	int idx;
	int size = sizeof(trusty_x86_64_irq)/sizeof(struct trusty_x86_64_irq_s);

	/* Set up irq action for each IRQ */
	for (idx = 0; idx < size; idx++) {
		struct trusty_x86_64_irq_s *irq_status = &trusty_x86_64_irq[idx];
		struct irq_cfg *cfg;

		setup_irq(irq_status->irq, irq_status->action);
		cfg = irq_cfg(irq_status->irq);

		BUG_ON(!cfg || (cfg->vector != irq_status->vector));
	}

	return 0;
}

core_initcall(trusty_x86_64_irq_init);

subsys_initcall(trusty_x86_64_driver_init);
module_exit(trusty_x86_64_driver_exit);
