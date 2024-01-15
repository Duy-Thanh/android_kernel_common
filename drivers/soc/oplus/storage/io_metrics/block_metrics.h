#ifndef __BLOCK_METRICS_H__
#define __BLOCK_METRICS_H__

#include <linux/fs.h>

#define IO_SIZE_4K_TO_32K_MASK       4096
#define IO_SIZE_32K_TO_128K_MASK     32768
#define IO_SIZE_128K_TO_512K_MASK    131072
#define IO_SIZE_512K_TO_MAX_MASK     524288

/* IO size histogram */
enum io_range {
    IO_SIZE_0_TO_4K = 0,  /* (0, 4K]      */
    IO_SIZE_4K_TO_32K,    /* (4K, 32K]    */
    IO_SIZE_32K_TO_128K,  /* (32K, 128K]  */
    IO_SIZE_128K_TO_512K, /* (128K, 512K) */
    IO_SIZE_512K_TO_MAX, /*  [512K, +∞)   */
    IO_SIZE_MAX /*Record total IO, regardless of size */
};

/* IO operation type */
enum io_op_type {
    OP_READ = 0,
    OP_WRITE,
    OP_MAX
};

enum layer_type {
    IN_DRIVER = 0,/* The time consumption of IO at the driver layer */
    IN_BLOCK,     /* The time consumption of IO in the block layer  */
    LAYER_MAX
};

//Ensure cache line alignment
struct blk_metrics_struct {
    /* The timestamp at which the statistics started */
    u64 timestamp;
    /* IO Count */
    u64 total_cnt;
    /* Total IO traffic */
    u64 total_size;
    /* Maximum time consumption of block+driver */
    u64 max_time;
    struct {
        /* Accumulated time consumption statistics */
        u64 elapse_time;
        /* Maximum time consumption statistics */
        u64 max_time;
    } layer[LAYER_MAX];//Count the block and driver layers separately
};

extern bool block_rq_issue_enabled;
extern bool block_rq_complete_enabled;
extern struct blk_metrics_struct blk_metrics[OP_MAX][CYCLE_MAX][IO_SIZE_MAX];
extern spinlock_t blk_metrics_lock[OP_MAX][CYCLE_MAX][IO_SIZE_MAX];

void block_register_tracepoint_probes(void);
void block_unregister_tracepoint_probes(void);
int block_metrics_proc_open(struct inode *inode, struct file *file);
void block_metrics_reset(void);
void block_metrics_init(void);

#endif /* __BLOCK_METRICS_H__ */