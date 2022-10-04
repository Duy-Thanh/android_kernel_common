// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <linux/arch_topology.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

//TODO: Add accompanying DT binding documents in another patch.

enum {
	REG_CUR_FREQ,
	REG_SET_FREQ,
	REG_END,
};

struct kvm_cpufreq_ops {
	void (*set_freq)(void* cpufreq_data, u32 freq);
	u32 (*get_freq)(void* cpufreq_data);
};

struct kvm_cpufreq_info {
	struct kvm_cpufreq_ops *ops;
	const u32 *offsets;
	u32 offsets_len;
};

struct kvm_cpufreq_drv_data {
	struct device *dev;
	void __iomem *base;
	const struct kvm_cpufreq_info* info;
};

struct kvm_cpufreq_policy_data {
	struct kvm_cpufreq_drv_data *drv_data;
	void __iomem *reg[];
};

static void kvm_cpufreq_set_freq(void *cpufreq_data, u32 freq)
{
	struct kvm_cpufreq_policy_data *data = (struct kvm_cpufreq_policy_data*)cpufreq_data;

	writel_relaxed(freq, data->reg[REG_SET_FREQ]);
}

static u32 kvm_cpufreq_get_freq(void *cpufreq_data)
{
	struct kvm_cpufreq_policy_data *data = (struct kvm_cpufreq_policy_data*)cpufreq_data;

	return readl_relaxed(data->reg[REG_CUR_FREQ]);
}

static struct kvm_cpufreq_ops kvm_freq_ops = {
	.set_freq = kvm_cpufreq_set_freq,
	.get_freq = kvm_cpufreq_get_freq,
};

static const u32 kvm_cpufreq_offsets[] = {
	[REG_CUR_FREQ] = 0x0,
	[REG_SET_FREQ] = 0x4,
	[REG_END] = 0x8,
};

static const struct kvm_cpufreq_info kvm_freq_info = {
	.ops = &kvm_freq_ops,
	.offsets = kvm_cpufreq_offsets,
	.offsets_len = ARRAY_SIZE(kvm_cpufreq_offsets),
};

static void kvm_scale_freq_tick(void)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(task_cpu(current));
	struct kvm_cpufreq_policy_data *policy_data = policy->driver_data;
	struct kvm_cpufreq_ops *ops = policy_data->drv_data->info->ops;
	u64 max_freq = (u64)policy->cpuinfo.max_freq;
	unsigned long scale;
	u64 cur_freq;

	cpufreq_cpu_put(policy);

	cur_freq = (u64)ops->get_freq(policy_data);
	cur_freq <<= SCHED_CAPACITY_SHIFT;
	scale = div64_u64(cur_freq, max_freq);

	this_cpu_write(arch_freq_scale, (unsigned long)scale);
}

static struct scale_freq_data kvm_sfd = {
	.source = SCALE_FREQ_SOURCE_KVM,
	.set_freq_scale = kvm_scale_freq_tick,
};

static unsigned int kvm_cpufreq_set_perf(struct cpufreq_policy *policy)
{
	struct kvm_cpufreq_policy_data *policy_data = policy->driver_data;
	struct kvm_cpufreq_ops *ops = policy_data->drv_data->info->ops;

	/*
	 * Use cached frequency to avoid rounding to freq table entries
	 * and undo 25% frequency boost from util.
	 */
	u32 freq = mult_frac(policy->cached_target_freq, 80, 100);

	ops->set_freq(policy_data, freq);
	return 0;
}

static unsigned int kvm_cpufreq_fast_switch(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
	kvm_cpufreq_set_perf(policy);
	return target_freq;
}

static int kvm_cpufreq_target_index(struct cpufreq_policy *policy,
		unsigned int index)
{
	return kvm_cpufreq_set_perf(policy);
}

static const struct of_device_id kvm_cpufreq_match[] = {
	{ .compatible = "virtual,kvm-cpufreq", .data = &kvm_freq_info},
	{}
};
MODULE_DEVICE_TABLE(of, kvm_cpufreq_match);

static int kvm_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct kvm_cpufreq_drv_data *drv_data = cpufreq_get_driver_data();
	struct kvm_cpufreq_policy_data *policy_data;
	struct cpufreq_frequency_table *table;
	struct device *cpu_dev;
	int ret, i;

	policy_data = devm_kzalloc(drv_data->dev,
				sizeof(*policy_data) +
				drv_data->info->offsets_len * sizeof(policy_data->reg[0]),
				GFP_KERNEL);
	if (!policy_data)
		return -ENOMEM;

	policy_data->drv_data = drv_data;

	//TODO: Move init for policy_data to ops or use some DT representation for freq-domain/cpu mapping.
	for (i = 0; i < drv_data->info->offsets_len; i++)
		policy_data->reg[i] = drv_data->base + drv_data->info->offsets[i] +
			policy->cpu * drv_data->info->offsets[REG_END];

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev)
		return -ENODEV;

	ret = dev_pm_opp_of_add_table(cpu_dev);
	if (ret)
		return ret;

	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		return -ENODEV;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		return ret;
	}

	policy->freq_table = table;
	policy->dvfs_possible_from_any_cpu = false;
	policy->fast_switch_possible = true;
	policy->transition_delay_us = 1;
	policy->driver_data = policy_data;

	/*
	 * Only takes effect if another FIE source such as AMUs
	 * have not been registered.
	 */
	topology_set_scale_freq_source(&kvm_sfd, policy->cpus);

	return 0;
}

static int kvm_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	topology_clear_scale_freq_source(SCALE_FREQ_SOURCE_KVM, policy->related_cpus);
	kfree(policy->freq_table);
	return 0;
}

static int kvm_cpufreq_online(struct cpufreq_policy *policy)
{
	/* Nothing to restore. */
	return 0;
}

static int kvm_cpufreq_offline(struct cpufreq_policy *policy)
{
	/* Dummy offline() to avoid exit() being called and freeing resources. */
	return 0;
}

static struct cpufreq_driver cpufreq_kvm_driver = {
	.name		= "kvm-cpufreq",
	.init		= kvm_cpufreq_cpu_init,
	.exit		= kvm_cpufreq_cpu_exit,
	.online         = kvm_cpufreq_online,
	.offline        = kvm_cpufreq_offline,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= kvm_cpufreq_target_index,
	.fast_switch	= kvm_cpufreq_fast_switch,
	.attr		= cpufreq_generic_attr,
};

static int kvm_cpufreq_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct kvm_cpufreq_drv_data *drv_data;

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->info = of_device_get_match_data(&pdev->dev);
	if (!drv_data->info || !drv_data->info->ops)
		return -EINVAL;

	drv_data->dev = &pdev->dev;
	drv_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv_data->base))
		return PTR_ERR(drv_data->base);

	cpufreq_kvm_driver.driver_data = drv_data;

	ret = cpufreq_register_driver(&cpufreq_kvm_driver);
	if (ret) {
		dev_err(&pdev->dev, "KVM CPUFreq driver failed to register: %d\n", ret);
		return ret;
	}

	dev_dbg(&pdev->dev, "KVM CPUFreq driver initialized\n");
	return 0;
}

static int kvm_cpufreq_driver_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&cpufreq_kvm_driver);
	return 0;
}

static struct platform_driver kvm_cpufreq_driver = {
	.probe = kvm_cpufreq_driver_probe,
	.remove = kvm_cpufreq_driver_remove,
	.driver = {
		.name = "kvm-cpufreq",
		.of_match_table = kvm_cpufreq_match,
	},
};

static int __init kvm_cpufreq_init(void)
{
	return platform_driver_register(&kvm_cpufreq_driver);
}
postcore_initcall(kvm_cpufreq_init);

static void __exit kvm_cpufreq_exit(void)
{
	platform_driver_unregister(&kvm_cpufreq_driver);
}
module_exit(kvm_cpufreq_exit);

MODULE_DESCRIPTION("KVM cpufreq driver");
MODULE_LICENSE("GPL");
