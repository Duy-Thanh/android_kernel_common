// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/irq.h>
#include <linux/delay.h>
#include <trace/events/sched.h>
#include "sched.h"
#include "trace.h"

#ifdef CONFIG_HOTPLUG_CPU
static int do_isolation_work_cpu_stop(void *data)
{
	unsigned int cpu = smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct rq_flags rf;

	local_irq_disable();

	irq_migrate_all_off_this_cpu();

	sched_ttwu_pending();

	/* Update our root-domain */
	rq_lock(rq, &rf);

	/*
	 * Temporarily mark the rq as offline. This will allow us to
	 * move tasks off the CPU.
	 */
	if (rq->rd) {
		BUG_ON(!cpumask_test_cpu(cpu, rq->rd->span));
		set_rq_offline(rq);
	}

	migrate_tasks(rq, &rf, false);

	if (rq->rd)
		set_rq_online(rq);
	rq_unlock(rq, &rf);

	local_irq_enable();
	return 0;
}

static int do_unisolation_work_cpu_stop(void *data)
{
	watchdog_enable(smp_processor_id());
	return 0;
}

static void sched_update_group_capacities(int cpu)
{
	struct sched_domain *sd;

	mutex_lock(&sched_domains_mutex);
	rcu_read_lock();

	for_each_domain(cpu, sd) {
		int balance_cpu = group_balance_cpu(sd->groups);

		init_sched_groups_capacity(cpu, sd);
		/*
		 * Need to ensure this is also called with balancing
		 * cpu.
		 */
		if (cpu != balance_cpu)
			init_sched_groups_capacity(balance_cpu, sd);
	}

	rcu_read_unlock();
	mutex_unlock(&sched_domains_mutex);
}

static unsigned int cpu_isolation_vote[NR_CPUS];

/*
 * 1) CPU is isolated and cpu is offlined:
 *Unisolate the core.
 * 2) CPU is not isolated and CPU is offlined:
 *No action taken.
 * 3) CPU is offline and request to isolate
 *Request ignored.
 * 4) CPU is offline and isolated:
 *Not a possible state.
 * 5) CPU is online and request to isolate
 *Normal case: Isolate the CPU
 * 6) CPU is not isolated and comes back online
 *Nothing to do
 *
 * Note: The client calling sched_isolate_cpu() is repsonsible for ONLY
 * calling sched_unisolate_cpu() on a CPU that the client previously isolated.
 * Client is also responsible for unisolating when a core goes offline
 * (after CPU is marked offline).
 */
int sched_isolate_cpu(int cpu)
{
	struct rq *rq;
	cpumask_t avail_cpus;
	int ret_code = 0;
	u64 start_time = 0;

	if (trace_sched_isolate_enabled())
		start_time = sched_clock();

	cpu_maps_update_begin();

	cpumask_andnot(&avail_cpus, cpu_online_mask, cpu_isolated_mask);

	if (cpu < 0 || cpu >= nr_cpu_ids || cpu >= NR_CPUS || !cpu_possible(cpu) ||
	    !cpu_online(cpu)) {
		ret_code = -EINVAL;
		goto out;
	}

	rq = cpu_rq(cpu);

	if (++cpu_isolation_vote[cpu] > 1)
		goto out;

	/* We cannot isolate ALL cpus in the system */
	if (cpumask_weight(&avail_cpus) == 1) {
		--cpu_isolation_vote[cpu];
		ret_code = -EINVAL;
		goto out;
	}

	/*
	 * There is a race between watchdog being enabled by hotplug and
	 * core isolation disabling the watchdog. When a CPU is hotplugged in
	 * and the hotplug lock has been released the watchdog thread might
	 * not have run yet to enable the watchdog.
	 * We have to wait for the watchdog to be enabled before proceeding.
	 */
	if (!watchdog_configured(cpu)) {
		msleep(20);
		if (!watchdog_configured(cpu)) {
			--cpu_isolation_vote[cpu];
			ret_code = -EBUSY;
			goto out;
		}
	}

	set_cpu_isolated(cpu, true);
	cpumask_clear_cpu(cpu, &avail_cpus);

	/* Migrate timers */
	smp_call_function_any(&avail_cpus, hrtimer_quiesce_cpu, &cpu, 1);
	smp_call_function_any(&avail_cpus, timer_quiesce_cpu, &cpu, 1);

	watchdog_disable(cpu);
	irq_lock_sparse();
	stop_cpus(cpumask_of(cpu), do_isolation_work_cpu_stop, 0);
	irq_unlock_sparse();

	calc_load_migrate(rq);
	update_max_interval();
	sched_update_group_capacities(cpu);

out:
	cpu_maps_update_done();
	trace_sched_isolate(cpu, cpumask_bits(cpu_isolated_mask)[0],
			    start_time, 1);
	return ret_code;
}
EXPORT_SYMBOL(sched_isolate_cpu);

/*
 * Note: The client calling sched_isolate_cpu() is repsonsible for ONLY
 * calling sched_unisolate_cpu() on a CPU that the client previously isolated.
 * Client is also responsible for unisolating when a core goes offline
 * (after CPU is marked offline).
 */
int sched_unisolate_cpu_unlocked(int cpu)
{
	int ret_code = 0;
	u64 start_time = 0;

	if (cpu < 0 || cpu >= nr_cpu_ids || !cpu_possible(cpu)) {
		ret_code = -EINVAL;
		goto out;
	}

	if (trace_sched_isolate_enabled())
		start_time = sched_clock();

	if (!cpu_isolation_vote[cpu]) {
		ret_code = -EINVAL;
		goto out;
	}

	if (--cpu_isolation_vote[cpu])
		goto out;

	set_cpu_isolated(cpu, false);
	update_max_interval();
	sched_update_group_capacities(cpu);

	if (cpu_online(cpu)) {
		stop_cpus(cpumask_of(cpu), do_unisolation_work_cpu_stop, 0);

		/* Kick CPU to immediately do load balancing */
		if (!atomic_fetch_or(NOHZ_KICK_MASK, nohz_flags(cpu)))
			smp_send_reschedule(cpu);
	}

out:
	trace_sched_isolate(cpu, cpumask_bits(cpu_isolated_mask)[0],
			    start_time, 0);
	return ret_code;
}
EXPORT_SYMBOL(sched_unisolate_cpu_unlocked);

int sched_unisolate_cpu(int cpu)
{
	int ret_code;

	cpu_maps_update_begin();
	ret_code = sched_unisolate_cpu_unlocked(cpu);
	cpu_maps_update_done();
	return ret_code;
}
EXPORT_SYMBOL(sched_unisolate_cpu);

#endif /* CONFIG_HOTPLUG_CPU */

