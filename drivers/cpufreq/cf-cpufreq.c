/* drivers/cpufreq/cf-cpufreq.c
 *
 * Copyright (C) 2019 Google, Inc.
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

#include <linux/cpufreq.h>

static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 1 },
	{ .frequency = 2 },
	{ .frequency = CPUFREQ_TABLE_END },
};

static int cf_cpufreq_target_index(struct cpufreq_policy *policy,
				   unsigned int index)
{
	return 0;
}

static int cf_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	return cpufreq_table_validate_and_show(policy, freq_table);
}

static int cf_cpufreq_verify(struct cpufreq_policy *policy)
{
	return 0;
}

static struct cpufreq_driver cf_cpufreq_driver = {
	.name = "cf",
	.target_index = cf_cpufreq_target_index,
	.init = cf_cpufreq_driver_init,
	.verify = cf_cpufreq_verify,
	.attr = cpufreq_generic_attr,
};

static int __init cf_cpufreq_init(void)
{
	return cpufreq_register_driver(&cf_cpufreq_driver);
}

late_initcall(cf_cpufreq_init);
