/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WORKLOAD_CLASSIFIER_H
#define WORKLOAD_CLASSIFIER_H
#include<linux/cpuset.h>

#define MIN_FREQ 20
#define OPP_NUM 35
#define ACPM_DVFS_CPUCL0 (0x0B040002)
#define ACPM_DVFS_CPUCL1 (0x0B040003)
#define ACPM_DVFS_CPUCL2 (0x0B040004)

#define GPA_VERSION "2024_0926"
/* update time dur 20ms*/
#define UPDATE_PERIOD 20000000

#define GT_PERF_STATUS 0x1381b4  /* Register offset for Aluminum/Tiger Lake */
#define GT_RC6_RESIDENCY 0x138108 /* RC6 Sleep Counter */
#define CAGF_SHIFT     11
#define CAGF_MASK      0x1FF

//#define Workload_Classifier_ENABLE

#define CPUL_NUM 2
#define CPUM_NUM 5
#define CPUB_NUM 1

#define CPUL_ID 0
#define CPUM_ID 1
#define CPUB_ID 2

#define CLU_NUM 3
#define CPU_NUM 8

#define ALU_CPU_NUM 12

#define WORKLOAD_NUM 10
#define TOTAL_WORKLOAD_NUM 12
#define DEEP_IDLE_ID 10
#define BURST_ID 11
#define CAP_AVG_SIZE 1
#define MIN_CNT 10

#define TPU_OPP_NUM 20

#if 0
#define MSR_IA32_MPERF            0x000000E7
#define MSR_IA32_APERF            0x000000E8
#define MSR_IA32_FIXED_CTR0       0x00000309 /* Instructions Retired */
#define MSR_IA32_THERM_STATUS     0x0000019C /* Core Temp */
#define MSR_PKG_ENERGY_STATUS     0x00000611 /* Package Energy */
#define MSR_PP0_ENERGY_STATUS     0x00000639 /* Core Energy */
#define MSR_RAPL_POWER_UNIT       0x00000606 /* Energy Units */
#endif

#define MSR_IA32_FIXED_CTR0       0x00000309 /* Instructions Retired */

struct cpu_msr_state {
    u64 last_mperf;
    u64 last_aperf;
    u64 last_instr;
    u64 last_energy;
    u64 last_tsc;
};

struct task_group;
struct cgroup_subsys_state;
//extern ssize_t cpuset_write(char *buf, unsigned int idx);



/* PMU related */


#define to_cpu_data(cpu_grp, cpu) \
	(&cpu_grp->cpus_data[cpu - cpumask_first(&cpu_grp->cpus)])

static DEFINE_PER_CPU(bool, is_idle);
static DEFINE_PER_CPU(bool, is_on);
static DEFINE_PER_CPU(int, cpu_idle_state);

struct msr_data {
        u64 cpu_utilization[ALU_CPU_NUM];
        u64 cpu_temperature[ALU_CPU_NUM];
        u64 cpu_ipc[ALU_CPU_NUM];
        u64 cpu_dvfs_freq[ALU_CPU_NUM];
        u64 cpu_total_energy;
        u64 gpu_total_energy;
        u64 total_energy;
        u64 dram_total_energy;
        u64 gpu_freq;
        u64 gpu_util;
        u64 gpu_eff_freq;
};

static struct msr_data ms_data = {
        .cpu_utilization = {0},
        .cpu_temperature = {0},
        .cpu_total_energy = 0,
        .gpu_total_energy = 0,
};

#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

#if !defined(_TRACE_GPA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPA_H

#include <linux/tracepoint.h>

TRACE_EVENT(gpa_msr_data,
    TP_PROTO(int cpu, u64 util, u64 ipc, u64 temp, u64 cpu_freq, u64 cpu_mw, u64 gpu_mw, u64 dram_mw, u64 pkg_mw, u64 gpu_freq, u64 gpu_util, u64 gpu_eff_freq),

    TP_ARGS(cpu, util, ipc, temp, cpu_freq, cpu_mw, gpu_mw, dram_mw, pkg_mw, gpu_freq, gpu_util, gpu_eff_freq),

    TP_STRUCT__entry(
        __field(int, cpu)
        __field(u64, util)
        __field(u64, ipc)
        __field(u64, temp)
        __field(u64, cpu_freq)
        __field(u64, cpu_mw)
        __field(u64, gpu_mw)
        __field(u64, dram_mw)
        __field(u64, pkg_mw)
        __field(u64, gpu_freq)
        __field(u64, gpu_util)
        __field(u64, gpu_eff_freq)
    ),

    TP_fast_assign(
        __entry->cpu = cpu;
        __entry->util = util;
        __entry->ipc = ipc;
        __entry->temp = temp;
        __entry->cpu_freq = cpu_freq;
        __entry->cpu_mw = cpu_mw;
        __entry->gpu_mw = gpu_mw;
        __entry->dram_mw = dram_mw;
        __entry->pkg_mw = pkg_mw;
        __entry->gpu_freq = gpu_freq;
        __entry->gpu_util = gpu_util;
        __entry->gpu_eff_freq = gpu_eff_freq;
    ),

    TP_printk("cpu=%d util=%llu ipc=%llu temp=%llu cpu_freq %llu cpu_pwr=%llu gpu_pwr=%llu dram_pwr=%llu pkg_pwr=%llu gpu_freq=%llu gpu_util=%llu gpu_eff_freq=%llu \n",
              __entry->cpu, __entry->util, __entry->ipc, __entry->temp, __entry->cpu_freq,
              __entry->cpu_mw, __entry->gpu_mw, __entry->dram_mw, __entry->pkg_mw, __entry->gpu_freq,
              __entry->gpu_util, __entry->gpu_eff_freq)
);

#endif




