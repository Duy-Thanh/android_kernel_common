#include <linux/cpuidle.h>
#include <linux/sched/cputime.h>
#include <trace/events/power.h>
#include <uapi/linux/sched/types.h>
#include <linux/module.h>
#include <linux/cpuset.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/stdarg.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include <uapi/linux/genetlink.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/percpu.h>
#include <asm/msr.h>

#define CREATE_TRACE_POINTS
#include "gpa_main.h"

/* Mandatory for the linker to find the symbols */
#undef TRACE_INCLUDE_PATH
/* Try the absolute path from kernel root if relative fails */
#define TRACE_INCLUDE_PATH . 
#define TRACE_INCLUDE_FILE gpa_main
#include <trace/define_trace.h>

#include <linux/pci.h>
#include <linux/io.h>

static void __iomem *gpu_mmio_base = NULL;
static struct pci_dev *gpu_pdev = NULL;
static u32 energy_unit_shift;
static u64 last_cpu_energy, last_gpu_energy, last_dram_energy, last_pkg_energy;
static u64 last_time_ns;
static unsigned int global_update = 0;
static unsigned int wc_flag;
static struct kobject *wc_kobj;
unsigned int update_period = UPDATE_PERIOD;

#define GT_PERF_STATUS 0x1381b4  /* Register offset for Aluminum/Tiger Lake */
#define CAGF_SHIFT     11
#define CAGF_MASK      0x1FF

#define CREATE_TRACE_POINTS
#define GPA_GENL_FAMILY_NAME "gpa"
#define GPA_GENL_VERSION 1
#define GPA_GENL_EVENT_GROUP_NAME "gpa_events"
#define GPA_GENL_ATTR_MAX 1
#define GPA_GENL_CMD_SEND_MESSAGE 1

static DEFINE_PER_CPU(struct cpu_msr_state, msr_snap);

static int init_procfs(void);
static void enable_fixed_counters_on_cpu(void *info);

static int init_gpu_mmio(void);
extern unsigned int cpu_khz;

static u32 last_rc6_ticks;
static u64 last_gpu_time_ns;
static u32 last_active_freq;


unsigned int check_gpa_enable(void)
{
        return wc_flag;

}
EXPORT_SYMBOL_GPL(check_gpa_enable);

unsigned int check_enable(void)
{
        return wc_flag;
}
EXPORT_SYMBOL_GPL(check_enable);

unsigned int get_cluster_id(unsigned int cpu)
{
        unsigned int clu_id;

        if(cpu < CPUL_NUM)
                clu_id = 0;
        else if (cpu < CPUL_NUM + CPUM_NUM)
                clu_id = 1;
        else
                clu_id = 2;

        return clu_id;
}


int wlc_init(void)
{
        init_procfs();
        on_each_cpu(enable_fixed_counters_on_cpu, NULL, 1);
        init_gpu_mmio();
        return 0;
}

static int init_gpu_mmio(void) {
        gpu_pdev = pci_get_device(0x8086, PCI_ANY_ID, NULL);

        while (gpu_pdev) {
                if ((gpu_pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
                        gpu_mmio_base = pci_iomap(gpu_pdev, 0, 0);
                        if (gpu_mmio_base) {
                                pr_info("GPA: Successfully mapped Intel GPU MMIO at BAR 0.\n");
                                return 0;
                        }
                }
                gpu_pdev = pci_get_device(0x8086, PCI_ANY_ID, gpu_pdev);
        }
        pr_err("GPA: Failed to find or map Intel GPU.\n");
        return -ENODEV;
}

#if 0
static void update_gpu_hardware_status_mmio(void) {
        struct msr_data *msr_data = &ms_data;
        u32 reg_val, raw_freq, act_freq;

        if (!gpu_mmio_base) return;

        reg_val = ioread32(gpu_mmio_base + GT_PERF_STATUS);

        raw_freq = (reg_val >> CAGF_SHIFT) & CAGF_MASK;

        act_freq = (raw_freq * 50) / 3;

        msr_data->gpu_freq = (u64)act_freq;

        pr_err("GPA: GPU MMIO Frequency %llu MHz\n", msr_data->gpu_freq);
}
#endif


static void update_gpu_hardware_status_mmio(void) {
        struct msr_data *msr_data = &ms_data;
        u32 reg_val, raw_freq, act_freq;
        u32 current_rc6_ticks, tick_delta;
        u64 cur_time_ns, delta_time_ns, delta_rc6_ns;
        u64 gpu_active_time_ns;

        if (!gpu_mmio_base) return;

        reg_val = ioread32(gpu_mmio_base + GT_PERF_STATUS);
        raw_freq = (reg_val >> CAGF_SHIFT) & CAGF_MASK;
        act_freq = (raw_freq * 50) / 3;
        if (act_freq > 0) {
                last_active_freq = act_freq;
        }
        current_rc6_ticks = ioread32(gpu_mmio_base + GT_RC6_RESIDENCY);
        cur_time_ns = ktime_get_ns();

        if (last_gpu_time_ns > 0) {
                delta_time_ns = cur_time_ns - last_gpu_time_ns;

                tick_delta = current_rc6_ticks - last_rc6_ticks;

                delta_rc6_ns = (u64)tick_delta * 1280;

                if (delta_rc6_ns > delta_time_ns) {
                        delta_rc6_ns = delta_time_ns;
                }

                gpu_active_time_ns = delta_time_ns - delta_rc6_ns;

                msr_data->gpu_util = div64_u64(gpu_active_time_ns * 100, delta_time_ns);

                msr_data->gpu_util = msr_data->gpu_util > 100 ? 100: msr_data->gpu_util;

                msr_data->gpu_freq = (u64)last_active_freq;

                msr_data->gpu_eff_freq = div64_u64((u64)last_active_freq * gpu_active_time_ns, delta_time_ns);

        }

        last_rc6_ticks = current_rc6_ticks;
        last_gpu_time_ns = cur_time_ns;
}

static void cleanup_gpu_mmio(void) {
        if (gpu_mmio_base) {
                pci_iounmap(gpu_pdev, gpu_mmio_base);
                gpu_mmio_base = NULL;
        }
        if (gpu_pdev) {
                pci_dev_put(gpu_pdev);
                gpu_pdev = NULL;
        }
}

void init_rapl_units(void) {
        u32 low, high;
        u64 units;

        if (!rdmsr_safe(MSR_RAPL_POWER_UNIT, &low, &high)) {
                units = (u64)low | ((u64)high << 32);
                energy_unit_shift = (units >> 8) & 0x1F;
        }
}



static void enable_fixed_counters_on_cpu(void *info) {
        wrmsrl_safe(0x38D, 0x3);
        wrmsrl_safe(0x38F, (1ULL << 32));
}



void update_power_stats(u64 *cpu_mw, u64 *gpu_mw, u64 *dram_mw, u64 *pkg_mw) {
        u32 low, high;
        u64 cur_cpu_en, cur_gpu_en, cur_dram_en, cur_pkg_en;
        u64 cur_time_ns = ktime_get_ns();
        u64 delta_time = cur_time_ns - last_time_ns;

        rdmsr_safe(MSR_PP0_ENERGY_STATUS, &low, &high);
        cur_cpu_en = (u64)low | ((u64)high << 32);

        rdmsr_safe(MSR_PP1_ENERGY_STATUS, &low, &high);
        cur_gpu_en = (u64)low | ((u64)high << 32);

        rdmsr_safe(MSR_DRAM_ENERGY_STATUS, &low, &high);
        cur_dram_en = (u64)low | ((u64)high << 32);

        rdmsr_safe(MSR_PKG_ENERGY_STATUS, &low, &high);
        cur_pkg_en = (u64)low | ((u64)high << 32);

        if (delta_time > 0 && last_time_ns > 0) {
                u64 d_cpu = cur_cpu_en - last_cpu_energy;
                u64 d_gpu = cur_gpu_en - last_gpu_energy;
                u64 d_dram = cur_dram_en - last_dram_energy;
                u64 d_pkg = cur_pkg_en - last_pkg_energy;

                *cpu_mw = div64_u64(((d_cpu * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
                *gpu_mw = div64_u64(((d_gpu * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
                *dram_mw = div64_u64(((d_dram * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
                *pkg_mw = div64_u64(((d_pkg * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
        }

        last_cpu_energy = cur_cpu_en;
        last_gpu_energy = cur_gpu_en;
        last_dram_energy = cur_dram_en;
        last_pkg_energy = cur_pkg_en;
        last_time_ns = cur_time_ns;
}

void update_msr_stats(int cpu) {
        u64 mperf = 0, aperf = 0, instr = 0;
        u64 d_mperf = 0, d_aperf = 0, d_instr = 0;
        u64 avg_mhz = 0, therm = 0, target;
        u64 tsc = 0, d_tsc = 0;
        u32 tj_max = 0, dts_offset = 0;
        u32 low = 0, high = 0;
        u32 cpu_freq = 0;
        struct msr_data *msr_data = &ms_data;
        struct cpu_msr_state *snap = per_cpu_ptr(&msr_snap, cpu);

        rdmsr_safe_on_cpu(cpu, MSR_IA32_APERF, &low, &high);
        aperf = ((u64)high << 32) | low;

        rdmsr_safe_on_cpu(cpu, MSR_IA32_MPERF, &low, &high);
        mperf = ((u64)high << 32) | low;
        rdmsr_safe_on_cpu(cpu, MSR_IA32_FIXED_CTR0, &low, &high);
        instr = (u64)low | ((u64)high << 32);
        tsc = rdtsc();

        d_mperf = mperf - snap->last_mperf;
        d_aperf = aperf - snap->last_aperf;
        d_instr = instr - snap->last_instr;
        d_tsc = tsc - snap->last_tsc;

        if (d_mperf > 0) {
            avg_mhz = (d_aperf * 2400) / d_mperf;
            msr_data->cpu_ipc[cpu] = d_instr * 100 / d_mperf;
        }

        if (d_tsc > 0)
                msr_data->cpu_utilization[cpu] = 100 * d_mperf / d_tsc;
        if (d_mperf > 0) {
            msr_data->cpu_ipc[cpu] = (d_instr * 100) / d_mperf;
        } else {
            msr_data->cpu_ipc[cpu] = 0;
        }
        if (!rdmsr_safe_on_cpu(cpu, MSR_IA32_TEMPERATURE_TARGET, &low, &high)) {
                target = (u64)low | ((u64)high << 32);
                tj_max = (target >> 16) & 0xFF;
        } else {
                tj_max = 100;
        }

        if (!rdmsr_safe_on_cpu(cpu, MSR_IA32_THERM_STATUS, &low, &high)) {
                therm = (u64)low | ((u64)high << 32);
                dts_offset = (therm >> 16) & 0x7F;
                msr_data->cpu_temperature[cpu] = tj_max - dts_offset;
        }

        if (d_mperf > 0) {
                msr_data->cpu_dvfs_freq[cpu] =
                    div64_u64((u64)cpu_khz * d_aperf, d_mperf);
        }
        cpu_freq = arch_freq_get_on_cpu(cpu);


        snap->last_mperf = mperf;
        snap->last_aperf = aperf;
        snap->last_instr = instr;
        snap->last_tsc = tsc;
        trace_gpa_msr_data(cpu, ms_data.cpu_utilization[cpu], ms_data.cpu_ipc[cpu],
                       ms_data.cpu_temperature[cpu],
                       msr_data->cpu_dvfs_freq[cpu],
                       ms_data.cpu_total_energy,
                       ms_data.gpu_total_energy, ms_data.dram_total_energy,
                       ms_data.total_energy,
                       ms_data.gpu_freq, ms_data.gpu_util,
                       ms_data.gpu_eff_freq);
}

void wlc_exec(void)
{
        struct msr_data *msr_data = &ms_data;
        u64 cur_t = 0, last_time = 0, time_dur = 0;
        u64 start_t = 0, end_t = 0;
        unsigned int cpuid;

        if (wc_flag == 1 && global_update == 0) {
                global_update = 1;
                cur_t = ktime_get_ns();
                time_dur = cur_t - last_time;

                if (time_dur > update_period) {
                        start_t = ktime_get_ns();
                        update_gpu_hardware_status_mmio();
                        update_power_stats(&msr_data->cpu_total_energy, &msr_data->gpu_total_energy,
                                           &msr_data->dram_total_energy,&msr_data->total_energy);
                        for_each_possible_cpu(cpuid)
                                update_msr_stats(cpuid);
                        end_t = ktime_get_ns();
                        start_t = ktime_get_ns();
                        last_time = cur_t;
                        time_dur = 0;

                }
                global_update = 0;
        }
}

static ssize_t MO_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
        int ret;

        ret  = sscanf(buf, "%u", &wc_flag);
        if(wc_flag == 0) {
                cleanup_gpu_mmio();
        }
        return count;
}

static ssize_t MO_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        return scnprintf(buf, PAGE_SIZE, "micro_observer_enable %u\n", wc_flag);
}


static struct kobj_attribute wc_attr_wcflag = __ATTR_RW_MODE(MO_enable, 0660);


static ssize_t get_system_index_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,size_t count)
{
        return count;
}
static ssize_t get_system_index_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct msr_data *msr_data = &ms_data;
        unsigned int count = 0;
        unsigned int cpuid = 0;

        for_each_possible_cpu(cpuid) {
                if (cpuid >= ALU_CPU_NUM)
                        break; /* Bounds safety check */

                count += scnprintf(buf + count, PAGE_SIZE - count,
                    "C%d_util %llu C%d_temp %llu C%d_IPC %llu C%d_freq %llu ",
                    cpuid, msr_data->cpu_utilization[cpuid],
                    cpuid, msr_data->cpu_temperature[cpuid],
                    cpuid, msr_data->cpu_ipc[cpuid],
                    cpuid, msr_data->cpu_dvfs_freq[cpuid]);
        }

        count += scnprintf(buf + count, PAGE_SIZE - count,
            "\nCPU_Pwr %llu GPU_Pwr %llu DRAM_Pwr %llu PKG_Pwr %llu GPU_Freq %llu GPU_Util %llu\n",
            msr_data->cpu_total_energy,
            msr_data->gpu_total_energy,
            msr_data->dram_total_energy,
            msr_data->total_energy,
            msr_data->gpu_freq,
            msr_data->gpu_util);

        return count;
}


static struct kobj_attribute wc_attr_get_system_index = __ATTR_RW_MODE(get_system_index, 0660);

static struct attribute *wc_attrs[] = {
        &wc_attr_wcflag.attr,
        &wc_attr_get_system_index.attr,
        NULL
};

static const struct attribute_group wc_attr_group = {
        .attrs = wc_attrs,
        .name = "config_setting"
};


static int init_procfs(void)
{
        wc_kobj = kobject_create_and_add("micro_observer", kernel_kobj);
        if (!wc_kobj) {
                pr_err("cannot create kobj for micro observer!");
                goto error;
        }
        if (sysfs_create_group(wc_kobj, &wc_attr_group)) {
                pr_err("cannot create files in ../micro_observer/micro_observer\n");
                kobject_put(wc_kobj);
                return -EINVAL;
        }
        pr_info("initialized!");
        return 0;
error:
        return -ENOMEM;
}


