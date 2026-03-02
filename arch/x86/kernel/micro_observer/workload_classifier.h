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

enum event_type {
        CPU_INST,
        CPU_CYC,
        CPU_IPC,
        CPU_STALL,
        CPU_STALL_RATIO,
        L3_MISS,
        L3_ACCESS,
        L3_MISS_RATIO,
        PMU_NUM,

};


struct pmu_event {
        unsigned long inst[CPU_NUM];
        unsigned long cyc[CPU_NUM];
        unsigned long cpu_freq[CPU_NUM];
        unsigned long l3_cachemiss[CPU_NUM];
        unsigned long stall_cnt[CPU_NUM];
        unsigned long stall_ratio[CPU_NUM];
};

struct wc_stats {
        u64 prev_wfreq[CPU_NUM];
        u64 prev_idle_time[CPU_NUM];
        u64 prev_wfreq_utime[CPU_NUM];
        u64 prev_idle_utime[CPU_NUM];
        u64 cpu_wfreq[CPU_NUM];
        unsigned long cpu_cap[CPU_NUM];
        unsigned long cpu_cap_arr[CPU_NUM][CAP_AVG_SIZE];
        u64 cpu_idle_time[CPU_NUM];
        u64 last_time;
        u64 time_dur;
        u64 idle_time_dur[CPU_NUM];
        u64 wfreq_time_dur[CPU_NUM];
        unsigned int idle_pct[CPU_NUM];
        unsigned long long wfreq_pct[CPU_NUM];
        unsigned int idle_over[CPU_NUM];
        unsigned int wfreq_over[CPU_NUM];
        unsigned int eff_freq[CPU_NUM];
        unsigned int eff_cap[CPU_NUM];
        bool update_flag;

};


struct model_input {
        uint64_t eff_cpu_freq_l;
        uint64_t eff_cpu_freq_m;
        uint64_t eff_cpu_freq_b;

        uint64_t eff_cpu_cap_l;
        uint64_t eff_cpu_cap_m;
        uint64_t eff_cpu_cap_b;
        uint64_t eff_cpu_cap;

        unsigned int prev_workload;
        unsigned int static_workload;

        uint64_t eff_gpu_freq;
        uint64_t gpu_freq;
        uint64_t gpu_freq_arr[CAP_AVG_SIZE];
        s64 gmc_freq;
        s64 memss_freq;
        uint64_t mcu_util;
        unsigned int gpu_util;
        unsigned int workload_cluster;
        unsigned int cpu_util[CPU_NUM];
        unsigned int cpu_util_arr[CPU_NUM][CAP_AVG_SIZE];

        unsigned int cap_start_idx;
        unsigned int cap_last_idx;
        int cap_avg_sum;
        int cap_var;
        unsigned int cap_avg_result;
        unsigned int cap_avg_arr[CAP_AVG_SIZE];

        unsigned int mif_start_idx;
        unsigned int mif_last_idx;
        int mif_avg_sum;
        int mif_var;
        unsigned int mif_avg_result;
        unsigned int mif_avg_arr[CAP_AVG_SIZE];


        unsigned int gpu_start_idx;
        unsigned int gpu_last_idx;
        int gpu_avg_sum;
        int gpu_var;
        unsigned int gpu_avg_result;
        unsigned int gpu_avg_arr[CAP_AVG_SIZE];



        unsigned int debounce_num;
        unsigned int debounce_cnt;
        unsigned int debounce_start;

        unsigned int burst_num;
        unsigned int burst_start;
        unsigned int burst_cnt;

        unsigned int pmu_start_idx[PMU_NUM];
        unsigned int pmu_last_idx[PMU_NUM];
        int pmu_avg_sum[PMU_NUM][CPU_NUM];
        int pmu_var[PMU_NUM][CPU_NUM];
        int final_pmu_var[PMU_NUM];
        int final_pmu_val[PMU_NUM];
        unsigned int pmu_avg_result;
        unsigned int pmu_avg_arr[PMU_NUM][CAP_AVG_SIZE];
        unsigned long event_val[PMU_NUM][CPU_NUM];
        unsigned long event_val_arr[PMU_NUM][CPU_NUM][CAP_AVG_SIZE];

        unsigned int inst;
        unsigned int cyc;
        unsigned int ipc;
        unsigned int stall;
        unsigned int stall_ratio;
        unsigned int l3_miss;

        int inst_var;
        int cyc_var;
        int ipc_var;
        int stall_var;
        int stall_ratio_var;
        int l3_miss_var;

        unsigned int tpu_freq;
        unsigned int tpu_freq_arr[CAP_AVG_SIZE];
        unsigned int tpu_freq_min;
        unsigned int tpu_freq_max;
        unsigned int tpu_util;
        unsigned int tpu_util_arr[CAP_AVG_SIZE];
        unsigned int tpu_ip_util;
        unsigned int tpu_core_util;
        unsigned int tpu_control_util;

        uint32_t tpu_freq_htable[TPU_OPP_NUM];
        uint64_t tpu_time_in_state[TPU_OPP_NUM];
        uint32_t tpu_time_dur[TPU_OPP_NUM];
        uint32_t tpu_htable_set_flag;
        uint32_t tpu_avg_freq;

        u64 nl_latency;
        u64 main_latency;
        u64 stats_latency;
        u64 classify_latency;

        /* hint info */
        long ADPF_DISABLE_TA_BOOST;
        long CAMERA_BACKEND_BOOST;
        long CAMERA_CAPTURE_CPU_THROTTLE;
        long CAMERA_GPU_STANDARD;
        long CAMERA_LAUNCH;
        long CAMERA_LAUNCH_EXTENDED;
        long CAMERA_STREAMING_HIGH;
        long CAMERA_STREAMING_STANDARD;
        long CAMERA_STREAMING_VIDEO_CALL;
        long CAMERA_VIDEO_RECORDING;
        long CAMERA_ZOOMING_BOOST;
        long CPU_LOAD_RESET;
        long DISPLAY_CHANGE;
        long DISPLAY_CHANGE_GPU;
        long DISPLAY_IDLE;
        long DISPLAY_INACTIVE;
        long DISPLAY_UPDATE_IMMINENT;
        long EXPENSIVE_RENDERING;
        long GAME;
        long GCA_CAMERA_SHOT_ALLCPU;
        long LAUNCH;
        long LAUNCH_EXTEND;
        long LAUNCH_GPU;
        long LAUNCH_PMU;
        long REFRESH_120FPS;
        long REFRESH_60FPS;

};

struct msr_data {
        u64 cpu_utilization[ALU_CPU_NUM];
        u64 cpu_temperature[ALU_CPU_NUM];
        u64 cpu_ipc[ALU_CPU_NUM];
        u64 cpu_total_energy;
        u64 gpu_total_energy;
        u64 total_energy;
        u64 dram_total_energy;
        u64 gpu_utilization;
};

struct mif_priv {
	u64 mif_last_update_time_ns;
        u64 mif_last_freq;
        u64 mif_weight_freq;
        u64 mif_freq_diff;
        u64 mif_wfreq_pct;
        u64 mif_prev_wfreq;
        u64 mif_freq_time_dur;
        u64 mif_freq_prev_utime;
        bool mif_wfreq_over;
        bool mif_update;
};


struct gpu_priv {
	u64 last_freq_update_time_ns;
	u64 last_util_update_time_ns;
	u64 last_mcu_util_update_time_ns;
	u64 gpu_last_util;
	u64 mcu_last_util;
        u64 gpu_last_freq;
        u64 gpu_weight_util;
        u64 mcu_weight_util;
        u64 gpu_weight_freq;
        u64 gpu_freq_diff;
        u64 gpu_util_diff;
        u64 mcu_util_diff;
        u64 gpu_wfreq_pct;
        u64 gpu_util_pct;
        u64 gpu_util_pct_arr[CAP_AVG_SIZE];
        u64 mcu_util_pct;
        u64 mcu_util_pct_arr[CAP_AVG_SIZE];
        u64 gpu_prev_wfreq;
        u64 gpu_prev_util;
        u64 mcu_prev_util;
        u64 gpu_freq_time_dur;
        u64 gpu_util_time_dur;
        u64 mcu_util_time_dur;
        u64 gpu_freq_prev_utime;
        u64 gpu_util_prev_utime;
        u64 mcu_util_prev_utime;
        u64 gpu_eff_freq;
        bool gpu_wfreq_over;
        bool gpu_util_over;
        bool mcu_util_over;
        bool gpu_update;
        bool gpu_util_update;
        bool mcu_util_update;

};

enum hint_type {
        CPU_FREQ,
        CPU_AFFINITY,
        CPU_EM,
        MIF_FREQ,
        CPU_UCLAMP,
        //CPU_PELT,
        TYPE_NUM,
};

enum group_type {
        TOP_APP,
        FOREGROUND,
        BACKGROUND,
        RESTRICTED,
        GROUP_NUM,
};

struct  cpu_freq_hint {
        unsigned int cpuid;
        unsigned int freq_min;
        unsigned int freq_max;
};

struct cpu_affinity_hint {
        unsigned int group_id;
        char cpuset[4];
};

struct cpu_uclamp_hint {
        enum uclamp_id cid;
        unsigned int vg;
        unsigned int val;
};

struct perf_hint {
        bool hint_flag[TYPE_NUM];
        bool bounce_flag;
        bool burst_flag;
        unsigned int pelt_val;
        struct cpu_freq_hint freq_hint[CLU_NUM];
        struct cpu_affinity_hint affinity_hint;
        struct cpu_uclamp_hint uclamp_hint;
        unsigned int mif_freq;
        char profile_name[20];
};

static int default_min_freq[CLU_NUM] = {820000, 357000, 700000};
static int default_max_freq[CLU_NUM] = {1950000, 2600000, 3105000};

static struct perf_hint ph_info[WORKLOAD_NUM] = {
        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
         },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 500},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 0},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },

        {.hint_flag = {0, 0, 0, 0, 0},
         .freq_hint = {{0, 820000, 1950000}, {4, 357000, 2600000}, {7, 700000, 3105000}},
         .affinity_hint = {TOP_APP, "0-7"},
         .uclamp_hint = {UCLAMP_MIN, 1, 1},
         .mif_freq = 421000,
         .bounce_flag = 0,
         .burst_flag = 0,
         .pelt_val = 1,
         .profile_name = "default",
        },
};

static struct model_input md_input = {
        .eff_cpu_freq_l = 0,
        .eff_cpu_freq_m = 0,
        .eff_cpu_freq_b = 0,
        .debounce_num = 5,
        .burst_num = 5,
        .pmu_start_idx = {0},
        .pmu_last_idx = {0},
        .pmu_avg_arr = {0},
        .pmu_var = {0},
        .event_val = {0},

};

static struct msr_data ms_data = {
        .cpu_utilization = {0},
        .cpu_temperature = {0},
        .cpu_total_energy = 0,
        .gpu_total_energy = 0,
};

static struct wc_stats cpu_stats = {
        .prev_wfreq = {0},
        .prev_idle_time = {0},
        .cpu_wfreq = {0},
        .cpu_idle_time = {0},
        .last_time = 0,
        .time_dur = 0,
        .idle_pct = {0},
        .idle_over = {0},
        .wfreq_over = {0},


};

static struct pmu_event pmu_stats = {
        .inst = {0},
        .cyc = {0},
        .cpu_freq = {0},
        .l3_cachemiss= {0},
        .stall_cnt = {0},
        .stall_ratio = {0},
};


static struct gpu_priv gpu_stats = {
        .last_freq_update_time_ns = 0,
        .last_util_update_time_ns = 0,
        .gpu_last_util = 0,
        .gpu_last_freq = 0,
        .gpu_weight_util = 0,
        .gpu_weight_freq = 0,
        .gpu_prev_wfreq = 0,
        .gpu_freq_time_dur = 0,
        .gpu_util_time_dur = 0,
        .gpu_freq_prev_utime = 0,
        .gpu_util_prev_utime = 0,
        .gpu_wfreq_over = 0
};

static struct mif_priv mif_stats = {0};
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

#if !defined(_TRACE_GPA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPA_H

#include <linux/tracepoint.h>

TRACE_EVENT(gpa_msr_data,
    TP_PROTO(int cpu, u64 util, u64 ipc, u64 temp, u64 cpu_mw, u64 gpu_mw, u64 dram_mw, u64 pkg_mw, u64 gpu_util),

    TP_ARGS(cpu, util, ipc, temp, cpu_mw, gpu_mw, dram_mw, pkg_mw, gpu_util),

    TP_STRUCT__entry(
        __field(int, cpu)
        __field(u64, util)
        __field(u64, ipc)
        __field(u64, temp)
        __field(u64, cpu_mw)
        __field(u64, gpu_mw)
        __field(u64, dram_mw)
        __field(u64, pkg_mw)
        __field(u64, gpu_util)
    ),

    TP_fast_assign(
        __entry->cpu = cpu;
        __entry->util = util;
        __entry->ipc = ipc;
        __entry->temp = temp;
        __entry->cpu_mw = cpu_mw;
        __entry->gpu_mw = gpu_mw;
        __entry->dram_mw = dram_mw;
        __entry->pkg_mw = pkg_mw;
        __entry->gpu_util = gpu_util;
    ),

    TP_printk("cpu=%d util=%llu ipc=%llu temp=%llu cpu_pwr=%llu gpu_pwr=%llu dram_pwr=%llu pkg_pwr=%llu gpu_util=%llu",
              __entry->cpu, __entry->util, __entry->ipc, __entry->temp,
              __entry->cpu_mw, __entry->gpu_mw, __entry->dram_mw, __entry->pkg_mw, __entry->gpu_util)
);

#endif




