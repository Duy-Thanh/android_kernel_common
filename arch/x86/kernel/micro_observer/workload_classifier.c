#include <linux/cpuidle.h>
#include <linux/sched/cputime.h>
//#include <kernel/sched/autogroup.h>
//#include <kernel/sched/sched.h>
//#include <kernel/sched/pelt.h>
//#include <linux/moduleparam.h>
#include <trace/events/power.h>
//#include <trace/hooks/systrace.h>
#include <uapi/linux/sched/types.h>

//#include "sched_priv.h"
//#include "sched_events.h"
#include <linux/module.h>
#include <linux/cpuset.h>

#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/stdarg.h>

#include "workload_classifier.h"
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
#include "workload_classifier.h"

/* Mandatory for the linker to find the symbols */
#undef TRACE_INCLUDE_PATH
/* Try the absolute path from kernel root if relative fails */
#define TRACE_INCLUDE_PATH . 
#define TRACE_INCLUDE_FILE workload_classifier
#include <trace/define_trace.h>


#define CREATE_TRACE_POINTS
#define GPA_GENL_FAMILY_NAME "gpa"
#define GPA_GENL_VERSION 1
#define GPA_GENL_EVENT_GROUP_NAME "gpa_events"
#define GPA_GENL_ATTR_MAX 1
#define GPA_GENL_CMD_SEND_MESSAGE 1

static DEFINE_PER_CPU(struct cpu_msr_state, msr_snap);

unsigned int wlc_id;
u64 get_cpu_weight_freq(unsigned int cpu);
u64 get_cpu_idle(unsigned int cpu);
u64 get_idle_update(unsigned int cpu);
u64 get_wfreq_update(unsigned int cpu);
int workload_classifier_init(void);
unsigned int get_cluster_id(unsigned int cpu);
static unsigned int cnt;
static unsigned int cnt_arr[CAP_AVG_SIZE];
static unsigned int global_update = 0;

static u32 energy_unit_shift;
static u64 last_cpu_energy, last_gpu_energy, last_dram_energy, last_pkg_energy;
static u64 last_time_ns;


#define DEVFREQ_DSU			8
#define DEVFREQ_BCI			9
//#define ENABLE_WLC_HINT


extern int exynos_acpm_get_rate(unsigned int id, unsigned long dbg_val);
//extern unsigned int get_tpu_val(void);
//extern int gs_perf_mon_get_data(unsigned int cpu, struct gs_cpu_perf_data *data_dest);
//extern struct vendor_group_property *get_vendor_group_property(enum vendor_group group);
//extern void apply_uclamp_change(enum vendor_group group, enum uclamp_id clamp_id);
extern unsigned long exynos_devfreq_get_domain_freq(unsigned int devfreq_type);
extern const char *css_cs_ex(struct cgroup_subsys_state *css);
//extern void exynos_pm_qos_update_request(struct exynos_pm_qos_request *req, s32 new_value);

//extern int exynos_pm_qos_read_req_value(int pm_qos_class, struct exynos_pm_qos_request *req);


//extern int exynos_pm_qos_request_active(struct exynos_pm_qos_request *req);
#ifdef ENABLE_WLC_HINT
static void handle_perf_hint(struct perf_hint *ph, unsigned int workload_cluster);
#endif
//static void wlc_cnt_func(struct perf_hint *ph, unsigned int wlcid, bool debounce_flag, bool burst_flag);
int update_mov_avg_data(unsigned int *val_arr, unsigned int *idx, unsigned int new_val);
static void hint_convert(char *);
static void hint_clear(void);
static unsigned int get_start_idx(unsigned int last_idx);
static void enable_fixed_counters_on_cpu(void *info);

static unsigned int idle_cnt_flag = 1;
int init_opp_table(void);
static int init_procfs(void);
static struct kobject *wc_kobj;
static unsigned int wc_flag;
static unsigned int wlc_cnt_flag;
struct cgroup_subsys_state *css_ptr[9];
//static struct exynos_pm_qos_request wc_mif_qos;
#ifdef ENABLE_WLC_HINT
static struct work_type *wlc_wq = NULL;
#endif

unsigned int buf_idx;
unsigned int cal_id[CLU_NUM] = {ACPM_DVFS_CPUCL0, ACPM_DVFS_CPUCL1, ACPM_DVFS_CPUCL2};
unsigned int cap_table[CLU_NUM][OPP_NUM];
unsigned int freq_table[CLU_NUM][OPP_NUM];
unsigned int opp_size[CPU_NUM];
unsigned int last_modify_id;
unsigned int update_period = UPDATE_PERIOD;


struct acpu_stats {
        __u64 weighted_sum_freq;
        __u64 total_idle_time_ns;
};

struct freq_priv {
	u64 last_update_time_ns;
	u64 last_freq;
	u64 weighted_sum;
};

struct idle_priv {
	u64 last_update_time_ns;
	u64 current_pid;
	u64 total_idle_time_ns;
};

struct work_type {
    char *name;
    struct workqueue_struct *wq;
    struct work_struct wk;
};

static DEFINE_PER_CPU(struct idle_priv, idle_stats);
static DEFINE_PER_CPU(struct freq_priv, freq_stats);

#define TPU_ACPM_DOMAIN 9
#define TPU_DEBUG_VALUE_SHIFT (27)
#define TPU_DEBUG_REQ (1 << 31)
#define TPU_CLK_CORE_DEBUG (3 << TPU_DEBUG_VALUE_SHIFT)

char keys_storage[CPU_NUM * (PMU_NUM + 2)][32]; // Enough space for "CPUx_metric_name\0"
const char *keys[CPU_NUM * (PMU_NUM +2)];
unsigned long values[CPU_NUM * (PMU_NUM +2)];


enum gpa_genl_attr {
    GPA_GENL_ATTR_MESSAGE
};

static const struct nla_policy gpa_genl_policy[GPA_GENL_ATTR_MAX + 1] = {
    [GPA_GENL_ATTR_MESSAGE] = { .type = NLA_STRING },
};

// Multicast group definition
static const struct genl_multicast_group gpa_genl_mcgrps[] = {
    {
        .name = GPA_GENL_EVENT_GROUP_NAME,
    },
};

static const struct genl_small_ops gpa_genl_ops[] = {};

// Family definition
static struct genl_family gpa_gnl_family __ro_after_init = {
    .hdrsize = 0,
    .name = GPA_GENL_FAMILY_NAME,
    .version = GPA_GENL_VERSION,
    .maxattr = GPA_GENL_ATTR_MAX,
    .policy = gpa_genl_policy,
    .small_ops = gpa_genl_ops,
    .n_small_ops = ARRAY_SIZE(gpa_genl_ops),
    .mcgrps = gpa_genl_mcgrps,
    .n_mcgrps = ARRAY_SIZE(gpa_genl_mcgrps),
};

static unsigned int my_seq_num = 0;

char *create_json_string(const char *keys[], int count) {

    char temp[512];
    int len = 2; // "{" and "}"
    char *result = (char *)kmalloc(len + 1, GFP_KERNEL);

    strcpy(result, "{");

    for (int i = 0; i < count; i++) {
        sprintf(temp, "\"%s\":%lu", keys[i], values[i]);
        len += strlen(temp);
        if (i > 0) {
            len += 1; 
            char *temp_result = (char *)krealloc(result, len + 1, GFP_KERNEL);
            if (temp_result == NULL) {
                kfree(result);
                return NULL;
            }
            result = temp_result;
            strcat(result, ",");
        }

        char *temp_result = (char *)krealloc(result, len + 1, GFP_KERNEL);
        if (temp_result == NULL) {
            kfree(result);
            //va_end(args);
            return NULL;
        }
        result = temp_result;
        strcat(result, temp);
    }

    strcat(result, "}");

    return result;
}



static int gpa_genl_send_message(const char *message) {
    struct sk_buff *msg;
    void *hdr;
    int ret = -EMSGSIZE;
    ktime_t kt;
    u64 timestamp;

    // Get the timestamp in ms
    kt = ktime_get_ns();
    timestamp = ktime_to_ns(kt) / 1000000;

    //printk(KERN_INFO "GPA Kernel: Sending message, seq=%d, timestamp=%llu\n", my_seq_num, timestamp);

    msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
    if (!msg) {
        printk(KERN_ERR "GPA Failed to allocate skb\n");
        return -ENOMEM;
    }

    hdr = genlmsg_put(msg, 0, 0, &gpa_gnl_family, 0, GPA_GENL_CMD_SEND_MESSAGE);
    if (!hdr) {
        printk(KERN_ERR "GPA Failed to put genetlink header\n");
        goto out_free_msg;
    }

    if (nla_put_string(msg, GPA_GENL_ATTR_MESSAGE, message)) {
        printk(KERN_ERR "GPA Failed to put string attribute\n");
        goto out_cancel_msg;
    }

    genlmsg_end(msg, hdr);

    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    nlh->nlmsg_seq = my_seq_num++;

    ret = genlmsg_multicast(&gpa_gnl_family, msg, 0, 0, GFP_KERNEL);
    if (ret) {
        //printk(KERN_ERR "GPA multicast error: %d\n", ret);
        // Don't return error for ESRCH (no processes listening)
        if (ret == -ESRCH)
            return 0;
        return ret;
    }

    // Get the latency timestamp
    kt = ktime_get_ns();
    timestamp = ktime_to_ns(kt) / 1000000 - timestamp;

    // Print the latency
    printk(KERN_INFO "GPA Kernel: message sent, seq=%d, execution_time_ms=%llu\n", nlh->nlmsg_seq, timestamp);

    return 0;

out_cancel_msg:
    genlmsg_cancel(msg, hdr);
out_free_msg:
    nlmsg_free(msg);
    return -EMSGSIZE;
}
#if 0
static int gpa_genl_init(void) {
    int err;
    err = genl_register_family(&gpa_gnl_family);
    if (err) {
        pr_err("GPA Failed to register genetlink family\n");
        return err;
    }
    return 0;
}
#endif

void gpa_notify(void) {
        struct model_input *model_input = &md_input;
        struct wc_stats *wc_stats = &cpu_stats;
        const char *metric_names[] = {"INST","CYCLE","IPC", "STALL", "STALL_RATIO", "L3_MISS", "L3_ACCESS", "L3_MISS_RATIO", "util", "capacity"};
        //char keys_storage[CPU_NUM * METRICS_PER_CPU][32]; // Enough space for "CPUx_metric_name\0"
        //const char *keys[CPU_NUM * METRICS_PER_CPU];
        //unsigned long values[CPU_NUM * METRICS_PER_CPU];
        int current_key_idx = 0;
        int count = CPU_NUM * (PMU_NUM + 2);

        for (int i = 0; i < CPU_NUM; i++) {
                for (int pmuid = 0; pmuid < PMU_NUM; pmuid++) {
                        snprintf(keys_storage[current_key_idx], sizeof(keys_storage[current_key_idx]), "CPU%d_%s", i, metric_names[pmuid]);
                        keys[current_key_idx] = keys_storage[current_key_idx];
                        values[current_key_idx++] = model_input->event_val[pmuid][i];
                }
                snprintf(keys_storage[current_key_idx], sizeof(keys_storage[current_key_idx]),
                         "CPU%d_%s", i, metric_names[PMU_NUM]);
                keys[current_key_idx] = keys_storage[current_key_idx];
                values[current_key_idx++] = model_input->cpu_util[i];

        
                snprintf(keys_storage[current_key_idx], sizeof(keys_storage[current_key_idx]),
                         "CPU%d_%s", i, metric_names[PMU_NUM + 1]);
                keys[current_key_idx] = keys_storage[current_key_idx];
                values[current_key_idx++] = wc_stats->cpu_cap[i];

    }


    //char *message = NULL;
    char *message = create_json_string(keys, count); // Assuming create_json_string can take an array of values
    //pr_err("netlink msg %s\n", message);

    // limit the frequency the message is sent - device crash otherwise
    gpa_genl_send_message(message);
    // Free the message after sending if create_json_string allocates memory for it
    // free(message); 
}





//#define GPA_NETLINK_SYNC
#ifdef GPA_NETLINK_SYNC
static int pid = -1;
static struct sock *nl_sk;

#define NETLINK_TEST 26
#define MAX_MSGSIZE 512

int gpa_sendnlmsg(char *msg)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);
	//int len = strlen(msg);
	int ret = 0;

	if (!msg || !nl_sk || !pid)
		return -ENODEV;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 0, 0, len, 0);
	if (!nlh) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	NETLINK_CB(skb).portid = 0;
	NETLINK_CB(skb).dst_group = 0;

	memcpy(NLMSG_DATA(nlh), msg, strlen(msg));
	//pr_err("send message: %d\n", *(char *)NLMSG_DATA(nlh));
	pr_err("len %d send message: %s\n", len, (char *)NLMSG_DATA(nlh));

        //return 0;

	ret = netlink_unicast(nl_sk, skb, pid, MSG_DONTWAIT);
	if (ret > 0)
		ret = 0;

	return ret;
}

static void nl_data_ready(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char str[100];

	skb = skb_get(__skb);
	if (skb->len < NLMSG_SPACE(0))
		return;

	nlh = nlmsg_hdr(skb);

	memcpy(str, NLMSG_DATA(nlh), sizeof(str));
	pid = nlh->nlmsg_pid;

	kfree_skb(skb);
}

int gpa_netlink_init(void)
{
	struct netlink_kernel_cfg netlink_cfg;

	memset(&netlink_cfg, 0, sizeof(struct netlink_kernel_cfg));

	netlink_cfg.groups = 0;
	netlink_cfg.flags = 0;
	netlink_cfg.input = nl_data_ready;
	netlink_cfg.cb_mutex = NULL;

	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &netlink_cfg);

	if (!nl_sk) {
		pr_err("create netlink socket error\n");
		return 1;
	}

	return 0;
}

void netlink_exit(void)
{
	if (nl_sk != NULL) {
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}
}

#endif




#if 0
static void get_pmu_data(void)
{

        struct gs_cpu_perf_data perf_data;
        unsigned int cpuid = 0, ret = 0;
        unsigned int i = 0;
        struct model_input *model_input = &md_input;
        //unsigned int tpu_freq;

        //get_tpu_val();
        //tpu_freq = exynos_acpm_get_rate(TPU_ACPM_DOMAIN, TPU_DEBUG_REQ | TPU_CLK_CORE_DEBUG);
        //pr_err("!!wlc tpu_freq = %u\n", tpu_freq);
        for(i = 0; i < PMU_NUM; i++) {
                model_input->final_pmu_val[i] = 0;
                model_input->final_pmu_var[i] = 0;
        }
        for_each_possible_cpu(cpuid) {
                ret = gs_perf_mon_get_data(cpuid, &perf_data);
                if (ret)
                        return;

	        model_input->event_val[CPU_INST][cpuid] = perf_data.perf_ev_last_delta[PERF_INST_IDX];
	        model_input->event_val_arr[CPU_INST][cpuid][buf_idx] = perf_data.perf_ev_last_delta[PERF_INST_IDX];
                
                model_input->event_val[CPU_CYC][cpuid] = perf_data.perf_ev_last_delta[PERF_CYCLE_IDX];
                model_input->event_val_arr[CPU_CYC][cpuid][buf_idx] = perf_data.perf_ev_last_delta[PERF_CYCLE_IDX];

	        model_input->event_val[CPU_IPC][cpuid] = model_input->event_val[CPU_INST][cpuid] * 100 / model_input->event_val[CPU_CYC][cpuid];
	        model_input->event_val_arr[CPU_IPC][cpuid][buf_idx] = model_input->event_val[CPU_INST][cpuid] * 100 / model_input->event_val[CPU_CYC][cpuid];
	        model_input->event_val[CPU_STALL][cpuid] = perf_data.perf_ev_last_delta[PERF_STALL_BACKEND_MEM_IDX];
	        model_input->event_val_arr[CPU_STALL][cpuid][buf_idx] = perf_data.perf_ev_last_delta[PERF_STALL_BACKEND_MEM_IDX];

                model_input->event_val[L3_ACCESS][cpuid] = perf_data.perf_ev_last_delta[PERF_L3_CACHE_ACCESS_IDX];
                model_input->event_val_arr[L3_ACCESS][cpuid][buf_idx] = perf_data.perf_ev_last_delta[PERF_L3_CACHE_ACCESS_IDX];

                model_input->event_val[L3_MISS][cpuid] = perf_data.perf_ev_last_delta[PERF_L3_CACHE_MISS_IDX];
                model_input->event_val_arr[L3_MISS][cpuid][buf_idx] = perf_data.perf_ev_last_delta[PERF_L3_CACHE_MISS_IDX];

                model_input->event_val[L3_MISS_RATIO][cpuid] = model_input->event_val[L3_MISS][cpuid] * 100 / model_input->event_val[L3_ACCESS][cpuid];
                model_input->event_val_arr[L3_MISS_RATIO][cpuid][buf_idx] = model_input->event_val[L3_MISS][cpuid] * 100 / model_input->event_val[L3_ACCESS][cpuid];
                

                model_input->event_val[CPU_STALL_RATIO][cpuid] = model_input->event_val[CPU_STALL][cpuid] * 100 / model_input->event_val[CPU_CYC][cpuid];
                model_input->event_val_arr[CPU_STALL_RATIO][cpuid][buf_idx] = model_input->event_val[CPU_STALL][cpuid] * 100 / model_input->event_val[CPU_CYC][cpuid];
                model_input->event_val[CPU_STALL_RATIO][cpuid] =
                    model_input->event_val[CPU_STALL_RATIO][cpuid] > 100 ? 100 : model_input->event_val[CPU_STALL_RATIO][cpuid];

                model_input->event_val_arr[CPU_STALL_RATIO][cpuid][buf_idx] = model_input->event_val[CPU_STALL_RATIO][cpuid];

                model_input->event_val[L3_MISS_RATIO][cpuid] =
                    model_input->event_val[L3_MISS_RATIO][cpuid] > 100 ? 100 : model_input->event_val[L3_MISS_RATIO][cpuid];
                model_input->event_val_arr[L3_MISS_RATIO][cpuid][buf_idx] = model_input->event_val[L3_MISS_RATIO][cpuid];

        }
        unsigned int start_idx;
        start_idx = get_start_idx(buf_idx);
        for (int i = 0; i < CAP_AVG_SIZE; i++) {
                if(start_idx >= CAP_AVG_SIZE)
                        start_idx = 0;
                //pr_err("idx %d act_idx %d @@INST %lu\n", i, start_idx, model_input->event_val_arr[CPU_INST][0][start_idx]);
                start_idx++;

        }

        for_each_possible_cpu(cpuid)
                for(i = 0; i < PMU_NUM; i++)
                          model_input->final_pmu_val[i] += model_input->event_val[i][cpuid];

        for(i = 0; i < PMU_NUM; i++)
                model_input->final_pmu_var[i] = update_mov_avg_data((unsigned int *)model_input->pmu_avg_arr[i],
                                                            &model_input->pmu_last_idx[i], model_input->final_pmu_val[i]);

        model_input->inst = model_input->final_pmu_val[CPU_INST];
        model_input->inst_var = model_input->final_pmu_var[CPU_INST];
        model_input->cyc = model_input->final_pmu_val[CPU_CYC];
        model_input->cyc_var = model_input->final_pmu_var[CPU_CYC];
        model_input->ipc = model_input->final_pmu_val[CPU_IPC];
        model_input->ipc_var = model_input->final_pmu_var[CPU_IPC];
        model_input->stall = model_input->final_pmu_val[CPU_STALL];
        model_input->stall_var = model_input->final_pmu_var[CPU_STALL];
        model_input->stall_ratio = model_input->final_pmu_val[CPU_STALL_RATIO];
        model_input->stall_ratio_var = model_input->final_pmu_var[CPU_STALL_RATIO];
        model_input->l3_miss = model_input->final_pmu_val[L3_MISS];
        model_input->l3_miss_var = model_input->final_pmu_var[L3_MISS];
}
#endif
#if 0
unsigned int abs(int val)
{
        return (val + (val >> 31)) ^ (val >> 31);
}
#endif

unsigned int get_start_idx(unsigned int last_idx)
{
        unsigned int start_idx;
        
        if (last_idx >= (CAP_AVG_SIZE -1))
                start_idx = last_idx - (CAP_AVG_SIZE - 1);
        else
                start_idx = CAP_AVG_SIZE - abs(last_idx - (CAP_AVG_SIZE - 1));


        return start_idx;

}

#ifdef ENABLE_WLC_HINT
#define STATIC_NUM 4
unsigned int static_cnt;
unsigned int delta_detect(unsigned int cur_wl)
{
        unsigned int wl_change = 0;
        struct model_input *model_input = &md_input;
        if (cur_wl == model_input->prev_workload)
                static_cnt++;
        else
                static_cnt = 0;
        
        //pr_err("cur_wl %u prev_wl %u static_wl %u wl_cnt %u\n", cur_wl, model_input->prev_workload,
               //model_input->static_workload, static_cnt );

        
        if (static_cnt >= STATIC_NUM && cur_wl != model_input->static_workload) {
                model_input->static_workload = cur_wl;
                wl_change = 1;
                static_cnt = 0;
        }
        
        return wl_change;
}


u64 nl_time_dur;
u64 nl_last_time;
#define NL_UPDATE_PERIOD 100000000

static void hint_work(struct work_struct *work)
{
        unsigned int wlc_id = 0;
        struct model_input *model_input = &md_input;
        struct perf_hint *ph;
        unsigned int wl_change = 0;
        unsigned int nl_time_flag = 0;
        u64 cur_t = 0, start_t = 0, end_t = 0;
#ifdef GPA_NETLINK_SYNC
        char *msg = "kernel send msg";
        int ret;
#endif

        wlc_id = model_input->workload_cluster;
        ph = &ph_info[wlc_id];
        handle_perf_hint(ph, wlc_id);

        start_t = ktime_get_ns();

#ifdef GPA_NETLINK_SYNC
	ret = gpa_sendnlmsg(msg);
#endif
        wl_change = delta_detect(wlc_id);

        cur_t = ktime_get_ns();
        nl_time_dur = (cur_t - nl_last_time);
        
        if (nl_time_dur > NL_UPDATE_PERIOD)
                nl_time_flag = 1;

        //pr_err("curr_wl %d prev_wl %d time_flag %u wl_flag %u time %llu\n", wlc_id,
               //model_input->prev_workload, nl_time_flag, wl_change, nl_time_dur);

        if (wl_change && nl_time_flag) {
                //pr_err("update to user space\n");
                //gpa_notify();
                nl_last_time = cur_t;
                nl_time_dur = 0;
        }
        end_t = ktime_get_ns();
        model_input->nl_latency = (end_t - start_t)/1000;
         
        //pr_err("gpa send netlink ret %d\n", ret);

        model_input->prev_workload = wlc_id;
        //change_em_profile(buf);
}

static unsigned int check_burst(struct perf_hint *ph)
{
        struct model_input *md_ptr = &md_input;


        if (md_ptr->burst_start) {
                if (md_ptr->burst_cnt >= md_ptr->burst_num) {
                        md_ptr->burst_start = 0;
                        md_ptr->burst_cnt = 0;
                        return 0;
                } else {
                        md_ptr->burst_cnt++;
                        return 1;
                }
        }

        if (ph->burst_flag == 1 && md_ptr->debounce_start == 1) {
                md_ptr->burst_cnt ++;
                md_ptr->burst_start = 1;
                return 1;
        }

        return 0;
}

static unsigned int check_deep_wl(struct perf_hint *ph)
{
        struct model_input *md_ptr = &md_input;

        if(ph->bounce_flag) {
                md_ptr->debounce_cnt ++;
                if (md_ptr->debounce_cnt >= md_ptr->debounce_num) {
                        md_ptr->debounce_start = 1;
                        return 1;
                } else {
                        md_ptr->debounce_start = 0;
                        return 0;
                }
        } else {
                md_ptr->debounce_cnt = 0;
                md_ptr->debounce_start = 0;
                return 1;
        }

}

static unsigned int send_cpu_burst_hint(struct perf_hint *ph, unsigned int cpu_clu, bool burst_flag)
{
        struct cpufreq_policy *policy = NULL;
        unsigned int cpuid = ph->freq_hint[cpu_clu].cpuid;
        unsigned int cur_freq;
        unsigned int i = 0;
        int idx = 0;


        if (burst_flag == 0 && ph->burst_flag)
                return 1;
        policy = cpufreq_cpu_get(cpuid);
        if (ph->freq_hint[cpu_clu].freq_min < MIN_FREQ && ph->freq_hint[cpu_clu].freq_min >= 0) {
                cur_freq = policy->cur / 1000;
                for (i = 0; i < opp_size[cpuid]; i++) {
                        idx = i;
                        if (freq_table[cpu_clu][i] >= cur_freq) {
                                idx += ph->freq_hint[cpu_clu].freq_min;
                                idx = idx >= opp_size[cpuid] ? opp_size[cpuid]-1 : idx;
                                //pr_err("clu %d cpu %d cur_freq %d tarrget_freq %d\n", cpu_clu, cpuid, cur_freq, freq_table[cpu_clu][idx]);
                                freq_qos_update_request(policy->min_freq_req, freq_table[cpu_clu][idx] * 1000);
                                if (ph->freq_hint[cpu_clu].freq_max > MIN_FREQ)
                                        freq_qos_update_request(policy->max_freq_req, ph->freq_hint[cpu_clu].freq_max);
                                cpufreq_cpu_put(policy);
                                return 1;
                        }
                }

        }
        cpufreq_cpu_put(policy);
        return 0;

}


static void send_mif_freq_hint(struct perf_hint *ph)
{

#if 0
	unsigned int min_freq = ph->mif_freq;
        unsigned int cur_min_freq;
	if (!exynos_pm_qos_request_active(&wc_mif_qos))
                return;
        cur_min_freq = exynos_pm_qos_read_req_value(PM_QOS_BUS_THROUGHPUT, &wc_mif_qos);

        if (cur_min_freq != min_freq)
	        exynos_pm_qos_update_request(&wc_mif_qos, min_freq);
#endif 
        return;

}

static void send_cpu_freq_hint(struct perf_hint *ph, unsigned int cpu_clu, unsigned int workload_cluster, bool burst_flag, bool debounce_flag)
{
        struct cpufreq_policy *policy = NULL;

        if (send_cpu_burst_hint(ph, cpu_clu, burst_flag))
                return;
        policy = cpufreq_cpu_get(ph->freq_hint[cpu_clu].cpuid);
        if (ph->freq_hint[cpu_clu].freq_min && debounce_flag)
                freq_qos_update_request(policy->min_freq_req, ph->freq_hint[cpu_clu].freq_min);

        if (ph->freq_hint[cpu_clu].freq_max && debounce_flag)
                freq_qos_update_request(policy->max_freq_req, ph->freq_hint[cpu_clu].freq_max);
	cpufreq_cpu_put(policy);

        return;

}


static void send_cpu_uclamp_hint(struct perf_hint *ph)
{
        int val = ph->uclamp_hint.val;
        enum uclamp_id cid = ph->uclamp_hint.cid;
        enum vendor_group vg = ph->uclamp_hint.vg;
        struct vendor_group_property *gp = get_vendor_group_property(vg);

        gp->auto_uclamp_max = false;
        gp->uc_req[cid].value = val;
        gp->uc_req[cid].bucket_id = get_bucket_id(val);
        gp->uc_req[cid].user_defined = false;
        //apply_uclamp_change(vg, cid);
}

unsigned uclamp_min_orig[VG_MAX];
unsigned uclamp_max_orig[VG_MAX];
static void record_cpu_uclamp_hint(void)
{
        enum uclamp_id cid;
        enum vendor_group vg;
        struct vendor_group_property *gp;
        unsigned int i;

        for (i = 0; i < VG_MAX; i++) {

                cid = UCLAMP_MIN;
                vg = i;
                gp = get_vendor_group_property(vg);
                uclamp_min_orig[i] = gp->uc_req[cid].value;
                cid = UCLAMP_MAX;
                uclamp_max_orig[i] = gp->uc_req[cid].value;
        }

}

static void clear_cpu_uclamp_hint(void)
{
        enum uclamp_id cid;
        enum vendor_group vg;
        struct vendor_group_property *gp;
        unsigned int i;

        for (i = 0; i < VG_MAX; i++) {

                cid = UCLAMP_MIN;
                vg = i;
                gp = get_vendor_group_property(vg);
                gp->auto_uclamp_max = false;
                gp->uc_req[cid].value = 1;
                gp->uc_req[cid].bucket_id = get_bucket_id(1);
                gp->uc_req[cid].user_defined = false;
                //apply_uclamp_change(vg, cid);

                cid = UCLAMP_MAX;
                gp->auto_uclamp_max = false;
                gp->uc_req[cid].value = 1024;
                gp->uc_req[cid].bucket_id = get_bucket_id(1);
                gp->uc_req[cid].user_defined = false;
                //apply_uclamp_change(vg, cid);
        }

}



//#ifdef ENABLE_WLC_HINT
static void send_cpu_affinity_hint(struct perf_hint *ph)
{
        unsigned int idx = 0;
        struct model_input *model_input = &md_input;
        char cpu_set[10];

        if (ph->bounce_flag && model_input->debounce_cnt < model_input->debounce_num)
                return;

        if (ph->burst_flag && !model_input->burst_start)
                return;

        idx = ph->affinity_hint.group_id;
        strcpy(cpu_set, ph->affinity_hint.cpuset);
        //cpuset_write(cpu_set, idx);

        return;
}
#endif

static unsigned int wlc_counter[TOTAL_WORKLOAD_NUM];
static unsigned int total_wlc_cnt;
#if 0
static void wlc_cnt_func(struct perf_hint *ph, unsigned int wlcid, bool debounce_flag, bool burst_flag)
{
        //struct model_input *model_input = &md_input;

        if (debounce_flag && ph->bounce_flag)
                wlc_counter[DEEP_IDLE_ID]++;
        else if (burst_flag && ph->burst_flag)
                wlc_counter[BURST_ID]++;
        else
                wlc_counter[wlcid]++;

        //pr_err("wlc_id = %d debounce_flag %d %d burst %d %d prev_wl %d burst_cnt %d debounce_cnt %d burst_start %d de_start %d\n", wlcid, debounce_flag, ph->bounce_flag, burst_flag, ph->burst_flag, model_input->prev_workload, model_input->burst_cnt, model_input->debounce_cnt, model_input->burst_start, model_input->debounce_start);
}
#endif

#ifdef ENABLE_WLC_HINT
static void send_cpu_em_hint(void)
{
        return;

}

static void handle_perf_hint(struct perf_hint *ph, unsigned int workload_cluster)
{
        unsigned int i = 0;
        unsigned int cpu_clu;
        bool burst_flag = 0;
        bool debounce_flag = 0;


        for(i = 0; i < TYPE_NUM; i++) {
                if (i == CPU_FREQ && ph->hint_flag[i]) {
                        burst_flag = check_burst(ph);
                        debounce_flag = check_deep_wl(ph);
                        for (cpu_clu = 0; cpu_clu < CLU_NUM; cpu_clu++)
                                send_cpu_freq_hint(ph, cpu_clu, workload_cluster, burst_flag, debounce_flag);
                }
                if (i == CPU_AFFINITY && ph->hint_flag[i])
                        send_cpu_affinity_hint(ph);
                if (i == CPU_EM && ph->hint_flag[i])
                        send_cpu_em_hint();
                if (i == MIF_FREQ && ph->hint_flag[i])
                        send_mif_freq_hint(ph);
                if (i == CPU_UCLAMP && ph->hint_flag[i])
                        send_cpu_uclamp_hint(ph);
                /* if (i == CPU_PELT && ph->hint_flag[i])
                        change_pelt_multiplier(ph->pelt_val, workload_cluster);
                */
        }

        if (wlc_cnt_flag)
                wlc_cnt_func(ph, workload_cluster, debounce_flag, burst_flag);
}
#endif

char *get_model_id(void)
{
        struct model_input *model_input = &md_input;
        struct perf_hint *ph;

        if (model_input->workload_cluster >= WORKLOAD_NUM) {
                pr_err("wrong wlc num %d\n", model_input->workload_cluster);
                return "NULL";
        }
        ph = &ph_info[model_input->workload_cluster];
        if (ph->hint_flag[CPU_EM] == 0 || !wc_flag)
                return "NULL";

        return ph->profile_name;
}
EXPORT_SYMBOL_GPL(get_model_id);


unsigned int get_mif_freq(s64 gmc_freq, s64 memss_freq)
{

        struct model_input *model_input = &md_input;
	u64 t = ktime_get_ns();
	u64 delta = t - mif_stats.mif_last_update_time_ns;
        gmc_freq /= 1000; //to kHz
        memss_freq /= 1000; //to kHz

	WRITE_ONCE(mif_stats.mif_last_update_time_ns, t);
	WRITE_ONCE(mif_stats.mif_weight_freq, mif_stats.mif_weight_freq + delta * mif_stats.mif_last_freq);
	WRITE_ONCE(mif_stats.mif_last_freq, gmc_freq);

        //pr_err("@@In %s memss_freq %llu\n", __func__, mif_freq);
        model_input->gmc_freq = gmc_freq;
        model_input->memss_freq = memss_freq;

        return gmc_freq;

}
EXPORT_SYMBOL_GPL(get_mif_freq);

unsigned int check_gpa_enable(void)
{
        return wc_flag;

}
EXPORT_SYMBOL_GPL(check_gpa_enable);



unsigned int get_tpu_htable(uint32_t state, int32_t freq, uint32_t time_dur, uint64_t time_in_state)
{
        struct model_input *model_input = &md_input;
        
        model_input->tpu_freq_htable[state] = freq;
        model_input->tpu_time_in_state[state] = time_in_state;
        model_input->tpu_time_dur[state] = time_dur;
        model_input->tpu_htable_set_flag = 1;
        //pr_err("@@tpu_freq %u state %u time_dur %u time_in_state %llu\n", freq,  state, time_dur, time_in_state);

        return 0;

}
EXPORT_SYMBOL_GPL(get_tpu_htable);

void calc_tpu_wl(void)
{
        struct model_input *model_input = &md_input;

        model_input->tpu_avg_freq = model_input->tpu_freq * model_input->tpu_util / 100;

        return;
}


void calc_tpu_wfreq(void)
{
        struct model_input *model_input = &md_input;
        unsigned long long wfreq_sum = 0;
        unsigned int i = 0;
        uint64_t total_time = 0;
        
        for(i = 0; i < TPU_OPP_NUM; i++) {
                wfreq_sum += model_input->tpu_time_dur[i] * model_input->tpu_freq_htable[i];
                total_time += model_input->tpu_time_dur[i];

        }

        if (total_time > 0)
                model_input->tpu_avg_freq = (wfreq_sum / total_time) * model_input->tpu_util / 100; 
        else if (model_input->tpu_freq)
                model_input->tpu_avg_freq = model_input->tpu_freq * model_input->tpu_util / 100;
        else
                model_input->tpu_avg_freq = 0;
}


unsigned int clear_tpu_htable(void)
{
        struct model_input *model_input = &md_input;
        unsigned int i = 0;
        uint64_t total_time = 0;


        if (!model_input->tpu_htable_set_flag)
                return 0;

        for(i = 0; i < TPU_OPP_NUM; i++)
                total_time += model_input->tpu_time_dur[i];
              

        for(i = 0; i < TPU_OPP_NUM; i++) {
#if 0
                if (cnt % 1000 ==0)
                        pr_err("!!Sum cnt %d  freq %u time %llu\n", cnt,
                              model_input->tpu_freq_htable[i],

                              model_input->tpu_time_in_state[i]);
#endif
                model_input->tpu_time_dur[i] = 0;
        }
        //pr_err("!!clear cnt %d total_time %llu\n", cnt, total_time);
        model_input->tpu_htable_set_flag = 0;


        return 0;
}

unsigned int get_tpu_freq(unsigned int tpu_freq)
{

        struct model_input *model_input = &md_input;

        //pr_err("In %s tpu freq %u\n", __func__, tpu_freq);
        model_input->tpu_freq = tpu_freq;
        model_input->tpu_freq_arr[buf_idx] = tpu_freq;
        calc_tpu_wl();


        return tpu_freq;

}
EXPORT_SYMBOL_GPL(get_tpu_freq);

void get_tpu_min_max(unsigned int tpu_freq_min, unsigned int tpu_freq_max)
{
        struct model_input *model_input = &md_input;

        model_input->tpu_freq_min = tpu_freq_min;
        model_input->tpu_freq_max = tpu_freq_max;


}
EXPORT_SYMBOL_GPL(get_tpu_min_max);



unsigned int check_enable(void)
{
        return wc_flag;
}
EXPORT_SYMBOL_GPL(check_enable);



unsigned int get_tpu_util(unsigned int sub_id, unsigned int tpu_ip, unsigned tpu_core, unsigned int tpu_control)
{

        struct model_input *model_input = &md_input;


        //pr_err("In %s tmp_util %u\n", __func__, tpu_util);
        model_input->tpu_util = tpu_core;
        //model_input->tpu_util_arr[buf_idx] = tpu_util;

        model_input->tpu_ip_util = tpu_ip;
        model_input->tpu_core_util = tpu_core;
        model_input->tpu_control_util = tpu_control;
        calc_tpu_wl();


        return tpu_core;

}
EXPORT_SYMBOL_GPL(get_tpu_util);



unsigned int get_gpu_freq(unsigned int gpu_freq)
{
        struct model_input *model_input = &md_input;
#if 0
	u64 t = ktime_get_ns();
	u64 delta = t - gpu_stats.last_freq_update_time_ns;
	WRITE_ONCE(gpu_stats.last_freq_update_time_ns, t);
	WRITE_ONCE(gpu_stats.gpu_weight_freq, gpu_stats.gpu_weight_freq + delta * gpu_stats.gpu_last_freq);
	WRITE_ONCE(gpu_stats.gpu_last_freq, gpu_freq);
#endif
        model_input->gpu_freq = gpu_freq / 1000;
        model_input->gpu_freq_arr[buf_idx] = gpu_freq / 1000;

        return gpu_freq;
}
EXPORT_SYMBOL_GPL(get_gpu_freq);

unsigned int get_gpu_util(unsigned int gpu_util)
{
        struct model_input *model_input = &md_input;
#if 0
	u64 t = ktime_get_ns();
	u64 delta = t - gpu_stats.last_util_update_time_ns;

	WRITE_ONCE(gpu_stats.last_util_update_time_ns, t);
	WRITE_ONCE(gpu_stats.gpu_weight_util, gpu_stats.gpu_weight_util + delta * gpu_stats.gpu_last_util);
	WRITE_ONCE(gpu_stats.gpu_last_util, gpu_util);
#endif   
        model_input->gpu_util = gpu_util;

        return gpu_util;
}
EXPORT_SYMBOL_GPL(get_gpu_util);

unsigned int get_mcu_util(unsigned int mcu_util)
{
	u64 t = ktime_get_ns();
	u64 delta = t - gpu_stats.last_mcu_util_update_time_ns;


	WRITE_ONCE(gpu_stats.last_mcu_util_update_time_ns, t);
	WRITE_ONCE(gpu_stats.mcu_weight_util, gpu_stats.mcu_weight_util + delta * gpu_stats.mcu_last_util);
	WRITE_ONCE(gpu_stats.mcu_last_util, mcu_util);

        return mcu_util;
}
EXPORT_SYMBOL_GPL(get_mcu_util);


#if 0
static void sched_switch_cb(void *data, bool preempt, struct task_struct *prev,
			    struct task_struct *next, unsigned int prev_state)
{
	struct idle_priv *stats = this_cpu_ptr(&idle_stats);
	u64 t = ktime_get_ns();
	u64 delta = t - stats->last_update_time_ns;

	WRITE_ONCE(stats->current_pid, (u64)next->pid);
	WRITE_ONCE(stats->last_update_time_ns, t);
	if (!prev->pid) {
		WRITE_ONCE(stats->total_idle_time_ns, stats->total_idle_time_ns + delta);
        }
}

/* #define GET_DSU 1 */
static void cpu_frequency_cb(void *data, unsigned int freq, unsigned int cpu)
{
	struct freq_priv *stats = per_cpu_ptr(&freq_stats, cpu);
	u64 t = ktime_get_ns();
	u64 delta = t - stats->last_update_time_ns;
#ifdef GET_DSU
        unsigned long dsu_freq, bci_freq;
#endif
	WRITE_ONCE(stats->last_update_time_ns, t);
	WRITE_ONCE(stats->weighted_sum, stats->weighted_sum + delta * stats->last_freq);
	WRITE_ONCE(stats->last_freq, freq);

#ifdef GET_DSU
        /* DSU BCI */
        dsu_freq = exynos_devfreq_get_domain_freq(DEVFREQ_DSU);
        bci_freq = exynos_devfreq_get_domain_freq(DEVFREQ_BCI);
#endif
}
#endif


u64 get_cpu_weight_freq(unsigned int cpu)
{
        unsigned long long weight_freq;
	struct freq_priv *stats = per_cpu_ptr(&freq_stats, cpu);

        weight_freq = stats->weighted_sum;
        return weight_freq;
}

u64 get_cpu_idle(unsigned int cpu)
{
        unsigned long long idle_time;
	struct idle_priv *stats = per_cpu_ptr(&idle_stats, cpu);

        idle_time = stats->total_idle_time_ns;
        return idle_time;
}


u64 get_wfreq_update(unsigned int cpu)
{
        unsigned long long update_time;
	struct freq_priv *stats = per_cpu_ptr(&freq_stats, cpu);

        update_time = stats->last_update_time_ns;
        return update_time;
}
EXPORT_SYMBOL(get_wfreq_update);

u64 get_idle_update(unsigned int cpu)
{
        unsigned long long update_time;
	struct idle_priv *stats = per_cpu_ptr(&idle_stats, cpu);

        update_time = stats->last_update_time_ns;
        return update_time;
}
EXPORT_SYMBOL(get_idle_update);

#if 0
int register_trace_node(void)
{
	int ret;

	ret = register_trace_sched_switch(sched_switch_cb, NULL);
	if (ret)
		return ret;

	ret = register_trace_cpu_frequency(cpu_frequency_cb, NULL);
	if (ret)
		goto fail_tp;

	return 0;

fail_tp:
	unregister_trace_sched_switch(sched_switch_cb, NULL);

	return ret;
}
#endif



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


int update_mov_avg_data(unsigned int *val_arr, unsigned int *idx, unsigned int new_val)
{
        unsigned int val = 0;
        unsigned int i;
        int avg_sum = 0;
        int avg_val = 0;
        int avg_var = 0;

        val = val_arr[*idx];
        for(i = 0; i < CAP_AVG_SIZE; i++)
                avg_sum += val_arr[i];
        avg_sum = avg_sum - val + new_val;
        val_arr[*idx] = new_val;
        *idx = (*idx + 1 ) % CAP_AVG_SIZE;
        avg_val = avg_sum / CAP_AVG_SIZE;
        avg_var = new_val - avg_val;

        return avg_var;
}

void assign_to_model(unsigned int clu_id, unsigned long long eff_freq, unsigned long long eff_cap)
{
        struct model_input *model_input = &md_input;

        if (clu_id == CPUL_ID) {
                model_input->eff_cpu_freq_l = eff_freq;
                model_input->eff_cpu_cap_l = eff_cap;
        } else if (clu_id == CPUM_ID) {
                model_input->eff_cpu_freq_m = eff_freq;
                model_input->eff_cpu_cap_m = eff_cap;
        } else {
                model_input->eff_cpu_freq_b = eff_freq;
                model_input->eff_cpu_cap_b = eff_cap;
        }
}

unsigned long long get_cpu_capacity(unsigned int cpu, unsigned freq)
{

        unsigned int i = 0;
        unsigned int cap = 0;
        unsigned int clu_id = get_cluster_id(cpu);

        for (i = 0; i < opp_size[cpu]; i++) {
                if (freq_table[clu_id][i] >= freq) {
                        cap = cap_table[clu_id][i];
                        break;
                }
        }
        return cap;
}

void get_mif_system_index(void)
{
        struct mif_priv *mif_ptr = &mif_stats;
        u64 mif_freq_utime;
        u64 mif_wfreq;

        mif_freq_utime = mif_ptr->mif_last_update_time_ns;
        mif_ptr->mif_freq_time_dur = mif_freq_utime - mif_ptr->mif_freq_prev_utime;
        mif_wfreq = mif_ptr->mif_weight_freq;

        if (mif_wfreq >= mif_ptr->mif_prev_wfreq) {
                mif_ptr->mif_freq_diff = mif_wfreq - mif_ptr->mif_prev_wfreq;
                mif_ptr->mif_wfreq_over = 0;
                mif_ptr->mif_update = 1;
        } else {
                mif_ptr->mif_wfreq_over = 1;
                mif_ptr->mif_update = 0;
        }

        if (mif_ptr->mif_update) {
                mif_ptr->mif_wfreq_pct = mif_ptr->mif_freq_diff / mif_ptr->mif_freq_time_dur / 1000;
                mif_ptr->mif_prev_wfreq = mif_wfreq;
                mif_ptr->mif_freq_prev_utime = mif_freq_utime;
        }
}



void get_gpu_system_index(void)
{
        struct gpu_priv *gpu_ptr = &gpu_stats;
        u64 gpu_freq_utime;
        u64 gpu_util_utime;
        u64 mcu_util_utime;
        u64 gpu_wfreq;
        u64 gpu_util;
        u64 mcu_util;

        gpu_freq_utime = gpu_ptr->last_freq_update_time_ns;
        gpu_util_utime = gpu_ptr->last_util_update_time_ns;
        mcu_util_utime = gpu_ptr->last_mcu_util_update_time_ns;

        gpu_ptr->gpu_freq_time_dur = gpu_freq_utime - gpu_ptr->gpu_freq_prev_utime;
        gpu_ptr->gpu_util_time_dur = gpu_util_utime - gpu_ptr->gpu_util_prev_utime;
        gpu_ptr->mcu_util_time_dur = mcu_util_utime - gpu_ptr->mcu_util_prev_utime;

        gpu_wfreq = gpu_ptr->gpu_weight_freq;
        gpu_util = gpu_ptr->gpu_weight_util;
        mcu_util = gpu_ptr->mcu_weight_util;

        if (gpu_wfreq >= gpu_ptr->gpu_prev_wfreq) {
                gpu_ptr->gpu_freq_diff = gpu_wfreq - gpu_ptr->gpu_prev_wfreq;
                gpu_ptr->gpu_wfreq_over = 0;
                gpu_ptr->gpu_update = 1;
        } else {
                gpu_ptr->gpu_wfreq_over = 1;
                gpu_ptr->gpu_update = 0;
        }


        if (gpu_util >= gpu_ptr->gpu_prev_util) {
                gpu_ptr->gpu_util_diff = gpu_util - gpu_ptr->gpu_prev_util;
                gpu_ptr->gpu_util_over = 0;
                gpu_ptr->gpu_util_update = 1;
        } else {
                gpu_ptr->gpu_util_over = 1;
                gpu_ptr->gpu_util_update = 0;
        }

        if (mcu_util >= gpu_ptr->mcu_prev_util) {
                gpu_ptr->mcu_util_diff = mcu_util - gpu_ptr->mcu_prev_util;
                gpu_ptr->mcu_util_over = 0;
                gpu_ptr->mcu_util_update = 1;
        } else {
                gpu_ptr->mcu_util_over = 1;
                gpu_ptr->mcu_util_update = 0;
        }


        if (gpu_ptr->gpu_update) {
                gpu_ptr->gpu_wfreq_pct = gpu_ptr->gpu_freq_diff / gpu_ptr->gpu_freq_time_dur / 1000;
                gpu_ptr->gpu_prev_wfreq = gpu_wfreq;
                gpu_ptr->gpu_freq_prev_utime = gpu_freq_utime;
        }

        if (gpu_ptr->gpu_util_update) {
                gpu_ptr->gpu_util_pct = gpu_ptr->gpu_util_diff / gpu_ptr->gpu_util_time_dur;
                gpu_ptr->gpu_prev_util = gpu_util;
                gpu_ptr->gpu_util_prev_utime = gpu_util_utime;
        }

        if (gpu_ptr->mcu_util_update) {
                gpu_ptr->mcu_util_pct = gpu_ptr->mcu_util_diff / gpu_ptr->mcu_util_time_dur;
                gpu_ptr->mcu_util_pct_arr[buf_idx] = gpu_ptr->mcu_util_pct;
                gpu_ptr->mcu_prev_util = mcu_util;
                gpu_ptr->mcu_util_prev_utime = mcu_util_utime;
        }

        if (gpu_ptr->gpu_update || gpu_ptr->gpu_util_update)
                gpu_ptr->gpu_eff_freq = gpu_ptr->gpu_wfreq_pct * gpu_ptr->gpu_util_pct / 100;

}
void tpu_freq_clamp(void)
{
        struct model_input *model_input = &md_input;

        if (!model_input->tpu_freq || !model_input->tpu_util) {
                model_input->tpu_util = 0;
                model_input->tpu_freq = 0;
        }

        if (!model_input->tpu_freq_max || !model_input->tpu_freq_min || !model_input->tpu_freq)
                return;
        if (model_input->tpu_freq > model_input->tpu_freq_max)
                model_input->tpu_freq = model_input->tpu_freq_max;
        if (model_input->tpu_freq < model_input->tpu_freq_min)
                model_input->tpu_freq = model_input->tpu_freq_min;

}


void get_system_index(unsigned int cpu)
{
        struct wc_stats *wc_stats = &cpu_stats;
        struct model_input *model_input = &md_input;
        u64 weight_freq, idle_time;
        u64 wfreq_utime;
        u64 idle_utime;
        bool wfreq_update_flag = 0, idle_update_flag = 0;
        unsigned int act_ratio = 0;
        u64 eff_freq = 0;

        weight_freq = get_cpu_weight_freq(cpu) / 100000;
        idle_time = get_cpu_idle(cpu);
        wfreq_utime = get_wfreq_update(cpu);
        idle_utime = get_idle_update(cpu);

        wc_stats->idle_time_dur[cpu] = idle_utime - wc_stats->prev_idle_utime[cpu];
        wc_stats->wfreq_time_dur[cpu] = wfreq_utime - wc_stats->prev_wfreq_utime[cpu];

        if (wc_stats->idle_time_dur[cpu])
                idle_update_flag = 1;

        if (wc_stats->wfreq_time_dur[cpu]) {
                wfreq_update_flag = 1;
                wc_stats->update_flag = 1;
        }

        if (weight_freq >= wc_stats->prev_wfreq[cpu] && wfreq_update_flag == 1) {
                wc_stats->cpu_wfreq[cpu] = weight_freq - wc_stats->prev_wfreq[cpu];
                wc_stats->wfreq_over[cpu] = 0;
        } else {
                wc_stats->wfreq_over[cpu] = 1;
        }

        if (idle_time >= wc_stats->prev_idle_time[cpu] && idle_update_flag == 1) {
                wc_stats->cpu_idle_time[cpu] = idle_time - wc_stats->prev_idle_time[cpu];
                wc_stats->idle_over[cpu] = 0;

        } else {
                wc_stats->idle_over[cpu] = 1;
        }
        if (idle_update_flag == 1) {
                wc_stats->idle_pct[cpu] = wc_stats->cpu_idle_time[cpu] * 100 / wc_stats->idle_time_dur[cpu];
                wc_stats->idle_pct[cpu] = wc_stats->idle_pct[cpu] > 100 ? 100 : wc_stats->idle_pct[cpu];
                //trace_sched_idle(cnt, cpu, wc_stats, idle_time);
                wc_stats->prev_idle_time[cpu] = idle_time;
                wc_stats->prev_idle_utime[cpu] = idle_utime;
                act_ratio = 100 - wc_stats->idle_pct[cpu];
                model_input->cpu_util[cpu] = act_ratio;
                model_input->cpu_util_arr[cpu][buf_idx] = act_ratio; 
        }

        if (wfreq_update_flag == 1) {
                wc_stats->wfreq_pct[cpu] = wc_stats->cpu_wfreq[cpu] * 100 / wc_stats->wfreq_time_dur[cpu];
                wc_stats->cpu_cap[cpu] = get_cpu_capacity(cpu, wc_stats->wfreq_pct[cpu]);
                wc_stats->cpu_cap_arr[cpu][buf_idx] = wc_stats->cpu_cap[cpu];
                wc_stats->prev_wfreq[cpu] = weight_freq;
                wc_stats->prev_wfreq_utime[cpu] = wfreq_utime;
        }

        eff_freq = wc_stats->wfreq_pct[cpu] * act_ratio / 100;
        wc_stats->eff_freq[cpu] = eff_freq;
        wc_stats->eff_cap[cpu] = wc_stats->cpu_cap[cpu] * act_ratio / 100;
        //trace_sched_wfreq(cnt, cpu, wc_stats, weight_freq, eff_freq);
}


int wlc_init(void)
{
#ifdef ENABLE_WLC_HINT
        wlc_wq = kzalloc(sizeof(struct work_type), GFP_KERNEL);
        wlc_wq->name = "WLC_HINT";
        INIT_WORK(&wlc_wq->wk, hint_work);
#endif
        init_procfs();
        on_each_cpu(enable_fixed_counters_on_cpu, NULL, 1);
	//exynos_pm_qos_add_request(&wc_mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
        //register_trace_node();
#ifdef ENABLE_WLC_HINT
        record_cpu_uclamp_hint();
#endif

#ifdef GPA_NETLINK_SYNC
        gpa_netlink_init();
#endif
        //gpa_genl_init();

        return 0;
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
    /* Enable Fixed Counter 0 (Instruction Retired) */
    wrmsrl_safe(0x38D, 0x3); 
    /* Global Enable for Fixed Counters */
    wrmsrl_safe(0x38F, (1ULL << 32)); 
}


void update_power_stats(u64 *cpu_mw, u64 *gpu_mw, u64 *dram_mw, u64 *pkg_mw, u64 *gpu_util) {
    u32 low, high;
    u64 cur_cpu_en, cur_gpu_en, cur_dram_en, cur_pkg_en;
    u64 cur_time_ns = ktime_get_ns();
    u64 delta_time = cur_time_ns - last_time_ns;

    /* Read MSRs for CPU (0x639), GPU (0x641), and DRAM (0x619) */
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

        /* Convert Energy Ticks to mW */
        *cpu_mw = div64_u64(((d_cpu * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
        *gpu_mw = div64_u64(((d_gpu * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
        *dram_mw = div64_u64(((d_dram * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);
        *pkg_mw = div64_u64(((d_pkg * 1000000000ULL) >> energy_unit_shift) * 1000, delta_time);

        /* GPU Util Estimate: Compare current power against a TDP baseline (e.g., 15W) */
        *gpu_util = (*gpu_mw * 100) / 15000; 
        if (*gpu_util > 100) *gpu_util = 100;
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
        struct msr_data *msr_data = &ms_data;
        struct cpu_msr_state *snap = per_cpu_ptr(&msr_snap, cpu);

        if (rdmsr_safe_on_cpu(cpu, MSR_IA32_MPERF, (u32*)&mperf, (u32*)&mperf+1))
                return;

        rdmsr_safe_on_cpu(cpu, MSR_IA32_APERF, (u32*)&aperf, (u32*)&aperf+1);
        
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

        snap->last_mperf = mperf;
        snap->last_aperf = aperf;
        snap->last_instr = instr;
        snap->last_tsc = tsc;
        
        
        trace_gpa_msr_data(cpu, ms_data.cpu_utilization[cpu], ms_data.cpu_ipc[cpu], 
                       ms_data.cpu_temperature[cpu], ms_data.cpu_total_energy, 
                       ms_data.gpu_total_energy, ms_data.dram_total_energy,
                       ms_data.total_energy,
                       ms_data.gpu_utilization);
        

    }

void wlc_exec(void)
{
        struct wc_stats *wc_stats = &cpu_stats;
        struct model_input *model_input = &md_input;
        struct msr_data *msr_data = &ms_data;
        //struct gpu_priv *gpu_ptr = &gpu_stats;
        //struct mif_priv *mif_ptr = &mif_stats;
        //struct perf_hint *ph = NULL;
        u64 cur_t;
        u64 start_t = 0, end_t = 0;
        unsigned int cpuid;

        if (wc_flag == 1 && global_update == 0) {
                global_update = 1;
                cur_t = ktime_get_ns();
                wc_stats->time_dur = cur_t - wc_stats->last_time;

                pr_err("check point1\n");

                if (wc_stats->time_dur > update_period) {
                        start_t = ktime_get_ns();
                        pr_err("wlc update time dur %llu\n", wc_stats->time_dur);
                        //get_gpu_system_index();  /*use another interface */
                        //get_mif_system_index();
                        //get_pmu_data();
#if 0 
                        tpu_freq_clamp();
                        for_each_possible_cpu(i) {
                                clu = get_cluster_id(i);
                                get_system_index(i);
                                freq_sum[clu] += wc_stats->eff_freq[i];
                                cap_sum[clu] += wc_stats->eff_cap[i];
                        }
                        for(i = 0; i < CLU_NUM; i++) {
                                assign_to_model(i, freq_sum[i], cap_sum[i]);
                                total_cap += cap_sum[i];
                                freq_sum[i] = 0;
                                cap_sum[i] = 0;
                        }
                        model_input->eff_cpu_cap = total_cap;
                        model_input->cap_var = update_mov_avg_data(model_input->cap_avg_arr, &model_input->cap_last_idx, model_input->eff_cpu_cap);
                        model_input->cap_start_idx = get_start_idx(model_input->cap_last_idx);
#endif
#if 0 
                        if (gpu_ptr->gpu_update && gpu_ptr->gpu_wfreq_pct) {
                                model_input->eff_gpu_freq = gpu_ptr->gpu_eff_freq;
                                model_input->gpu_freq = gpu_ptr->gpu_wfreq_pct;
                                model_input->gpu_freq_arr[buf_idx] = gpu_ptr->gpu_wfreq_pct;
                        }
                        if (mif_ptr->mif_update && mif_ptr->mif_wfreq_pct) {
                                model_input->gmc_freq = mif_ptr->mif_wfreq_pct;
                        }

                        if (gpu_ptr->mcu_util_update) {
                                model_input->mcu_util = gpu_ptr->mcu_util_pct;
                        }

                        model_input->gpu_var = update_mov_avg_data(model_input->gpu_avg_arr, &model_input->gpu_last_idx, model_input->eff_gpu_freq);
                        model_input->gpu_start_idx = get_start_idx(model_input->gpu_last_idx);
                        model_input->mif_var = update_mov_avg_data(model_input->mif_avg_arr, &model_input->mif_last_idx, model_input->gmc_freq);
                        model_input->mif_start_idx = get_start_idx(model_input->mif_last_idx);

                        model_input->gpu_var = model_input->gpu_var;
                        model_input->mif_var = model_input->mif_var;
#endif
                        //trace_sched_model_input(cnt, model_input, wc_stats->time_dur, total_cap);
                        


                        update_power_stats(&msr_data->cpu_total_energy, &msr_data->gpu_total_energy, &msr_data->dram_total_energy,
                        		   &msr_data->total_energy, &msr_data->gpu_utilization);
#if 0                        	
                        pr_err("GPA Power: TOTAL %llu CPU %llu , GPU %llu DRAM %llu\n",
                               msr_data->total_energy,
                               msr_data->cpu_total_energy,
                               msr_data->gpu_total_energy,
                               msr_data->dram_total_energy);
#endif
                        for_each_possible_cpu(cpuid)
                                update_msr_stats(cpuid);
                        end_t = ktime_get_ns();
                        model_input->stats_latency = (end_t - start_t) / 1000;

                        start_t = ktime_get_ns();


#if 0
                        /* classify workload */
                        workload_cluster = workload_classify(model_input);
                        model_input->workload_cluster = workload_cluster;
                        wlc_id = workload_cluster;
                        ph = &ph_info[workload_cluster];
                        end_t = ktime_get_ns();
                        model_input->classify_latency = (end_t - start_t) / 1000;
#ifdef ENABLE_WLC_HINT
                        schedule_work(&wlc_wq->wk);
#endif

                        trace_classify_info(cnt, ph, workload_cluster);

                        trace_time_info(cnt, model_input);
#endif

                        //calc_tpu_wfreq();
                        //trace_sched_model_input(cnt, model_input, wc_stats->time_dur, total_cap);
                        //clear_tpu_htable();
                        wc_stats->last_time = cur_t;
                        wc_stats->update_flag = 0;
                        wc_stats->time_dur = 0;
                        cnt_arr[buf_idx] = cnt;
                        cnt++;
                        if (buf_idx >= CAP_AVG_SIZE-1)
                                buf_idx = 0;
                        else
                                buf_idx++;

                }
                global_update = 0;
        }
}

static ssize_t wcflag_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        //char reset[10]="0-7", idx;

        ret  = sscanf(buf, "%u", &wc_flag);
        if(wc_flag == 0) {
                //for (idx = 0; idx < GROUP_NUM; idx++)
                        //cpuset_write(reset, idx);
#ifdef ENABLE_WLC_HINT
                clear_cpu_uclamp_hint();
#endif
        }
	return count;
}

static ssize_t wcflag_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

	return scnprintf(buf, PAGE_SIZE, "wc_flag %u\n", wc_flag);
}


static struct kobj_attribute wc_attr_wcflag = __ATTR_RW_MODE(wcflag, 0660);


static ssize_t gpa_sys_state_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	return count;
}
static ssize_t gpa_sys_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct model_input *model_input = &md_input;
        struct wc_stats *wc_stats = &cpu_stats;
        unsigned long stall_r = 0;
        unsigned int count = 0;
        unsigned int cpuid = 0;
        unsigned int read_idx = get_start_idx(buf_idx);
        unsigned int start_idx;
        unsigned int i = 0;


        start_idx = read_idx == 0 ? (CAP_AVG_SIZE - 1) : (read_idx - 1);
        //count += scnprintf(buf + count, PAGE_SIZE - count, "cnt %u ", cnt);

        for (i = 0; i < CAP_AVG_SIZE; i++) {
                if(start_idx >= CAP_AVG_SIZE)
                        start_idx = 0;

                count += scnprintf(buf + count, PAGE_SIZE - count, "%u ", cnt_arr[start_idx]);
                for_each_possible_cpu(cpuid) {
                        stall_r = model_input->event_val_arr[CPU_STALL][cpuid][start_idx] * 100 /
                            model_input->event_val[CPU_CYC][cpuid];
                        stall_r = stall_r > 100 ? 100 : stall_r;
                        count += scnprintf(buf + count, PAGE_SIZE - count, "%lu %lu %lu %lu %lu %u %lu ",
                            model_input->event_val_arr[CPU_CYC][cpuid][start_idx],
                            model_input->event_val_arr[CPU_INST][cpuid][start_idx],
                            model_input->event_val_arr[CPU_IPC][cpuid][start_idx],
                            stall_r,
                            model_input->event_val_arr[L3_MISS][cpuid][start_idx],
                            model_input->cpu_util_arr[cpuid][start_idx],
                            wc_stats->cpu_cap_arr[cpuid][start_idx]);
                }

                count += scnprintf(buf + count, PAGE_SIZE  - count,
                    "%llu %u %lld %u %u %u\n",
                    model_input->gpu_freq, model_input->gpu_util,
                    model_input->gmc_freq, model_input->tpu_freq,
                    model_input->tpu_avg_freq, model_input->tpu_util);
                start_idx++;
        }



        return count;
}


static struct kobj_attribute wc_attr_gpa_sys_state = __ATTR_RW_MODE(gpa_sys_state, 0660);





static ssize_t get_system_index_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	return count;
}
static ssize_t get_system_index_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct model_input *model_input = &md_input;
        struct wc_stats *wc_stats = &cpu_stats;
        unsigned long stall_r = 0;
        unsigned int count = 0;
        unsigned int cpuid = 0;
        unsigned int read_idx = get_start_idx(buf_idx);
        unsigned int start_idx;
        unsigned int i = 0;
        char version[] = GPA_VERSION;


        start_idx = read_idx == 0 ? (CAP_AVG_SIZE - 1) : (read_idx - 1);
        //count += scnprintf(buf + count, PAGE_SIZE - count, "cnt %u ", cnt);
        calc_tpu_wl();

        for (i = 0; i < CAP_AVG_SIZE; i++) {
                if(start_idx >= CAP_AVG_SIZE)
                        start_idx = 0;

                count += scnprintf(buf + count, PAGE_SIZE - count, "version %s cnt %u ", version, cnt);
                for_each_possible_cpu(cpuid) {
                        stall_r = model_input->event_val_arr[CPU_STALL][cpuid][start_idx] * 100 /
                            model_input->event_val[CPU_CYC][cpuid];
                        stall_r = stall_r > 100 ? 100 : stall_r;
                        count += scnprintf(buf + count, PAGE_SIZE - count, "C%d_cyc %lu C%d_inst %lu C%d_IPC %lu C%d_stall %lu C%d_L3_M %lu C%d_util %u C%d_cap %lu ",
                            cpuid, model_input->event_val_arr[CPU_CYC][cpuid][start_idx],
                            cpuid, model_input->event_val_arr[CPU_INST][cpuid][start_idx],
                            cpuid, model_input->event_val_arr[CPU_IPC][cpuid][start_idx],
                            cpuid, stall_r,
                            cpuid, model_input->event_val_arr[L3_MISS][cpuid][start_idx],
                            cpuid, model_input->cpu_util_arr[cpuid][start_idx],
                            cpuid, wc_stats->cpu_cap_arr[cpuid][start_idx]);
                }

                count += scnprintf(buf + count, PAGE_SIZE  - count,
                    "G_f %llu G_util %u MIF_f %lld T_f %u T_wl %u T_util %u T_freq %u time_in_state %llu\n",
                    model_input->gpu_freq, model_input->gpu_util,
                    model_input->gmc_freq, model_input->tpu_freq,
                    model_input->tpu_avg_freq, model_input->tpu_util, model_input->tpu_freq_htable[8], model_input->tpu_time_in_state[8]);
                start_idx++;
        }

        pr_err("total cnt %d\n", count);


        return count;
}


static struct kobj_attribute wc_attr_get_system_index = __ATTR_RW_MODE(get_system_index, 0660);


static ssize_t get_cache_info_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	return count;
}

static ssize_t get_cache_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct model_input *model_input = &md_input;
        unsigned int stall_r = 0, ipc_r = 0, l3_miss_r = 0;
        unsigned int count = 0;
        unsigned int cpuid = 0, clu;
        unsigned int stall[CLU_NUM], ipc[CLU_NUM], l3_miss[CLU_NUM], l3_access[CLU_NUM], utilization[CLU_NUM];


        count += scnprintf(buf + count, PAGE_SIZE - count, "cnt %u ", cnt);
        for_each_possible_cpu(cpuid) {
                clu = get_cluster_id(cpuid);
                stall_r = model_input->event_val[CPU_STALL][cpuid] * 100 /
                    model_input->event_val[CPU_CYC][cpuid];
                stall_r = stall_r > 100 ? 100 : stall_r;
                stall[clu] += stall_r;

                ipc_r = model_input->event_val[CPU_IPC][cpuid];
                ipc[clu] += ipc_r;

                l3_miss_r = model_input->event_val[L3_MISS_RATIO][cpuid];
                l3_miss[clu] += l3_miss_r;

                utilization[clu] += model_input->cpu_util[cpuid];
                l3_access[clu] += model_input->event_val[L3_ACCESS][cpuid];

          

        }

        for (clu = 0; clu < CLU_NUM; clu++) {
                if(clu == 0) {
                        stall[clu] /= CPUL_NUM;
                        l3_miss[clu] /= CPUL_NUM;
                        ipc[clu] /= CPUL_NUM;
                        utilization[clu] /= CPUL_NUM;
                        l3_access[clu] /= CPUL_NUM;
                } else if(clu == 1) {
                        stall[clu] /= CPUM_NUM;
                        l3_miss[clu] /= CPUM_NUM;
                        ipc[clu] /= CPUM_NUM;
                        utilization[clu] /= CPUM_NUM;
                        l3_access[clu] /= CPUM_NUM;

                } else if(clu == 2) {
                        stall[clu] /= CPUB_NUM;
                        l3_miss[clu] /= CPUB_NUM;
                        ipc[clu] /= CPUB_NUM;
                        utilization[clu] /= CPUB_NUM;
                        l3_access[clu] /= CPUB_NUM;
                }

        }

        count += scnprintf(buf, PAGE_SIZE,
            "CPUL_IPC %u CPUM_IPC %u CPUB_IPC %u CPUL_STALL %u CPUM_STALL %u CPUB_STALL %u CPUL_L3CACHE_MISS %u CPUM_L3CACHE_MISS %u CPUB_L3CACHE_MISS %u CPUL_UTIL %u CPUM_UTIL %u CPUB_UTIL %u CPUL_L3CACHE_ACCESS %u CPUM_L3CACHE_ACCESS %u CPUB_L3CACHE_ACCESS %u\n",
            ipc[0], ipc[1], ipc[2], stall[0], stall[1], stall[2],
            l3_miss[0], l3_miss[1], l3_miss[2], utilization[0], utilization[1], utilization[2], l3_access[0], l3_access[1], l3_access[2]);


        return count;
}


static struct kobj_attribute wc_attr_get_cache_info = __ATTR_RW_MODE(get_cache_info, 0660);






static ssize_t idle_cnt_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;

        ret  = sscanf(buf, "%u", &idle_cnt_flag);
	return count;
}

static ssize_t idle_cnt_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "idle_cnt_enable %u\n", idle_cnt_flag);
}


static struct kobj_attribute wc_attr_idle_cnt = __ATTR_RW_MODE(idle_cnt, 0660);



static ssize_t wlc_cnt_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret, i;

        ret  = sscanf(buf, "%u", &wlc_cnt_flag);

        if (wlc_cnt_flag){
                for (i = 0; i < TOTAL_WORKLOAD_NUM; i++)
                        wlc_counter[i] = 0;
                total_wlc_cnt = 0;
        }
	return count;
}

static ssize_t wlc_cnt_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        int i = 0, cnt = 0;

        total_wlc_cnt = 0;
        for (i = 0; i < WORKLOAD_NUM; i++)
                total_wlc_cnt += wlc_counter[i];

        for (i = 0; i < WORKLOAD_NUM; i++)
                cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "workload_%d %u\n", i, wlc_counter[i]*100/total_wlc_cnt);

        cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "deep_idle %u\n",
                         wlc_counter[DEEP_IDLE_ID] * 100 / total_wlc_cnt);
        cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "burst %u\n",
                         wlc_counter[BURST_ID] * 100 / total_wlc_cnt);
        cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "total_cnt %u\n",
                         total_wlc_cnt);
        return cnt;
}


static struct kobj_attribute wc_attr_wlc_cnt = __ATTR_RW_MODE(wlc_cnt, 0660);

static ssize_t sampling_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret, period;

        ret  = sscanf(buf, "%u", &period);
        update_period = period * 1000000;

	return count;
}

static ssize_t sampling_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "update_period %u\n", update_period / 1000000);
}


static struct kobj_attribute wc_attr_sampling = __ATTR_RW_MODE(sampling, 0660);


static ssize_t debounce_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret, val;
        struct model_input *model_input = &md_input;

        ret  = sscanf(buf, "%u", &val);
        model_input->debounce_num = val;

	return count;
}

static ssize_t debounce_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

        struct model_input *model_input = &md_input;

	return scnprintf(buf, PAGE_SIZE, "debounce_num %u\n", model_input->debounce_num);
}


static struct kobj_attribute wc_attr_debounce = __ATTR_RW_MODE(debounce, 0660);

static ssize_t burst_num_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret, val;
        struct model_input *model_input = &md_input;

        ret  = sscanf(buf, "%u", &val);
        model_input->burst_num = val;

	return count;
}

static ssize_t burst_num_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

        struct model_input *model_input = &md_input;

	return scnprintf(buf, PAGE_SIZE, "burst_num %u\n", model_input->burst_num);
}


static struct kobj_attribute wc_attr_burst_num = __ATTR_RW_MODE(burst_num, 0660);





static ssize_t mif_config_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
        int load_num = -1 , freq_min = -1, ret;
        struct perf_hint *ph;

        ret  = sscanf(buf, "%d %d", &load_num, &freq_min);
        if (load_num >= 0 && freq_min >= 0) {
                ph = &ph_info[load_num];
                last_modify_id = load_num;
                ph->mif_freq = freq_min;
        }


	return count;
}

static ssize_t mif_config_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];

        return scnprintf(buf, PAGE_SIZE, " workload_id %u mif_min_freq %u\n", last_modify_id, ph->mif_freq);
}


static struct kobj_attribute wc_attr_mif_config = __ATTR_RW_MODE(mif_config, 0660);


static ssize_t uclamp_hint_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        int load_num, type, group, val;
        struct perf_hint *ph;

        ret  = sscanf(buf, "%d %d %d %d", &load_num, &type, &group, &val);
        ph = &ph_info[load_num];
        last_modify_id = load_num;
        ph->uclamp_hint.cid = type;
        ph->uclamp_hint.vg = group;
        ph->uclamp_hint.val = val;

	return count;
}

static ssize_t uclamp_hint_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];

	return scnprintf(buf, PAGE_SIZE, "workload_id %d type %d group %d val %d\n", last_modify_id,
                                          ph->uclamp_hint.cid,
                                          ph->uclamp_hint.vg,
                                          ph->uclamp_hint.val);
}


static struct kobj_attribute wc_attr_uclamp_hint = __ATTR_RW_MODE(uclamp_hint, 0660);

static ssize_t freq_hint_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        int load_num, clu, freq_min, freq_max;
        struct perf_hint *ph;

        ret  = sscanf(buf, "%u %u %d %d", &load_num, &clu, &freq_min, &freq_max);
        ph = &ph_info[load_num];
        last_modify_id = load_num;
        if (freq_min >= 0)
                ph->freq_hint[clu].freq_min = freq_min;
        else
                ph->freq_hint[clu].freq_min = default_min_freq[clu];

        if (freq_max >= 0)
                ph->freq_hint[clu].freq_max = freq_max;
        else
                ph->freq_hint[clu].freq_max = default_max_freq[clu];

	return count;
}

static ssize_t freq_hint_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];

	return scnprintf(buf, PAGE_SIZE, "workload_id %d\nclu 0 f_min %u f_max %u\nclu 1 f_min %u f_max %u\nclu 2 f_min %u f_max %u\n", last_modify_id, ph->freq_hint[0].freq_min,
                                          ph->freq_hint[0].freq_max,
                                          ph->freq_hint[1].freq_min,
                                          ph->freq_hint[1].freq_max,
                                          ph->freq_hint[2].freq_min,
                                          ph->freq_hint[2].freq_max);
}


static struct kobj_attribute wc_attr_freq_hint = __ATTR_RW_MODE(freq_hint, 0660);


static ssize_t idle_bounce_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        int load_num, bounce_flag;
        struct perf_hint *ph;

        ret  = sscanf(buf, "%u %u", &load_num, &bounce_flag);
        ph = &ph_info[load_num];
        last_modify_id = load_num;
        if (bounce_flag >= 0)
                ph->bounce_flag = bounce_flag;

	return count;
}

static ssize_t idle_bounce_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];

	return scnprintf(buf, PAGE_SIZE, "workload_id %d idle_bounce_flag %d\n", last_modify_id, ph->bounce_flag);
}


static struct kobj_attribute wc_attr_idle_bounce = __ATTR_RW_MODE(idle_bounce, 0660);




static ssize_t burst_bounce_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        int load_num, burst_flag;
        struct perf_hint *ph;

        ret  = sscanf(buf, "%u %u", &load_num, &burst_flag);
        ph = &ph_info[load_num];
        last_modify_id = load_num;
        if (burst_flag >= 0)
                ph->burst_flag = burst_flag;

	return count;
}

static ssize_t burst_bounce_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];

	return scnprintf(buf, PAGE_SIZE, "workload_id %d burst_bounce_flag %d\n", last_modify_id, ph->burst_flag);
}


static struct kobj_attribute wc_attr_burst_bounce = __ATTR_RW_MODE(burst_bounce, 0660);



static ssize_t em_tbl_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret;
        int load_num, em_flag;
        char tbl_name[20];
        struct perf_hint *ph;

        ret  = sscanf(buf, "%u %d %s", &load_num, &em_flag, tbl_name);
        ph = &ph_info[load_num];
        last_modify_id = load_num;
        ph->hint_flag[CPU_EM] = em_flag;
        strcpy(ph->profile_name, tbl_name);

	return count;
}

static ssize_t em_tbl_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];
 
	return scnprintf(buf, PAGE_SIZE, "Workload_id %d EM_FLAG %d Energy_model tbl %s\n", last_modify_id, ph->hint_flag[CPU_EM], ph->profile_name);
}


static struct kobj_attribute wc_attr_em_tbl = __ATTR_RW_MODE(em_tbl, 0660);


static ssize_t affinity_hint_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
        int load_num;
        char set[20];
        char group[4] = "0-7";
        struct perf_hint *ph;

        ret  = sscanf(buf, "%u %s %s", &load_num, set, group);

        ph = &ph_info[load_num];
        switch(set[0]) {
                case 'T':
                        ph->affinity_hint.group_id = TOP_APP;
                        strcpy(ph->affinity_hint.cpuset,group);
                        break;

                case 'F':
                        ph->affinity_hint.group_id = FOREGROUND;
                        strcpy(ph->affinity_hint.cpuset,group);
                        break;

                case 'B':
                        ph->affinity_hint.group_id = BACKGROUND;
                        strcpy(ph->affinity_hint.cpuset,group);
                        break;

                case 'R':
                        ph->affinity_hint.group_id = RESTRICTED;
                        strcpy(ph->affinity_hint.cpuset,group);
                        break;

        }
        last_modify_id = load_num;

	return count;
}

static ssize_t affinity_hint_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct perf_hint *ph = &ph_info[last_modify_id];
        char group[20];


        switch(ph->affinity_hint.group_id) {
                case TOP_APP:
                        strcpy(group,"TOP_APP");
                        break;
                case FOREGROUND:
                        strcpy(group,"FOREGROUND");
                        break;
                case BACKGROUND:
                        strcpy(group,"BACKGROUND");
                        break;
                case RESTRICTED:
                        strcpy(group,"RESTRICTED");
                        break;

        }
	return scnprintf(buf, PAGE_SIZE, "workload_id %d %s %s\n",
                         last_modify_id, group, ph->affinity_hint.cpuset);
}


static struct kobj_attribute wc_attr_affinity_hint = __ATTR_RW_MODE(affinity_hint, 0660);


#define STRSZ 300
#define HINT_NUM 30
static ssize_t hint_info_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int ret = 0, idx = 0, i = 0;
        char hint_val[STRSZ];
        char *str_ptr;
        char *token[HINT_NUM];
        char *delim = ",";
        //struct model_input *model_input = &md_input;

        ret  = sscanf(buf, "%s", hint_val);
        str_ptr = hint_val;
        token[idx++] = strsep(&str_ptr, delim);
        while(token[idx-1] != NULL) {
                token[idx++] = strsep(&str_ptr, delim);
        
        }
        hint_clear();
        for(i = 0; i < idx; i++) {
                if (token[i]!=NULL)
                        hint_convert(token[i]); 

        }
#if 0
        if (token[0]!= NULL)
                ret = kstrtol(token[0], 0, &model_input->ADPF_DISABLE_TA_BOOST);
        if (token[1]!= NULL)
                ret = kstrtol(token[1], 0, &model_input->CAMERA_BACKEND_BOOST);
        if (token[2]!= NULL)
                ret = kstrtol(token[2], 0, &model_input->CAMERA_CAPTURE_CPU_THROTTLE);
        if (token[3]!= NULL)
                ret = kstrtol(token[3], 0, &model_input->CAMERA_GPU_STANDARD);
        if (token[4]!= NULL)
                ret = kstrtol(token[4], 0, &model_input->CAMERA_LAUNCH);
        if (token[5]!= NULL)
                ret = kstrtol(token[5], 0, &model_input->CAMERA_LAUNCH_EXTENDED);
        if (token[6]!= NULL)
                ret = kstrtol(token[6], 0, &model_input->CAMERA_STREAMING_HIGH);
        if (token[7]!= NULL)
                ret = kstrtol(token[7], 0, &model_input->CAMERA_STREAMING_STANDARD);
        if (token[8]!= NULL)
                ret = kstrtol(token[8], 0, &model_input->CAMERA_STREAMING_VIDEO_CALL);
        if (token[9]!= NULL)
                ret = kstrtol(token[9], 0, &model_input->CAMERA_VIDEO_RECORDING);
        if (token[10]!= NULL)
                ret = kstrtol(token[10], 0, &model_input->CAMERA_ZOOMING_BOOST);
        if (token[11]!= NULL)
                ret = kstrtol(token[11], 0, &model_input->CPU_LOAD_RESET);
        if (token[12]!= NULL)
                ret = kstrtol(token[12], 0, &model_input->DISPLAY_CHANGE);
        if (token[13]!= NULL)
                ret = kstrtol(token[13], 0, &model_input->DISPLAY_CHANGE_GPU);
        if (token[14]!= NULL)
                ret = kstrtol(token[14], 0, &model_input->DISPLAY_IDLE);
        if (token[15]!= NULL)
                ret = kstrtol(token[15], 0, &model_input->DISPLAY_INACTIVE);
        if (token[16]!= NULL)
                ret = kstrtol(token[16], 0, &model_input->DISPLAY_UPDATE_IMMINENT);
        if (token[17]!= NULL)
                ret = kstrtol(token[17], 0, &model_input->EXPENSIVE_RENDERING);
        if (token[18]!= NULL)
                ret = kstrtol(token[18], 0, &model_input->GAME);
        if (token[19]!= NULL)
                ret = kstrtol(token[19], 0, &model_input->GCA_CAMERA_SHOT_ALLCPU);
        if (token[20]!= NULL)
                ret = kstrtol(token[20], 0, &model_input->LAUNCH);
        if (token[21]!= NULL)
                ret = kstrtol(token[21], 0, &model_input->LAUNCH_EXTEND);
        if (token[22]!= NULL)
                ret = kstrtol(token[22], 0, &model_input->LAUNCH_GPU);
        if (token[23]!= NULL)
                ret = kstrtol(token[23], 0, &model_input->LAUNCH_PMU);
        if (token[24]!= NULL)
                ret = kstrtol(token[24], 0, &model_input->REFRESH_120FPS);
        if (token[25]!= NULL)
                ret = kstrtol(token[25], 0, &model_input->REFRESH_60FPS);
#endif
	return count;
}

void hint_convert(char *str)
{

        struct model_input *model_input = &md_input;
        
        if (!strcmp(str, "ADPF_DISABLE_TA_BOOST"))
                model_input->ADPF_DISABLE_TA_BOOST = 1;
        if (!strcmp(str, "CAMERA_BACKEND_BOOST"))
                model_input->CAMERA_BACKEND_BOOST = 1;
        if (!strcmp(str, "CAMERA_CAPTURE_CPU_THROTTLE"))
                model_input->CAMERA_CAPTURE_CPU_THROTTLE = 1;
        if (!strcmp(str, "CAMERA_GPU_STANDARD"))
                model_input->CAMERA_GPU_STANDARD = 1;
        if (!strcmp(str, "CAMERA_LAUNCH"))
                model_input->CAMERA_LAUNCH = 1;
        if (!strcmp(str, "CAMERA_LAUNCH_EXTENDED"))
                model_input->CAMERA_LAUNCH_EXTENDED = 1;
        if (!strcmp(str, "CAMERA_STREAMING_HIGH"))
                model_input->CAMERA_STREAMING_HIGH = 1;
        if (!strcmp(str, "CAMERA_STREAMING_STANDARD"))
                model_input->CAMERA_STREAMING_STANDARD = 1;
        if (!strcmp(str, "CAMERA_STREAMING_VIDEO_CALL"))
                model_input->CAMERA_STREAMING_VIDEO_CALL = 1;
        if (!strcmp(str, "CAMERA_VIDEO_RECORDING"))
                model_input->CAMERA_VIDEO_RECORDING = 1;
        if (!strcmp(str, "CAMERA_ZOOMING_BOOST"))
                model_input->CAMERA_ZOOMING_BOOST = 1;
        if (!strcmp(str, "CPU_LOAD_RESET"))
                model_input->CPU_LOAD_RESET = 1;
        if (!strcmp(str, "DISPLAY_CHANGE"))
                model_input->DISPLAY_CHANGE = 1;
        if (!strcmp(str, "DISPLAY_CHANGE_GPU"))
                model_input->DISPLAY_CHANGE_GPU = 1;
        if (!strcmp(str, "DISPLAY_IDLE"))
                model_input->DISPLAY_IDLE = 1;
        if (!strcmp(str, "DISPLAY_INACTIVE"))
                model_input->DISPLAY_INACTIVE = 1;
        if (!strcmp(str, "DISPLAY_UPDATE_IMMINENT"))
                model_input->DISPLAY_UPDATE_IMMINENT = 1;
        if (!strcmp(str, "EXPENSIVE_RENDERING"))
                model_input->EXPENSIVE_RENDERING = 1;
        if (!strcmp(str, "GAME"))
                model_input->GAME = 1;
        if (!strcmp(str, "GCA_CAMERA_SHOT_ALLCPU"))
                model_input->GCA_CAMERA_SHOT_ALLCPU = 1;
        if (!strcmp(str, "LAUNCH"))
                model_input->LAUNCH = 1;
        if (!strcmp(str, "LAUNCH_EXTEND"))
                model_input->LAUNCH_EXTEND = 1;
        if (!strcmp(str, "LAUNCH_GPU"))
                model_input->LAUNCH_GPU = 1;
        if (!strcmp(str, "LAUNCH_PMU"))
                model_input->LAUNCH_PMU = 1;
        if (!strcmp(str, "REFRESH_120FPS"))
                model_input->REFRESH_120FPS = 1;
        if (!strcmp(str, "REFRESH_60FPS"))
                model_input->REFRESH_60FPS = 1;

}

void hint_clear(void)
{

        struct model_input *model_input = &md_input;

        model_input->ADPF_DISABLE_TA_BOOST = 0;
        model_input->CAMERA_BACKEND_BOOST = 0;
        model_input->CAMERA_CAPTURE_CPU_THROTTLE = 0;
        model_input->CAMERA_GPU_STANDARD = 0;
        model_input->CAMERA_LAUNCH = 0;
        model_input->CAMERA_LAUNCH_EXTENDED = 0;
        model_input->CAMERA_STREAMING_HIGH = 0;
        model_input->CAMERA_STREAMING_STANDARD = 0;
        model_input->CAMERA_STREAMING_VIDEO_CALL = 0;
        model_input->CAMERA_VIDEO_RECORDING = 0;
        model_input->CAMERA_ZOOMING_BOOST = 0;
        model_input->CPU_LOAD_RESET = 0;
        model_input->DISPLAY_CHANGE = 0;
        model_input->DISPLAY_CHANGE_GPU = 0;
        model_input->DISPLAY_IDLE = 0;
        model_input->DISPLAY_INACTIVE = 0;
        model_input->DISPLAY_UPDATE_IMMINENT = 0;
        model_input->EXPENSIVE_RENDERING = 0;
        model_input->GAME = 0;
        model_input->GCA_CAMERA_SHOT_ALLCPU = 0;
        model_input->LAUNCH = 0;
        model_input->LAUNCH_EXTEND = 0;
        model_input->LAUNCH_GPU = 0;
        model_input->LAUNCH_PMU = 0;
        model_input->REFRESH_120FPS = 0;
        model_input->REFRESH_60FPS = 0;

}

static ssize_t hint_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "Write only node\n");
}


static struct kobj_attribute wc_attr_hint_info = __ATTR_RW_MODE(hint_info, 0660);




static struct attribute *wc_attrs[] = {
	&wc_attr_wcflag.attr,
	&wc_attr_get_system_index.attr,
	&wc_attr_gpa_sys_state.attr,
	&wc_attr_get_cache_info.attr,
	&wc_attr_idle_cnt.attr,
	&wc_attr_sampling.attr,
	&wc_attr_debounce.attr,
	&wc_attr_burst_num.attr,
        &wc_attr_wlc_cnt.attr,
	&wc_attr_mif_config.attr,
	&wc_attr_freq_hint.attr,
        &wc_attr_uclamp_hint.attr,
	&wc_attr_affinity_hint.attr,
	&wc_attr_idle_bounce.attr,
	&wc_attr_burst_bounce.attr,
	&wc_attr_em_tbl.attr,
	&wc_attr_hint_info.attr,
	NULL
};

static const struct attribute_group wc_attr_group = {
	.attrs = wc_attrs,
	.name = "config_setting"
};


static int init_procfs(void)
{
	wc_kobj = kobject_create_and_add("workload_classifier", kernel_kobj);
	if (!wc_kobj) {
		pr_err("cannot create kobj for workload classifier!");
		goto error;
	}
        if (sysfs_create_group(wc_kobj, &wc_attr_group)) {
		pr_err("cannot create files in ../workload_classifier/workload_classifier\n");
		kobject_put(wc_kobj);
		return -EINVAL;
	}
	pr_info("initialized!");
	return 0;
error:
	return -ENOMEM;
}






