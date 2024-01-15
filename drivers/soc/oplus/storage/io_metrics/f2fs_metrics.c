#include "io_metrics_entry.h"
#include "f2fs_metrics.h"
#include "procfs.h"
#include "fs/f2fs/f2fs.h"
#include "fs/f2fs/segment.h"
#include "fs/f2fs/node.h"
#include <trace/events/f2fs.h>

bool f2fs_issue_discard_enabled = false;
bool f2fs_gc_begin_enabled = false;
bool f2fs_gc_end_enabled = false;
bool f2fs_write_checkpoint_enabled = false;
bool f2fs_sync_file_enter_enabled = false;
bool f2fs_sync_file_exit_enabled = false;
bool f2fs_dataread_start_enabled = false;
bool f2fs_dataread_end_enabled = false;
bool f2fs_datawrite_start_enabled = false;
bool f2fs_datawrite_end_enabled = false;

module_param(f2fs_issue_discard_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_issue_discard_enabled, " Debug f2fs_issue_discard");
module_param(f2fs_gc_begin_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_gc_begin_enabled, " Debug f2fs_gc_begin");
module_param(f2fs_gc_end_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_gc_end_enabled, " Debug f2fs_gc_end");
module_param(f2fs_write_checkpoint_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_write_checkpoint_enabled, " Debug f2fs_write_checkpoint");
module_param(f2fs_sync_file_enter_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_sync_file_enter_enabled, " Debug f2fs_sync_file_enter");
module_param(f2fs_sync_file_exit_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_sync_file_exit_enabled, " Debug f2fs_sync_file_exit");
module_param(f2fs_dataread_start_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_dataread_start_enabled, " Debug f2fs_dataread_start");
module_param(f2fs_dataread_end_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_dataread_end_enabled, " Debug f2fs_dataread_end");
module_param(f2fs_datawrite_start_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_datawrite_start_enabled, " Debug f2fs_datawrite_start");
module_param(f2fs_datawrite_end_enabled, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(f2fs_datawrite_end_enabled, " Debug f2fs_datawrite_end");

enum {
    GC_BG = 0,  //Background GC
    GC_FG,      //Front end GC
    GC_MAX
};
atomic64_t f2fs_metrics_timestamp[CYCLE_MAX];
/* GC has its own lock protection and no competition, so there is no need to 
customize locks */
struct {
    /* Accumulated time consumption statistics */
    u64 elapse_time;
    /* Last GC start time */
    u64 begin_time;
    /* Total number of GC attempts */
    u64 cnt;
    /* The average time spent on a single GC */
    u64 avg_time;
    /* Total number of segments recycled */
    u64 segs;
    /* Average number of segments recovered in one GC cycle */
    u64 avg_segs;
    /* The average proportion of effective blocks in each GC */
    u64 efficiency;
    char padding[8];
} f2fs_gc_metrics[CYCLE_MAX][GC_MAX];//Statistics for different layers

/* CP has its own GC lock protection and no competition, so there is no need to 
customize locks */
struct {
    /* CP total count statistics */
    u64 cnt;
    /* Accumulated time consumption */
    u64 elapse_time;
    /* Last GC start time */
    u64 begin_time;
    /* Average time consumption */
    u64 avg_time;
    /* Maximum time consumption */
    u64 max_time;
    /* Number of local updates */
    u32 inplace_count;
    char padding[20];
} f2fs_cp_metrics[CYCLE_MAX] = {0};

struct {
    /* Discard count statistics */
    u64 discard_cnt;
    u64 discard_len;
    u64 ipu_cnt;
    u64 fsync_cnt;
    char padding[32];
} f2fs_metrics[CYCLE_MAX] = {0};

static void cb_f2fs_issue_discard(void *ignore, struct block_device *dev,
                                          block_t blkstart, block_t blklen)
{
    int i;
    u64 current_time_ns, elapse;

    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    current_time_ns = ktime_get_ns();

    for (i = 0; i < CYCLE_MAX; i++) {
        elapse = current_time_ns - atomic64_read(&f2fs_metrics_timestamp[i]);
        if (unlikely(elapse >= current_time_ns)) {
            atomic64_set(&f2fs_metrics_timestamp[i], current_time_ns);
            f2fs_metrics[i].discard_cnt = 1;
            f2fs_metrics[i].discard_len = blklen;
            elapse = 0;
        } else {
            f2fs_metrics[i].discard_cnt += 1;
            f2fs_metrics[i].discard_len += blklen;
        }
        if (unlikely(elapse >= sample_cycle_config[i].cycle_value)) {
            atomic64_set(&f2fs_metrics_timestamp[i], 0);
        }
    }
    if (unlikely(io_metrics_debug_enabled || f2fs_issue_discard_enabled)) {
        io_metrics_print("current_time_ns:%llu\n", current_time_ns);
    }
};
int gc_t;
static void cb_f2fs_gc_begin(void *ignore, struct super_block *sb, int gc_type, bool no_bg_gc,
            unsigned int nr_free_secs,
            long long dirty_nodes, long long dirty_dents,
            long long dirty_imeta, unsigned int free_sec,
            unsigned int free_seg, int reserved_seg,
            unsigned int prefree_seg)
{
    int i;
    u64 current_time_ns, elapse;

    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    current_time_ns = ktime_get_ns();
    gc_t = no_bg_gc ? GC_FG : GC_BG;
    for (i = 0; i < CYCLE_MAX; i++) {
        elapse = current_time_ns - atomic64_read(&f2fs_metrics_timestamp[i]);
        if (unlikely(elapse >= current_time_ns)) {
            atomic64_set(&f2fs_metrics_timestamp[i], current_time_ns);
            f2fs_gc_metrics[i][gc_t].elapse_time = 0;
            f2fs_gc_metrics[i][gc_t].begin_time = current_time_ns;
            f2fs_gc_metrics[i][gc_t].cnt = 0;
            f2fs_gc_metrics[i][gc_t].avg_time = 0;
            f2fs_gc_metrics[i][gc_t].segs = 0;
            f2fs_gc_metrics[i][gc_t].avg_segs = 0;
            f2fs_gc_metrics[i][gc_t].efficiency = 0;
            elapse = 0;
        } else {
            f2fs_gc_metrics[i][gc_t].begin_time = current_time_ns;
        }
        if (unlikely(elapse >= sample_cycle_config[i].cycle_value)) {
            atomic64_set(&f2fs_metrics_timestamp[i], 0);
        }
    }
    if (unlikely(io_metrics_debug_enabled || f2fs_gc_begin_enabled)) {
        io_metrics_print("current_time_ns:%llu\n", current_time_ns);
    }
};

static void cb_f2fs_gc_end(void *ignore, struct super_block *sb, int ret,
            int seg_freed, int sec_freed, long long dirty_nodes,
            long long dirty_dents, long long dirty_imeta,
            unsigned int free_sec, unsigned int free_seg,
            int reserved_seg, unsigned int prefree_seg)
{
    int i;
    u64 current_time_ns, gc_elapse = 0;

    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    if (unlikely(!(sb->s_flags & SB_ACTIVE))) {
        return;
    }
    if (gc_t > GC_FG) {
        if (unlikely(io_metrics_debug_enabled)) {
            io_metrics_print("gc_t(%d) is not a expected value\n", gc_t);
        }
        return;
    }
    current_time_ns = ktime_get_ns();
    for (i = 0; i < CYCLE_MAX; i++) {
        /* Considering that there may be external reset operations after the start 
        of GC, resulting in data distortion */
        if (likely(f2fs_gc_metrics[i][gc_t].begin_time)) {
            gc_elapse = current_time_ns - f2fs_gc_metrics[i][gc_t].begin_time;
            f2fs_gc_metrics[i][gc_t].elapse_time += gc_elapse;
            f2fs_gc_metrics[i][gc_t].cnt += 1;
            f2fs_gc_metrics[i][gc_t].segs += free_seg;/* todo */
            f2fs_gc_metrics[i][gc_t].avg_time = f2fs_gc_metrics[i][gc_t].elapse_time /
                                               f2fs_gc_metrics[i][gc_t].cnt;
            /* todo */
            f2fs_gc_metrics[i][gc_t].avg_segs = f2fs_gc_metrics[i][gc_t].segs /
                                               f2fs_gc_metrics[i][gc_t].cnt;
            f2fs_gc_metrics[i][gc_t].begin_time = 0;
        }
    }
    if (unlikely(io_metrics_debug_enabled || f2fs_gc_end_enabled)) {
        const char *gc_type[] = {"Background", "Foreground"};
        io_metrics_print("%s gc elapse:%llu  count:%llu\n", gc_type[gc_t], gc_elapse,
                                                f2fs_gc_metrics[CYCLE_MAX-1][gc_t].cnt);
    }
};
static void cb_f2fs_write_checkpoint(void *ignore, struct super_block *sb,
                                               int reason, const char *msg)
{
    int i;
    u64 current_time_ns, elapse, cp_elapse = 0;
#ifdef CONFIG_F2FS_STAT_FS
    struct f2fs_sb_info *sbi = F2FS_SB(sb);
#endif

    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    current_time_ns = ktime_get_ns();
    if (!strcmp(msg, "start block_ops")) {
        for (i = 0; i < CYCLE_MAX; i++) {
            elapse = current_time_ns - atomic64_read(&f2fs_metrics_timestamp[i]);
            if (unlikely(elapse >= current_time_ns)) {
                atomic64_set(&f2fs_metrics_timestamp[i], current_time_ns);
                f2fs_cp_metrics[i].begin_time = current_time_ns;
                f2fs_cp_metrics[i].cnt = 0;
                f2fs_cp_metrics[i].elapse_time = 0;
                f2fs_cp_metrics[i].avg_time = 0;
                f2fs_cp_metrics[i].max_time = 0;
                elapse = 0;
            } else {
                f2fs_cp_metrics[i].begin_time = current_time_ns;
            }
#ifdef CONFIG_F2FS_STAT_FS
            f2fs_cp_metrics[i].inplace_count = atomic_read(&sbi->inplace_count);
#endif
            if (unlikely(elapse >= sample_cycle_config[i].cycle_value)) {
                atomic64_set(&f2fs_metrics_timestamp[i], 0);
            }
        }
        if (unlikely(io_metrics_debug_enabled || f2fs_write_checkpoint_enabled)) {
            io_metrics_print("current_time_ns:%llu\n", current_time_ns);
        }
    } else if (!strcmp(msg, "finish checkpoint")) {
        for (i = 0; i < CYCLE_MAX; i++) {
            /* Considering that there may be external reset operations after the 
            start of CP, resulting in data distortion */
            if (likely(f2fs_cp_metrics[i].begin_time)) {
                cp_elapse = current_time_ns - f2fs_cp_metrics[i].begin_time;
                f2fs_cp_metrics[i].elapse_time += cp_elapse;
                f2fs_cp_metrics[i].cnt += 1;
                f2fs_cp_metrics[i].avg_time = f2fs_cp_metrics[i].elapse_time /
                                              f2fs_cp_metrics[i].cnt;
                f2fs_cp_metrics[i].max_time = f2fs_cp_metrics[i].max_time >= cp_elapse ?
                                              f2fs_cp_metrics[i].max_time : cp_elapse;
                f2fs_cp_metrics[i].begin_time = 0;
            }
        }
        if (unlikely(io_metrics_debug_enabled || f2fs_write_checkpoint_enabled)) {
            io_metrics_print("checkpoint elapse:%llu  count:%llu\n", cp_elapse,
                                            f2fs_cp_metrics[CYCLE_MAX-1].cnt);
        }
    }
};

static void cb_f2fs_sync_file_enter(void *ignore, struct inode *inode)
{
    int i;
    u64 current_time_ns, elapse;

    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    current_time_ns = ktime_get_ns();
    for (i = 0; i < CYCLE_MAX; i++) {
        elapse = current_time_ns - atomic64_read(&f2fs_metrics_timestamp[i]);
        if (unlikely(elapse >= current_time_ns)) {
            atomic64_set(&f2fs_metrics_timestamp[i], current_time_ns);
            f2fs_metrics[i].fsync_cnt = 1;
            elapse = 0;
        } else {
            f2fs_metrics[i].fsync_cnt += 1;
        }
        if (unlikely(elapse >= sample_cycle_config[i].cycle_value)) {
            atomic64_set(&f2fs_metrics_timestamp[i], 0);
        }
    }
    if (unlikely(io_metrics_debug_enabled || f2fs_sync_file_enter_enabled)) {
        io_metrics_print("current_time_ns:%llu count:%llu\n", current_time_ns,
                                       f2fs_metrics[CYCLE_MAX-1].fsync_cnt);
    }
};

static void cb_f2fs_sync_file_exit(void *ignore, struct inode *inode,
                                   int cp_reason, int datasync, int ret)
{
    if (unlikely(!io_metrics_enabled)) {
        return;
    }
    if (unlikely(io_metrics_debug_enabled || f2fs_sync_file_exit_enabled)) {
        io_metrics_print("here\n");
    }
};

void f2fs_register_tracepoint_probes(void)
{
    int ret;
    ret = register_trace_f2fs_issue_discard(cb_f2fs_issue_discard, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_gc_begin(cb_f2fs_gc_begin, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_gc_end(cb_f2fs_gc_end, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_write_checkpoint(cb_f2fs_write_checkpoint, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_sync_file_enter(cb_f2fs_sync_file_enter, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_sync_file_exit(cb_f2fs_sync_file_exit, NULL);
    WARN_ON(ret);
}

void f2fs_unregister_tracepoint_probes(void)
{
    unregister_trace_f2fs_issue_discard(cb_f2fs_issue_discard, NULL);
    unregister_trace_f2fs_gc_begin(cb_f2fs_gc_begin, NULL);
    unregister_trace_f2fs_gc_end(cb_f2fs_gc_end, NULL);
    unregister_trace_f2fs_write_checkpoint(cb_f2fs_write_checkpoint, NULL);
    unregister_trace_f2fs_sync_file_enter(cb_f2fs_sync_file_enter, NULL);
    unregister_trace_f2fs_sync_file_exit(cb_f2fs_sync_file_exit, NULL);
}

static int f2fs_metrics_proc_show(struct seq_file *seq_filp, void *data)
{
    int i = 0;
    u64 value = 123;
    struct file *file = (struct file *)seq_filp->private;
    enum sample_cycle_type cycle;

    if (unlikely(!io_metrics_enabled)) {
        seq_printf(seq_filp, "io_metrics_enabled not set to 1:%d\n", io_metrics_enabled);
        return 0;
    }

    if (proc_show_enabled || unlikely(io_metrics_debug_enabled)) {
        io_metrics_print("%s(%d) read %s/%s\n",
            current->comm, current->pid, file->f_path.dentry->d_parent->d_iname,
            file->f_path.dentry->d_iname);
    }
    /* Determine the value of the sampling period (parent directory) */
    cycle = CYCLE_MAX;
    for (i = 0; i < CYCLE_MAX; i++) {
        if(!strcmp(file->f_path.dentry->d_parent->d_iname, sample_cycle_config[i].tag)) {
            cycle = sample_cycle_config[i].value;
        }
    }
    if (unlikely(cycle == CYCLE_MAX)) {
        goto err;
    }
    if(!strcmp(file->f_path.dentry->d_iname, "f2fs_discard_cnt")) {
        value = f2fs_metrics[cycle].discard_cnt;
    } else if(!strcmp(file->f_path.dentry->d_iname, "f2fs_discard_len")) {
        value = f2fs_metrics[cycle].discard_len;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_fg_gc_cnt")) {
        value = f2fs_gc_metrics[cycle][GC_FG].cnt;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_fg_gc_avg_time")) {
        value = f2fs_gc_metrics[cycle][GC_FG].avg_time;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_fg_gc_seg_cnt")) {
        value = f2fs_gc_metrics[cycle][GC_FG].segs;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_bg_gc_cnt")) {
        value = f2fs_gc_metrics[cycle][GC_BG].cnt;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_bg_gc_avg_time")) {
        value = f2fs_gc_metrics[cycle][GC_BG].avg_time;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_bg_gc_seg_cnt")) {
        value = f2fs_gc_metrics[cycle][GC_BG].segs;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_cp_cnt")) {
        value = f2fs_cp_metrics[cycle].cnt;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_cp_avg_time")) {
        value = f2fs_cp_metrics[cycle].avg_time;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_cp_max_time")) {
        value = f2fs_cp_metrics[cycle].max_time;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_ipu_cnt")) {
        value = f2fs_cp_metrics[cycle].inplace_count;
    } else if (!strcmp(file->f_path.dentry->d_iname, "f2fs_fsync_cnt")) {
        value = f2fs_metrics[cycle].fsync_cnt;
    }
    seq_printf(seq_filp, "%llu\n", value);

    return 0;

err:
    io_metrics_print("%s(%d) I don't understand what the operation: %s/%s\n",
                                                      current->comm,current->pid,
             file->f_path.dentry->d_parent->d_iname, file->f_path.dentry->d_iname);
    return -1;
}

int f2fs_metrics_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, f2fs_metrics_proc_show, file);
}

void f2fs_metrics_reset(void)
{
    int i = 0;
    for (i = 0; i < CYCLE_MAX; i++) {
        atomic64_set(&f2fs_metrics_timestamp[i], 0);
    }
    memset(&f2fs_gc_metrics, 0, sizeof(f2fs_gc_metrics));
    memset(&f2fs_cp_metrics, 0, sizeof(f2fs_cp_metrics));
    memset(&f2fs_metrics, 0, sizeof(f2fs_metrics));
}
void f2fs_metrics_init(void)
{
    f2fs_metrics_reset();
    gc_t = 0;
}