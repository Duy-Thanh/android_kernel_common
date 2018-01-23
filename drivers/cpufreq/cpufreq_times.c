/*
 * drivers/cpufreq/cpufreq_times.c
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.*
 *
 */

#include <linux/atomic.h>
#include <linux/cpufreq.h>
#include <linux/cputime.h>
#include <linux/hashtable.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#define UID_HASH_BITS 10

DECLARE_HASHTABLE(uid_hash_table, UID_HASH_BITS);

static DEFINE_SPINLOCK(cpufreq_times_lock);

static DEFINE_SPINLOCK(task_time_in_state_lock); /* task->time_in_state */
static DEFINE_RT_MUTEX(uid_lock); /* uid_hash_table */

struct uid_entry {
	uid_t uid;
	unsigned int dead_max_state;
	unsigned int alive_max_state;
	u64 *dead_time_in_state;
	u64 *alive_time_in_state;
	struct hlist_node hash;
};

struct cpufreq_times {
	unsigned long long last_time;
	unsigned int max_state;
	atomic_t last_index;
	unsigned int offset;
	u64 *time_in_state;
};

static unsigned int next_offset;
static unsigned int *cpufreq_states;

/* Caller must hold uid lock */
static struct uid_entry *find_uid_entry(uid_t uid)
{
	struct uid_entry *uid_entry;

	hash_for_each_possible(uid_hash_table, uid_entry, hash, uid) {
		if (uid_entry->uid == uid)
			return uid_entry;
	}
	return NULL;
}

/* Caller must hold uid lock */
static struct uid_entry *find_or_register_uid(uid_t uid)
{
	struct uid_entry *uid_entry;

	uid_entry = find_uid_entry(uid);
	if (uid_entry)
		return uid_entry;

	uid_entry = kzalloc(sizeof(struct uid_entry), GFP_ATOMIC);
	if (!uid_entry)
		return NULL;

	uid_entry->uid = uid;

	hash_add(uid_hash_table, &uid_entry->hash, uid);

	return uid_entry;
}

static int account_thread_stats(struct task_struct *task,
		u64 **total_time_in_state, unsigned int *max_state)
{
	unsigned long flags;
	unsigned int i;
	u64 *tmp;

	if (*max_state < task->max_state) {
		tmp = krealloc(*total_time_in_state,
			       task->max_state * sizeof(u64),
			       GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;
		*total_time_in_state = tmp;
		memset(*total_time_in_state + *max_state, 0,
		       (task->max_state - *max_state) *
		       sizeof(u64));
		*max_state = task->max_state;
	}
	spin_lock_irqsave(&task_time_in_state_lock, flags);
	if (task->time_in_state) {
		for (i = 0; i < task->max_state; ++i) {
			(*total_time_in_state)[i] +=
				atomic_read(&task->time_in_state[i]);
		}
	}
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	return 0;
}

static int single_uid_time_in_state_show(struct seq_file *m, void *ptr)
{
	struct uid_entry *uid_entry;
	struct task_struct *task, *temp;
	unsigned int i, max_state;
	u64 *total_time_in_state;
	uid_t uid = from_kuid_munged(current_user_ns(), *(kuid_t *)m->private);

	if (uid == overflowuid)
		return -EINVAL;

	rt_mutex_lock(&uid_lock);

	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		rt_mutex_unlock(&uid_lock);
		return -ENOMEM;
	}
	max_state = uid_entry->dead_max_state;
	total_time_in_state = kcalloc(max_state, sizeof(u64), GFP_KERNEL);
	if (!total_time_in_state) {
		rt_mutex_unlock(&uid_lock);
		return -ENOMEM;
	}
	memcpy(total_time_in_state, uid_entry->dead_time_in_state,
	       max_state * sizeof(u64));

	rt_mutex_unlock(&uid_lock);

	rcu_read_lock();
	do_each_thread(temp, task) {
		int err;
		uid_t tsk_uid = from_kuid_munged(
			current_user_ns(), task_uid(task));
		if (tsk_uid != uid)
			continue;

		err = account_thread_stats(task,
					   &total_time_in_state,
					   &max_state);
		if (err) {
			rcu_read_unlock();
			kfree(total_time_in_state);
			return err;
		}
	} while_each_thread(temp, task);
	rcu_read_unlock();

	for (i = 0; i < max_state; ++i) {
		total_time_in_state[i] =
			cputime_to_clock_t(total_time_in_state[i]);
	}

	seq_write(m, total_time_in_state, max_state * sizeof(u64));
	kfree(total_time_in_state);
	return 0;
}

static int uid_time_in_state_show(struct seq_file *m, void *v)
{
	struct uid_entry *uid_entry;
	struct task_struct *task, *temp;
	unsigned long bkt;
	int i;

	seq_puts(m, "uid:");
	spin_lock(&cpufreq_times_lock);
	for (i = 0; i < next_offset; i++)
		seq_printf(m, " %d", cpufreq_states[i]);
	spin_unlock(&cpufreq_times_lock);

	rt_mutex_lock(&uid_lock);
	seq_putc(m, '\n');

	rcu_read_lock();
	do_each_thread(temp, task) {
		uid_entry = find_or_register_uid(from_kuid_munged(
			 current_user_ns(), task_uid(task)));
		if (!uid_entry)
			continue;

		account_thread_stats(
			task,
			&(uid_entry->alive_time_in_state),
			&(uid_entry->alive_max_state));
	} while_each_thread(temp, task);
	rcu_read_unlock();

	hash_for_each(uid_hash_table, bkt, uid_entry, hash) {
		int max_state = uid_entry->dead_max_state;

		if (uid_entry->alive_max_state > max_state)
			max_state = uid_entry->alive_max_state;
		if (max_state)
			seq_printf(m, "%d:", uid_entry->uid);
		for (i = 0; i < max_state; ++i) {
			u64 total_time_in_state = 0;

			if (uid_entry->dead_time_in_state &&
			    i < uid_entry->dead_max_state) {
				total_time_in_state =
					uid_entry->dead_time_in_state[i];
			}
			if (uid_entry->alive_time_in_state &&
			    i < uid_entry->alive_max_state) {
				total_time_in_state +=
					uid_entry->alive_time_in_state[i];
			}
			seq_printf(m, " %lu", (unsigned long)
				   cputime_to_clock_t(total_time_in_state));
		}
		if (max_state)
			seq_putc(m, '\n');

		kfree(uid_entry->alive_time_in_state);
		uid_entry->alive_time_in_state = NULL;
		uid_entry->alive_max_state = 0;
	}

	rt_mutex_unlock(&uid_lock);
	return 0;
}

static int cpufreq_times_update(struct cpufreq_times *times)
{
	unsigned long long cur_time = get_jiffies_64();

	spin_lock(&cpufreq_times_lock);
	times->time_in_state[atomic_read(&times->last_index)] +=
		cur_time - times->last_time;
	times->last_time = cur_time;
	spin_unlock(&cpufreq_times_lock);
	return 0;
}

void cpufreq_task_times_init(struct task_struct *p)
{
	void *temp;
	unsigned long flags;
	unsigned int max_state;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	p->time_in_state = NULL;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	WRITE_ONCE(p->max_state, 0);

	WRITE_ONCE(max_state, next_offset);

	/* We use one array to avoid multiple allocs per task */
	temp = kcalloc(max_state, sizeof(p->time_in_state[0]), GFP_ATOMIC);
	if (!temp)
		return;

	spin_lock_irqsave(&task_time_in_state_lock, flags);
	p->time_in_state = temp;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	WRITE_ONCE(p->max_state, max_state);
}

void cpufreq_task_times_exit(struct task_struct *p)
{
	struct uid_entry *uid_entry;
	unsigned long flags;
	uid_t uid;
	int i;
	bool account_times = true;
	void *temp;

	if (!p)
		return;

	rt_mutex_lock(&uid_lock);

	uid = from_kuid_munged(current_user_ns(), task_uid(p));
	uid_entry = find_or_register_uid(uid);
	if (!uid_entry) {
		pr_err("%s: failed to find uid %d\n", __func__, uid);
		account_times = false;
		goto out;
	}

	if (uid_entry->dead_max_state < p->max_state) {
		temp = krealloc(uid_entry->dead_time_in_state,
				p->max_state *
				sizeof(uid_entry->dead_time_in_state[0]),
				GFP_ATOMIC);
		if (!temp) {
			account_times = false;
			goto out;
		}
		uid_entry->dead_time_in_state = temp;
		memset(uid_entry->dead_time_in_state +
			uid_entry->dead_max_state,
			0, (p->max_state - uid_entry->dead_max_state) *
			sizeof(uid_entry->dead_time_in_state[0]));

	}

 out:
	spin_lock_irqsave(&task_time_in_state_lock, flags);
	if (account_times && p->time_in_state) {
		for (i = 0; i < p->max_state; ++i) {
			uid_entry->dead_time_in_state[i] +=
				atomic_read(&p->time_in_state[i]);
		}
	}
	temp = p->time_in_state;
	p->time_in_state = NULL;
	spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	rt_mutex_unlock(&uid_lock);
	kfree(temp);
}

int proc_time_in_state_show(struct seq_file *m, struct pid_namespace *ns,
	struct pid *pid, struct task_struct *p)
{
	int i;
	cputime_t cputime;
	unsigned long flags;

	if (!p->time_in_state)
		return 0;

	spin_lock(&cpufreq_times_lock);
	for (i = 0; i < p->max_state; ++i) {
		cputime = 0;
		spin_lock_irqsave(&task_time_in_state_lock, flags);
		if (p->time_in_state)
			cputime = atomic_read(&p->time_in_state[i]);
		spin_unlock_irqrestore(&task_time_in_state_lock, flags);

		seq_printf(m, "%u %lu\n", cpufreq_states[i],
			(unsigned long)cputime_to_clock_t(cputime));
	}
	spin_unlock(&cpufreq_times_lock);

	return 0;
}

static ssize_t show_time_in_state(struct cpufreq_policy *policy, char *buf)
{
	struct cpufreq_frequency_table *table, *pos;
	struct cpufreq_times *times = policy->times;
	ssize_t len = 0;
	int i;

	table = cpufreq_frequency_get_table(policy->cpu);
	if (!table)
		return 0;

	cpufreq_times_update(times);
	cpufreq_for_each_valid_entry(pos, table) {
		i = pos - table;
		len += sprintf(buf + len, "%u %llu\n", pos->frequency,
			       (unsigned long long)
			       jiffies_64_to_clock_t(times->time_in_state[i]));
	}
	return len;
}

/* Called without cpufreq_times_lock held */
void acct_update_power(struct task_struct *task, cputime_t cputime)
{
	struct cpufreq_policy *policy;
	struct cpufreq_times *times;
	unsigned int cpu, state;
	unsigned long flags;

	if (!task)
		return;

	cpu = task_cpu(task);
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return;

	times = policy->times;
	if (!times) {
		cpufreq_cpu_put(policy);
		return;
	}

	state = times->offset + atomic_read(&policy->times->last_index);

	/* This function is called from a different context
	 * Interruptions in between reads/assignements are ok
	 */

	if (!(task->flags & PF_EXITING) &&
	    state < READ_ONCE(task->max_state)) {
		spin_lock_irqsave(&task_time_in_state_lock, flags);
		if (task->time_in_state)
			atomic64_add(cputime, &task->time_in_state[state]);
		spin_unlock_irqrestore(&task_time_in_state_lock, flags);
	}
	cpufreq_cpu_put(policy);
}

static ssize_t show_all_time_in_state(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i, cpu, freq;
	bool found;
	u64 time;
	struct cpufreq_policy *policy;
	struct cpufreq_times *times;

	len += scnprintf(buf + len, PAGE_SIZE - len, "freq\t\t");
	for_each_possible_cpu(cpu) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "cpu%u\t\t", cpu);
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (policy->times)
			cpufreq_times_update(policy->times);
		cpufreq_cpu_put(policy);
	}

	spin_lock(&cpufreq_times_lock);
	for (i = 0; i < next_offset; i++) {
		freq = cpufreq_states[i];
		len += scnprintf(buf + len, PAGE_SIZE - len, "\n%u\t\t", freq);
		for_each_possible_cpu(cpu) {
			found = false;
			policy = cpufreq_cpu_get(cpu);
			if (policy) {
				times = policy->times;
				if (times && times->offset <= i &&
				    i < times->offset + times->max_state) {
					time = times->time_in_state[i - times->offset];
					found = true;
				}
				cpufreq_cpu_put(policy);
			}
			if (found) {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						 "%lu\t\t", (unsigned long)
						 cputime64_to_clock_t(time));
			} else {
				len += scnprintf(buf + len, PAGE_SIZE - len,
						 "N/A\t\t");
			}
		}
	}
	spin_unlock(&cpufreq_times_lock);
	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}

cpufreq_freq_attr_ro(time_in_state);

static struct attribute *default_attrs[] = {
	&time_in_state.attr,
	NULL
};
static struct attribute_group times_attr_group = {
	.attrs = default_attrs,
	.name = "stats"
};

static struct kobj_attribute _attr_all_time_in_state = __ATTR(all_time_in_state,
		0444, show_all_time_in_state, NULL);

static int cpufreq_times_create_table(struct cpufreq_policy *policy)
{
	int ret;
	unsigned int count = 0;
	struct cpufreq_times *times;
	struct cpufreq_frequency_table *pos, *table;
	void *tmp;

	if (policy->times)
		return -EEXIST;

	table = cpufreq_frequency_get_table(policy->cpu);
	if (unlikely(!table))
		return 0;

	times = kzalloc(sizeof(*times), GFP_KERNEL);
	if (times == NULL)
		return -ENOMEM;

	cpufreq_for_each_entry(pos, table)
		count++;

	times->time_in_state = kcalloc(count, sizeof(u64), GFP_KERNEL);
	if (!times->time_in_state) {
		kfree(times);
		return -ENOMEM;
	}
	times->last_time = get_jiffies_64();
	atomic_set(&times->last_index,
		   cpufreq_frequency_table_get_index(policy, policy->cur));
	times->max_state = count;

	spin_lock(&cpufreq_times_lock);
	tmp = krealloc(cpufreq_states,
		       sizeof(cpufreq_states[0]) * (next_offset + count),
		       GFP_ATOMIC);
	if (tmp) {
		cpufreq_states = tmp;
		cpufreq_for_each_entry(pos, table) {
			cpufreq_states[next_offset + pos - table] =
				pos->frequency;
		}
		times->offset = next_offset;
		next_offset += count;
	}
	spin_unlock(&cpufreq_times_lock);
	if (!tmp) {
		kfree(times->time_in_state);
		kfree(times);
		return -ENOMEM;
	}

	policy->times = times;
#ifdef CONFIG_CPU_FREQ_STAT
	ret = sysfs_merge_group(&policy->kobj, &times_attr_group);
#else
	ret = sysfs_create_group(&policy->kobj, &times_attr_group);
#endif
	if (!ret)
		return 0;

	/* We failed, release resources */
	policy->times = NULL;
	kfree(times->time_in_state);
	kfree(times);

	return ret;
}

void cpufreq_times_free_table(struct cpufreq_policy *policy)
{
	struct cpufreq_times *times = policy->times;

	if (!times)
		return;

#ifdef CONFIG_CPU_FREQ_STAT
	sysfs_unmerge_group(&policy->kobj, &times_attr_group);
#else
	sysfs_remove_group(&policy->kobj, &times_attr_group);
#endif
	kfree(times->time_in_state);
	kfree(times);
	policy->times = NULL;
}

void cpufreq_times_create_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return;

	cpufreq_times_create_table(policy);
	cpufreq_cpu_put(policy);
}

void cpufreq_task_times_remove_uids(uid_t uid_start, uid_t uid_end)
{
	struct uid_entry *uid_entry;
	struct hlist_node *tmp;

	rt_mutex_lock(&uid_lock);

	for (; uid_start <= uid_end; uid_start++) {
		hash_for_each_possible_safe(uid_hash_table, uid_entry, tmp,
			hash, uid_start) {
			if (uid_start == uid_entry->uid) {
				hash_del(&uid_entry->hash);
				kfree(uid_entry->dead_time_in_state);
				kfree(uid_entry);
			}
		}
	}

	rt_mutex_unlock(&uid_lock);
}

void cpufreq_times_record_transition(struct cpufreq_policy *policy,
				     struct cpufreq_freqs *freq)
{
	int index;
	struct cpufreq_times *times = policy->times;

	if (!times)
		return;

	index = cpufreq_frequency_table_get_index(policy, freq->new);
	if (index < 0)
		return;

	cpufreq_times_update(times);
	spin_lock(&cpufreq_times_lock);
	atomic_set(&times->last_index, index);
	spin_unlock(&cpufreq_times_lock);
}

static int uid_time_in_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_time_in_state_show, PDE_DATA(inode));
}

int single_uid_time_in_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, single_uid_time_in_state_show,
			&(inode->i_uid));
}

static const struct file_operations uid_time_in_state_fops = {
	.open		= uid_time_in_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init cpufreq_times_init(void)
{
	int ret;
	unsigned int cpu;

	for_each_online_cpu(cpu)
		cpufreq_times_create_policy(cpu);

	ret = sysfs_create_file(cpufreq_global_kobject,
			&_attr_all_time_in_state.attr);
	if (ret)
		pr_warn("Cannot create sysfs file for cpufreq stats\n");

	proc_create_data("uid_time_in_state", 0444, NULL,
			 &uid_time_in_state_fops, NULL);

	return 0;
}

static void __exit cpufreq_times_exit(void)
{
	unsigned int cpu;
	struct cpufreq_policy *policy;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		cpufreq_times_free_table(policy);
		cpufreq_cpu_put(policy);
	}
}

module_init(cpufreq_times_init);
module_exit(cpufreq_times_exit);
