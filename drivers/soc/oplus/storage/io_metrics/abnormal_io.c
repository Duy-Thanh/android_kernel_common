#include "io_metrics_entry.h"
#include "abnormal_io.h"
#include "fs/f2fs/f2fs.h"
#include "fs/f2fs/segment.h"
#include "fs/f2fs/node.h"
#include <ufs/ufshcd.h>

#include <trace/events/f2fs.h>
#include <trace/events/block.h>
#include <trace/events/ufs.h>
#include <linux/preempt.h>

#define BUFFER_SIZE  (1<<20)
#define CACHELINE_SIZE 64
#define COUNTER_BIT_SIZE 20
#define TP_BIT_SIZE 8

#define DAY_TO_SECONDS(n) ((n) * 24 * 3600LL)
#define DUMP_MINIMUM_INTERVAL (5 * 60)
#define DUMP_LIMIT_1_DAY 10

atomic_t abnormal_io_enabled = ATOMIC_INIT(0);
bool abnormal_io_trigger;
atomic_t multiple_dump;
int abnormal_io_dump_min_interval_s;
int abnormal_io_dump_limit_1_day;
static time64_t last_dump_seconds;
static time64_t base_dump_seconds;

enum TP_t {
    F2FS_SUBMIT_PAGE_WRITE = 0,
    F2FS_SUBMIT_PAGE_BIO,
    F2FS_FILEMAP_FAULT,
    F2FS_SUBMIT_READ_BIO,
    F2FS_SUBMIT_WRITE_BIO,
    F2FS_MAP_BLOCKS,
    F2FS_DATAREAD_START,
    F2FS_DATAREAD_END,
    F2FS_DATAWRITE_START,
    F2FS_DATAWRITE_END,
    BLOCK_RQ_ISSUE,
    BLOCK_RQ_COMPLETE,
    BLOCK_GETRQ,
    BLOCK_BIO_FRONTMERGE,
    BLOCK_BIO_BACKMERGE,
    BLOCK_BIO_REMAP,
    UFSHCD_COMMAND
};

/* 24Byte */
struct entry_head_t {
    u64 magic; /* 0x0a63697274656d6f69 or "iometrics" */
    u64 timestamp;
    u32 counter: COUNTER_BIT_SIZE;
    u32 in_nmi: 1;
    u32 in_hardirq: 1;
    u32 in_softirq: 1;
    u32 in_atomic: 1;
    u32  tp_t: TP_BIT_SIZE;
    int pid;
};

/* The current structure cannot modify specific structures in tb by more than 40B,
otherwise it will affect performance*/
struct entry_t {
    struct entry_head_t head;
    union {
        char pad[CACHELINE_SIZE - sizeof(struct entry_head_t)];
        struct {
            int major;
            int minor;
            u64 ino;
            u64 index;
            u64 old_blkaddr;
            u64 new_blkaddr;
        } f2fs_submit_page_write; /* (struct page *page, struct f2fs_io_info *fio) */
        struct {
            int major;
            int minor;
            u64 ino;
            u64 index;
            int op;
            int op_flags;
            u8 type;
            u8 temp;
            u8 pad[6];
        } f2fs_submit_page_bio;   /* (struct page *page, struct f2fs_io_info *fio) */
        struct {
            int major;
            int minor;
            u64 ino;
            u64 index;
            u64 ret;
            u64 pad;
        } f2fs_filemap_fault;     /* (struct inode *inode, pgoff_t index, u64 ret) */
        struct {
            int major;
            int minor;
            sector_t sector;
            int op;
            int op_flags;
            int type;
            u32 size;
            u8 pad[8];
        } f2fs_submit_read_bio;   /* (struct super_block *sb, int type, struct bio *bio) */
        struct {
            int major;
            int minor;
            sector_t sector;
            int op;
            int op_flags;
            int type;
            u32 size;
            u8 pad[8];
        } f2fs_submit_write_bio;  /* (struct super_block *sb, int type, struct bio *bio) */
#define PATHNAME_LEN    16
        struct {
            u64 offset;
            u64 ino;
            int bytes;
            pid_t pid;
            u8 pathname[PATHNAME_LEN];
        } f2fs_dataread_start;   /* (struct inode *inode, loff_t offset, int bytes, pid_t pid,
                                    char *pathname, char *command) */
        struct {
            u64 offset;
            u64 ino;
            int bytes;
            u8 pad[20];
        } f2fs_dataread_end;     /* (struct inode *inode, loff_t offset, int bytes) */
        struct {
            u64 offset;
            u64 ino;
            int bytes;
            pid_t pid;
            u8 pathname[PATHNAME_LEN];
        } f2fs_datawrite_start;  /* (struct inode *inode, loff_t offset, int bytes, pid_t pid,
                                    char *pathname, char *command) */
        struct {
            u64 offset;
            u64 ino;
            int bytes;
            u8 pad[20];
        } f2fs_datawrite_end;    /* (struct inode *inode, loff_t offset, int bytes) */
        struct {
            int major;
            int minor;
            sector_t sector;
            u8 rwbs[RWBS_LEN];   /* 8B */
            u32 bytes;
            u8 pad[12];
        } block_rq_issue;        /* (struct request *rq) */
        struct {
            int major;
            int minor;
            sector_t sector;
            u8 rwbs[RWBS_LEN];   /* 8B */
            u64 elapsed;
            u32 bytes;
            int error;
        } block_rq_complete;     /* (struct request *rq, int error, u32 nr_bytes) */
        struct {
            sector_t sector;
            u8 comm[TASK_COMM_LEN];
            u8 rwbs[RWBS_LEN];   /* 8B */
            u32 bytes;
            u8 pad[4];
        } block_getrq;           /* (struct bio *bio) */
        struct {
            sector_t sector;
            u8 rwbs[RWBS_LEN];   /* 8B */
            u32 bytes;
            u8 pad[20];
        } block_bio_frontmerge;  /* (struct bio *bio) */
        struct {
            sector_t sector;
            u8 rwbs[RWBS_LEN];   /* 8B */
            u32 bytes;
            u8 pad[20];
        } block_bio_backmerge;   /* (struct bio *bio) */
        struct {
            int old_major;
            int old_minor;
            int new_major;
            int new_minor;
            sector_t old_sector;
            sector_t new_sector;
            u32 bytes;
            u8 pad[4];
        } block_bio_remap;       /* (struct bio *bio, dev_t dev, sector_t from) */
        struct {
            u64 lba;
            u64 elapsed;
            u32 tag;
            u32 doorbell;
            int transfer_len;
            u32 intr;
            u8 opcode;
            u8 group_id;
            u8 str_t;
            u8 gear_rx: 4;
            u8 gear_tx: 4;
            u8 lane_rx: 4;
            u8 lane_tx: 4;
            u8 pwr_rx: 4;
            u8 pwr_tx: 4;
            u8 hwq_id;
            u8 pad[1];
        } ufshcd_command;       /* (const char *dev_name, enum ufs_trace_str_t str_t, u32 tag,
                     u32 doorbell, int transfer_len, u32 intr, u64 lba, u8 opcode, u8 group_id)*/
    } tp;
};

struct ring_buffer_tail {
    u64 ktime_ns;
    time64_t utc_seconds; /* seconds since 1970 */
    int writers;
};
struct ring_buffer_t {
    u32 size;
    u32 mask;
    atomic_t cursor;
    atomic_t writers;
    char pad[48];
    char data[BUFFER_SIZE + sizeof(struct ring_buffer_tail)];
} ring_buffer __attribute__((aligned(CACHELINE_SIZE)));

void ring_buffer_int(void)
{
    ring_buffer.size = BUFFER_SIZE;
    ring_buffer.mask = (ring_buffer.size >> 6) - 1;
    atomic_set(&ring_buffer.cursor, -1);
    atomic_set(&ring_buffer.writers, 0);
}

static __always_inline struct entry_t *get_pentry_from_ring_buffer(enum TP_t tp)
{
    struct entry_t* pentry = (struct entry_t*)ring_buffer.data;
    u32 tmp = 0;
    u32 slot = 0;

    tmp = (u32)atomic_inc_return(&ring_buffer.cursor);
    slot = ring_buffer.mask & tmp;
    pentry = pentry + slot;
    pentry->head.timestamp = ktime_get_ns();
    pentry->head.magic = 0x63697274656d6f69;
    pentry->head.counter = tmp;
    pentry->head.tp_t = tp;
    pentry->head.pid = current->pid;
    pentry->head.in_nmi = in_nmi() ? 1 : 0;
    pentry->head.in_hardirq = in_hardirq() ? 1 : 0;
    pentry->head.in_softirq = in_softirq() ? 1 : 0;
    pentry->head.in_atomic = in_atomic() ? 1 : 0;

    return pentry;
}

static  __always_inline void ring_buffer_writer_inc(void)
{
    atomic_inc(&ring_buffer.writers);
}

static  __always_inline void ring_buffer_writer_dec(void)
{
    atomic_dec(&ring_buffer.writers);
}

static int dump_data_to_file(const char *logpath)
{
    struct file *fp = NULL;
    int ret = 0;
    loff_t offset = 0;
    struct ring_buffer_tail *ptail = NULL;

    ptail = (struct ring_buffer_tail *)(ring_buffer.data + BUFFER_SIZE);
    ptail->ktime_ns = ktime_get_ns();
    ptail->utc_seconds = ktime_get_real_seconds();
    ptail->writers = atomic_read(&ring_buffer.writers);
    io_metrics_print("writers: %d\n", ptail->writers);
    fp = filp_open(logpath, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (IS_ERR(fp)) {
        ret = PTR_ERR(fp);
    } else {
        ret = kernel_write(fp, ring_buffer.data, BUFFER_SIZE + sizeof(struct ring_buffer_tail), &offset);
        if (!ret) {
           io_metrics_print("kernel_write err: %d\n", ret);
        }
        filp_close(fp, NULL);
    }
    ring_buffer_int();

    return ret;
}

void record_f2fs_submit_page_write(void *ignore, struct page *page, struct f2fs_io_info *fio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_SUBMIT_PAGE_WRITE);
    pentry->tp.f2fs_submit_page_write.major = MAJOR(page_file_mapping(page)->host->i_sb->s_dev);
    pentry->tp.f2fs_submit_page_write.minor = MINOR(page_file_mapping(page)->host->i_sb->s_dev);
    pentry->tp.f2fs_submit_page_write.ino = page_file_mapping(page)->host->i_ino;
    pentry->tp.f2fs_submit_page_write.index = (u64)page->index;
    pentry->tp.f2fs_submit_page_write.old_blkaddr = (u64)fio->old_blkaddr;
    pentry->tp.f2fs_submit_page_write.new_blkaddr = (u64)fio->new_blkaddr;
    ring_buffer_writer_dec();
}
void record_f2fs_submit_page_bio(void *ignore, struct page *page, struct f2fs_io_info *fio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_SUBMIT_PAGE_BIO);
    pentry->tp.f2fs_submit_page_bio.major = MAJOR(page_file_mapping(page)->host->i_sb->s_dev);
    pentry->tp.f2fs_submit_page_bio.minor = MINOR(page_file_mapping(page)->host->i_sb->s_dev);
    pentry->tp.f2fs_submit_page_bio.ino = page_file_mapping(page)->host->i_ino;
    pentry->tp.f2fs_submit_page_bio.index = (u64)page->index;
    pentry->tp.f2fs_submit_page_bio.op = fio->op;
    pentry->tp.f2fs_submit_page_bio.op_flags = fio->op_flags;
    pentry->tp.f2fs_submit_page_bio.type = (u8)fio->type;
    pentry->tp.f2fs_submit_page_bio.temp = (u8)fio->temp;
    ring_buffer_writer_dec();
}
void record_f2fs_filemap_fault(void *ignore, struct inode *inode, pgoff_t index, unsigned long ret)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_FILEMAP_FAULT);
    pentry->tp.f2fs_filemap_fault.major = MAJOR(inode->i_sb->s_dev);
    pentry->tp.f2fs_filemap_fault.minor = MINOR(inode->i_sb->s_dev);
    pentry->tp.f2fs_filemap_fault.ino = inode->i_ino;
    pentry->tp.f2fs_filemap_fault.index = index;
    pentry->tp.f2fs_filemap_fault.ret = ret;
    ring_buffer_writer_dec();
}
void record_f2fs_submit_read_bio(void *ignore, struct super_block *sb, int type, struct bio *bio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_SUBMIT_READ_BIO);
    pentry->tp.f2fs_submit_read_bio.major = MAJOR(sb->s_dev);
    pentry->tp.f2fs_submit_read_bio.minor = MINOR(sb->s_dev);
    pentry->tp.f2fs_submit_read_bio.sector = bio->bi_iter.bi_sector;
    pentry->tp.f2fs_submit_read_bio.op = bio_op(bio);
    pentry->tp.f2fs_submit_read_bio.op_flags = bio->bi_opf;
    pentry->tp.f2fs_submit_read_bio.type = type;
    pentry->tp.f2fs_submit_read_bio.size = bio->bi_iter.bi_size;
    ring_buffer_writer_dec();
}
void record_f2fs_submit_write_bio(void *ignore, struct super_block *sb, int type, struct bio *bio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_SUBMIT_WRITE_BIO);
    pentry->tp.f2fs_submit_write_bio.major = MAJOR(sb->s_dev);
    pentry->tp.f2fs_submit_write_bio.minor = MINOR(sb->s_dev);
    pentry->tp.f2fs_submit_write_bio.sector = bio->bi_iter.bi_sector;
    pentry->tp.f2fs_submit_write_bio.op = bio_op(bio);
    pentry->tp.f2fs_submit_write_bio.op_flags = bio->bi_opf;
    pentry->tp.f2fs_submit_write_bio.type = type;
    pentry->tp.f2fs_submit_write_bio.size = bio->bi_iter.bi_size;
    ring_buffer_writer_dec();
}

void record_f2fs_dataread_start(void *ignore, struct inode *inode, loff_t offset, int bytes,
                                      pid_t pid, char *pathname, char *command)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_DATAREAD_START);
    pentry->tp.f2fs_dataread_start.offset = offset;
    pentry->tp.f2fs_dataread_start.ino = inode->i_ino;
    pentry->tp.f2fs_dataread_start.bytes = bytes;
    pentry->tp.f2fs_dataread_start.pid = pid;
    strncpy(pentry->tp.f2fs_dataread_start.pathname, pathname, PATHNAME_LEN-1);
    pentry->tp.f2fs_dataread_start.pathname[PATHNAME_LEN-1] = 0;
    ring_buffer_writer_dec();
}

void record_f2fs_dataread_end(void *ignore, struct inode *inode, loff_t offset, int bytes)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_DATAREAD_END);
    pentry->tp.f2fs_dataread_end.offset = offset;
    pentry->tp.f2fs_dataread_end.ino = inode->i_ino;
    pentry->tp.f2fs_dataread_end.bytes = bytes;
    ring_buffer_writer_dec();
}

void record_f2fs_datawrite_start(void *ignore, struct inode *inode, loff_t offset, int bytes,
                                      pid_t pid, char *pathname, char *command)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_DATAWRITE_START);
    pentry->tp.f2fs_datawrite_start.offset = offset;
    pentry->tp.f2fs_datawrite_start.ino = inode->i_ino;
    pentry->tp.f2fs_datawrite_start.bytes = bytes;
    pentry->tp.f2fs_datawrite_start.pid = pid;
    strncpy(pentry->tp.f2fs_datawrite_start.pathname, pathname, PATHNAME_LEN-1);
    pentry->tp.f2fs_datawrite_start.pathname[PATHNAME_LEN-1] = 0;
    ring_buffer_writer_dec();
}

void record_f2fs_datawrite_end(void *ignore, struct inode *inode, loff_t offset, int bytes)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(F2FS_DATAWRITE_END);
    pentry->tp.f2fs_datawrite_end.offset = offset;
    pentry->tp.f2fs_datawrite_end.ino = inode->i_ino;
    pentry->tp.f2fs_datawrite_end.bytes = bytes;
    ring_buffer_writer_dec();
}

void record_block_rq_issue(void *ignore, struct request *rq)
{
    struct entry_t *pentry = NULL;
    dev_t dev = rq->part ? (rq->part->bd_disk ? disk_devt(rq->part->bd_disk) : 0) : 0;
    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_RQ_ISSUE);
    pentry->tp.block_rq_issue.major = MAJOR(dev);
    pentry->tp.block_rq_issue.minor = MINOR(dev);
    pentry->tp.block_rq_issue.sector = blk_rq_trace_sector(rq);
    pentry->tp.block_rq_issue.bytes = blk_rq_bytes(rq);
    blk_fill_rwbs(pentry->tp.block_rq_issue.rwbs, rq->cmd_flags);
    ring_buffer_writer_dec();
}

static void record_block_rq_complete(void *ignore, struct request *rq,
                      blk_status_t error, unsigned int nr_bytes)
{
    struct entry_t *pentry = NULL;
    u64 io_complete_time_ns = ktime_get_ns();
        dev_t dev = rq->part ? (rq->part->bd_disk ? disk_devt(rq->part->bd_disk) : 0) : 0;
    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_RQ_COMPLETE);
    pentry->tp.block_rq_complete.major = MAJOR(dev);
    pentry->tp.block_rq_complete.minor = MINOR(dev);
    pentry->tp.block_rq_complete.sector = blk_rq_trace_sector(rq);
    pentry->tp.block_rq_complete.bytes = nr_bytes;
    pentry->tp.block_rq_complete.error = error;
    pentry->tp.block_rq_complete.elapsed = (rq->start_time_ns && (io_complete_time_ns > rq->start_time_ns)) ?
                                           (io_complete_time_ns - rq->start_time_ns) : 0;
    blk_fill_rwbs(pentry->tp.block_rq_complete.rwbs, rq->cmd_flags);
    ring_buffer_writer_dec();
}

void record_block_getrq(void *ignore, struct bio *bio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_GETRQ);
    pentry->tp.block_getrq.sector = bio->bi_iter.bi_sector;
    pentry->tp.block_getrq.bytes = bio_sectors(bio) << 9;
    blk_fill_rwbs(pentry->tp.block_getrq.rwbs, bio->bi_opf);
    memcpy(pentry->tp.block_getrq.comm, current->comm, TASK_COMM_LEN);
    ring_buffer_writer_dec();
}

void record_block_bio_frontmerge(void *ignore, struct bio *bio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_BIO_FRONTMERGE);
    pentry->tp.block_bio_frontmerge.sector = bio->bi_iter.bi_sector;
    pentry->tp.block_bio_frontmerge.bytes = bio_sectors(bio) << 9;
    blk_fill_rwbs(pentry->tp.block_bio_frontmerge.rwbs, bio->bi_opf);
    ring_buffer_writer_dec();
}

void record_block_bio_backmerge(void *ignore, struct bio *bio)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_BIO_BACKMERGE);
    pentry->tp.block_bio_backmerge.sector = bio->bi_iter.bi_sector;
    pentry->tp.block_bio_backmerge.bytes = bio_sectors(bio) << 9;
    blk_fill_rwbs(pentry->tp.block_bio_backmerge.rwbs, bio->bi_opf);
    ring_buffer_writer_dec();
}

void record_block_bio_remap(void *ignore, struct bio *bio, dev_t dev, sector_t from)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(BLOCK_BIO_REMAP);
    pentry->tp.block_bio_remap.old_major = MAJOR(dev);
    pentry->tp.block_bio_remap.old_minor = MINOR(dev);
    pentry->tp.block_bio_remap.new_major = MAJOR(bio_dev(bio));
    pentry->tp.block_bio_remap.new_minor = MINOR(bio_dev(bio));
    pentry->tp.block_bio_remap.old_sector = from;
    pentry->tp.block_bio_remap.new_sector = bio->bi_iter.bi_sector;
    pentry->tp.block_bio_remap.bytes = bio_sectors(bio) << 9;
    ring_buffer_writer_dec();
}

void record_ufshcd_command(void *ignore, const char *dev_name, enum ufs_trace_str_t str_t,
                    unsigned int tag, u32 doorbell, u32 hwq_id, int transfer_len, u32 intr,
                    u64 lba, u8 opcode, u8 group_id)
{
    struct entry_t *pentry = NULL;

    if (unlikely(atomic_read(&multiple_dump))) {
        return;
    }
    ring_buffer_writer_inc();
    pentry = get_pentry_from_ring_buffer(UFSHCD_COMMAND);
    pentry->tp.ufshcd_command.str_t = (u8)str_t;
    pentry->tp.ufshcd_command.tag = tag;
    pentry->tp.ufshcd_command.doorbell = doorbell;
    pentry->tp.ufshcd_command.transfer_len = transfer_len;
    pentry->tp.ufshcd_command.intr = intr;
    pentry->tp.ufshcd_command.lba = lba;
    pentry->tp.ufshcd_command.opcode = opcode;
    pentry->tp.ufshcd_command.hwq_id = (u8)hwq_id;
    pentry->tp.ufshcd_command.group_id = group_id;
    ring_buffer_writer_dec();
}

void abnormal_io_register_tracepoint_probes(void)
{
    int ret;

    ret = register_trace_f2fs_submit_page_write(record_f2fs_submit_page_write, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_submit_page_bio(record_f2fs_submit_page_bio, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_filemap_fault(record_f2fs_filemap_fault, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_submit_read_bio(record_f2fs_submit_read_bio, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_submit_write_bio(record_f2fs_submit_write_bio, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_dataread_start(record_f2fs_dataread_start, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_dataread_end(record_f2fs_dataread_end, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_datawrite_start(record_f2fs_datawrite_start, NULL);
    WARN_ON(ret);
    ret = register_trace_f2fs_datawrite_end(record_f2fs_datawrite_end, NULL);
    WARN_ON(ret);
    ret = register_trace_block_rq_issue(record_block_rq_issue, NULL);
    WARN_ON(ret);
    ret = register_trace_block_rq_complete(record_block_rq_complete, NULL);
    WARN_ON(ret);
    ret = register_trace_block_getrq(record_block_getrq, NULL);
    WARN_ON(ret);
    ret = register_trace_block_bio_frontmerge(record_block_bio_frontmerge, NULL);
    WARN_ON(ret);
    ret = register_trace_block_bio_backmerge(record_block_bio_backmerge, NULL);
    WARN_ON(ret);
    ret = register_trace_block_bio_remap(record_block_bio_remap, NULL);
    WARN_ON(ret);
    ret = register_trace_ufshcd_command(record_ufshcd_command, NULL);
    WARN_ON(ret);
}

void abnormal_io_unregister_tracepoint_probes(void)
{
    unregister_trace_f2fs_submit_page_write(record_f2fs_submit_page_write, NULL);
    unregister_trace_f2fs_submit_page_bio(record_f2fs_submit_page_bio, NULL);
    unregister_trace_f2fs_filemap_fault(record_f2fs_filemap_fault, NULL);
    unregister_trace_f2fs_submit_read_bio(record_f2fs_submit_read_bio, NULL);
    unregister_trace_f2fs_submit_write_bio(record_f2fs_submit_write_bio, NULL);
    unregister_trace_f2fs_dataread_start(record_f2fs_dataread_start, NULL);
    unregister_trace_f2fs_dataread_end(record_f2fs_dataread_end, NULL);
    unregister_trace_f2fs_datawrite_start(record_f2fs_datawrite_start, NULL);
    unregister_trace_f2fs_datawrite_end(record_f2fs_datawrite_end, NULL);
    unregister_trace_block_rq_issue(record_block_rq_issue, NULL);
    unregister_trace_block_rq_complete(record_block_rq_complete, NULL);
    unregister_trace_block_getrq(record_block_getrq, NULL);
    unregister_trace_block_bio_frontmerge(record_block_bio_frontmerge, NULL);
    unregister_trace_block_bio_backmerge(record_block_bio_backmerge, NULL);
    unregister_trace_block_bio_remap(record_block_bio_remap, NULL);
    unregister_trace_ufshcd_command(record_ufshcd_command, NULL);
    tracepoint_synchronize_unregister();
}

static bool over_limit_dump(time64_t current_time_seconds)
{
    static int dump_limit_1_day = 0;

    /* Count again when the export log cycle exceeds 1 day */
    if ((current_time_seconds - base_dump_seconds) >= DAY_TO_SECONDS(1)) {
        base_dump_seconds = current_time_seconds;
        dump_limit_1_day = 1;
    } else {
        /* When the number of exports per day exceeds the upper limit, no further
        exports will be made */
        if (dump_limit_1_day <= abnormal_io_dump_limit_1_day) {
            dump_limit_1_day++;
        } else {
            return true;
        }
    }

    return false;
}

int abnormal_io_dump_to_file(const char *logpath)
{
    int ret = 0;
    time64_t current_time_seconds = ktime_get_boottime_seconds();

    if (!atomic_read(&abnormal_io_enabled)) {
        io_metrics_print("abnormal_io_enabled is disabled,please enable first");
        return ret;
    }
    /* Avoid multiple people exporting at the same time */
    if (1 == atomic_inc_return(&multiple_dump)) {
        if (((current_time_seconds - last_dump_seconds) > abnormal_io_dump_min_interval_s) &&
                                      (!over_limit_dump(current_time_seconds))) {
            last_dump_seconds = current_time_seconds;
            ret = dump_data_to_file(logpath);
        }
    }
    atomic_dec(&multiple_dump);

    return ret;
}
void abnormal_io_init(void)
{
    if (1 != atomic_inc_return(&abnormal_io_enabled)) {
        atomic_dec(&abnormal_io_enabled);
        io_metrics_print("abnormal_io has registerd:%d\n", atomic_read(&abnormal_io_enabled));
        return;
    }
    abnormal_io_trigger = false;
    atomic_set(&multiple_dump, 0);
    last_dump_seconds = 0;
    base_dump_seconds = 0;
    /* The default number of times to export logs in a day cannot exceed 10 times */
    abnormal_io_dump_limit_1_day = DUMP_LIMIT_1_DAY;
    /* The default time interval between two log exports should not be less than 5 minutes */
    abnormal_io_dump_min_interval_s = DUMP_MINIMUM_INTERVAL;
    ring_buffer_int();
    abnormal_io_register_tracepoint_probes();
    if (sizeof(struct entry_t) != CACHELINE_SIZE) {
        io_metrics_print("sizeof(struct entry_t): %d", (int)sizeof(struct entry_t));
    } else {
        io_metrics_print("ok");
    }
    io_metrics_print("init\n");
}

void abnormal_io_exit(void)
{
    if (0 != atomic_dec_return(&abnormal_io_enabled)) {
        atomic_inc(&abnormal_io_enabled);
        io_metrics_print("abnormal_io has unregisterd:%d\n", atomic_read(&abnormal_io_enabled));
        return;
    }
    abnormal_io_unregister_tracepoint_probes();
    io_metrics_print("exit\n");
}
