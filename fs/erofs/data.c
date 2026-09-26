// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include "internal.h"
#include <linux/sched/mm.h>
#include <trace/events/erofs.h>

void erofs_unmap_metabuf(struct erofs_buf *buf)
{
	if (!buf->base)
		return;
	kunmap_local(buf->base);
	buf->base = NULL;
}

void erofs_put_metabuf(struct erofs_buf *buf)
{
	if (!buf->page)
		return;
	erofs_unmap_metabuf(buf);
	folio_put(page_folio(buf->page));
	buf->page = NULL;
}

void *erofs_bread(struct erofs_buf *buf, erofs_off_t offset, bool need_kmap)
{
	pgoff_t index = (buf->off + offset) >> PAGE_SHIFT;
	struct folio *folio = NULL;

	if (buf->page) {
		folio = page_folio(buf->page);
		if (folio_file_page(folio, index) != buf->page)
			erofs_unmap_metabuf(buf);
	}
	if (!folio || !folio_contains(folio, index)) {
		erofs_put_metabuf(buf);
		folio = read_mapping_folio(buf->mapping, index, buf->file);
		if (IS_ERR(folio))
			return folio;
	}
	buf->page = folio_file_page(folio, index);
	if (buf->sb && erofs_sb_has_verity(EROFS_SB(buf->sb)) &&
	    EROFS_SB(buf->sb)->verity_ready &&
	    buf->mapping == buf->sb->s_bdev->bd_mapping) {
		int err = erofs_verify_metabuf_page(buf->sb, buf->page, index);

		if (unlikely(err)) {
			erofs_put_metabuf(buf);
			return ERR_PTR(err);
		}
	}
	if (!need_kmap)
		return NULL;
	if (!buf->base)
		buf->base = kmap_local_page(buf->page);
	return buf->base + (offset & ~PAGE_MASK);
}

int erofs_init_metabuf(struct erofs_buf *buf, struct super_block *sb,
		       bool in_metabox)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	buf->sb = sb;
	buf->file = NULL;
	if (in_metabox) {
		if (unlikely(!sbi->metabox_inode))
			return -EFSCORRUPTED;
		buf->mapping = sbi->metabox_inode->i_mapping;
		return 0;
	}
	buf->off = sbi->dif0.fsoff;
	if (erofs_is_fileio_mode(sbi)) {
		buf->file = sbi->dif0.file;	/* some fs like FUSE needs it */
		buf->mapping = buf->file->f_mapping;
	} else if (erofs_is_fscache_mode(sb))
		buf->mapping = sbi->dif0.fscache->inode->i_mapping;
	else
		buf->mapping = sb->s_bdev->bd_mapping;
	return 0;
}

void *erofs_read_metabuf(struct erofs_buf *buf, struct super_block *sb,
			 erofs_off_t offset, bool in_metabox)
{
	int err;

	err = erofs_init_metabuf(buf, sb, in_metabox);
	if (err)
		return ERR_PTR(err);
	return erofs_bread(buf, offset, true);
}

int erofs_map_blocks(struct inode *inode, struct erofs_map_blocks *map)
{
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct super_block *sb = inode->i_sb;
	unsigned int unit, blksz = sb->s_blocksize;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_chunk_index *idx;
	erofs_blk_t startblk, addrmask;
	bool tailpacking;
	erofs_off_t pos;
	u64 chunknr;
	int err = 0;

	trace_erofs_map_blocks_enter(inode, map, 0);
	map->m_deviceid = 0;
	map->m_flags = 0;
	if (map->m_la >= inode->i_size)
		goto out;

	if (vi->datalayout != EROFS_INODE_CHUNK_BASED) {
		tailpacking = (vi->datalayout == EROFS_INODE_FLAT_INLINE);
		if (!tailpacking && vi->startblk == EROFS_NULL_ADDR)
			goto out;
		pos = erofs_pos(sb, erofs_iblks(inode) - tailpacking);

		map->m_flags = EROFS_MAP_MAPPED;
		if (map->m_la < pos) {
			map->m_pa = erofs_pos(sb, vi->startblk) + map->m_la;
			map->m_llen = pos - map->m_la;
		} else {
			map->m_pa = erofs_iloc(inode) + vi->inode_isize +
				vi->xattr_isize + erofs_blkoff(sb, map->m_la);
			map->m_llen = inode->i_size - map->m_la;
			map->m_flags |= EROFS_MAP_META;
		}
		goto out;
	}

	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES)
		unit = sizeof(*idx);			/* chunk index */
	else
		unit = EROFS_BLOCK_MAP_ENTRY_SIZE;	/* block map */

	chunknr = map->m_la >> vi->chunkbits;
	pos = ALIGN(erofs_iloc(inode) + vi->inode_isize +
		    vi->xattr_isize, unit) + unit * chunknr;

	idx = erofs_read_metabuf(&buf, sb, pos, erofs_inode_in_metabox(inode));
	if (IS_ERR(idx)) {
		err = PTR_ERR(idx);
		goto out;
	}
	map->m_la = chunknr << vi->chunkbits;
	map->m_llen = min_t(erofs_off_t, 1UL << vi->chunkbits,
			    round_up(inode->i_size - map->m_la, blksz));
	if (vi->chunkformat & EROFS_CHUNK_FORMAT_INDEXES) {
		addrmask = (vi->chunkformat & EROFS_CHUNK_FORMAT_48BIT) ?
			BIT_ULL(48) - 1 : BIT_ULL(32) - 1;
		startblk = (((u64)le16_to_cpu(idx->startblk_hi) << 32) |
			    le32_to_cpu(idx->startblk_lo)) & addrmask;
		if ((startblk ^ EROFS_NULL_ADDR) & addrmask) {
			map->m_deviceid = le16_to_cpu(idx->device_id) &
				EROFS_SB(sb)->device_id_mask;
			map->m_pa = erofs_pos(sb, startblk);
			map->m_flags = EROFS_MAP_MAPPED;
		}
	} else {
		startblk = le32_to_cpu(*(__le32 *)idx);
		if (startblk != (u32)EROFS_NULL_ADDR) {
			map->m_pa = erofs_pos(sb, startblk);
			map->m_flags = EROFS_MAP_MAPPED;
		}
	}
	erofs_put_metabuf(&buf);
out:
	if (!err) {
		map->m_plen = map->m_llen;
		/* inline data should be located in the same meta block */
		if ((map->m_flags & EROFS_MAP_META) &&
		    erofs_blkoff(sb, map->m_pa) + map->m_plen > blksz) {
			erofs_err(sb, "inline data across blocks @ nid %llu", vi->nid);
			DBG_BUGON(1);
			return -EFSCORRUPTED;
		}
	}
	trace_erofs_map_blocks_exit(inode, map, 0, err);
	return err;
}

static void erofs_fill_from_devinfo(struct erofs_map_dev *map,
		struct super_block *sb, struct erofs_device_info *dif)
{
	map->m_sb = sb;
	map->m_dif = dif;
	map->m_bdev = NULL;
	if (dif->file && S_ISBLK(file_inode(dif->file)->i_mode))
		map->m_bdev = file_bdev(dif->file);
}

int erofs_map_dev(struct super_block *sb, struct erofs_map_dev *map)
{
	struct erofs_dev_context *devs = EROFS_SB(sb)->devs;
	struct erofs_device_info *dif;
	erofs_off_t startoff;
	int id;

	erofs_fill_from_devinfo(map, sb, &EROFS_SB(sb)->dif0);
	map->m_bdev = sb->s_bdev;	/* use s_bdev for the primary device */
	if (map->m_deviceid) {
		down_read(&devs->rwsem);
		dif = idr_find(&devs->tree, map->m_deviceid - 1);
		if (!dif) {
			up_read(&devs->rwsem);
			return -ENODEV;
		}
		if (devs->flatdev) {
			map->m_pa += erofs_pos(sb, dif->uniaddr);
			up_read(&devs->rwsem);
			return 0;
		}
		erofs_fill_from_devinfo(map, sb, dif);
		up_read(&devs->rwsem);
	} else if (devs->extra_devices && !devs->flatdev) {
		down_read(&devs->rwsem);
		idr_for_each_entry(&devs->tree, dif, id) {
			if (!dif->uniaddr)
				continue;

			startoff = erofs_pos(sb, dif->uniaddr);
			if (map->m_pa >= startoff &&
			    map->m_pa < startoff + erofs_pos(sb, dif->blocks)) {
				map->m_pa -= startoff;
				erofs_fill_from_devinfo(map, sb, dif);
				break;
			}
		}
		up_read(&devs->rwsem);
	}
	return 0;
}

/*
 * bit 30: I/O error occurred on this folio
 * bit 29: CPU has dirty data in D-cache (needs aliasing handling);
 * bit 0 - 29: remaining parts to complete this folio
 */
#define EROFS_ONLINEFOLIO_EIO		30
#define EROFS_ONLINEFOLIO_DIRTY		29

void erofs_onlinefolio_init(struct folio *folio)
{
	union {
		atomic_t o;
		void *v;
	} u = { .o = ATOMIC_INIT(1) };

	folio->private = u.v;	/* valid only if file-backed folio is locked */
}

void erofs_onlinefolio_split(struct folio *folio)
{
	atomic_inc((atomic_t *)&folio->private);
}

void erofs_onlinefolio_end(struct folio *folio, int err, bool dirty)
{
	int orig, v;

	do {
		orig = atomic_read((atomic_t *)&folio->private);
		DBG_BUGON(orig <= 0);
		v = dirty << EROFS_ONLINEFOLIO_DIRTY;
		v |= (orig - 1) | (!!err << EROFS_ONLINEFOLIO_EIO);
	} while (atomic_cmpxchg((atomic_t *)&folio->private, orig, v) != orig);

	if (v & (BIT(EROFS_ONLINEFOLIO_DIRTY) - 1))
		return;
	folio->private = 0;
	if (v & BIT(EROFS_ONLINEFOLIO_DIRTY))
		flush_dcache_folio(folio);
	folio_end_read(folio, !(v & BIT(EROFS_ONLINEFOLIO_EIO)));
}

static int erofs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	int ret;
	struct super_block *sb = inode->i_sb;
	struct erofs_map_blocks map;
	struct erofs_map_dev mdev;

	map.m_la = offset;
	map.m_llen = length;
	ret = erofs_map_blocks(inode, &map);
	if (ret < 0)
		return ret;

	iomap->offset = map.m_la;
	iomap->length = map.m_llen;
	iomap->flags = 0;
	iomap->private = NULL;
	iomap->addr = IOMAP_NULL_ADDR;
	if (!(map.m_flags & EROFS_MAP_MAPPED)) {
		iomap->type = IOMAP_HOLE;
		return 0;
	}

	if (!(map.m_flags & EROFS_MAP_META) || !erofs_inode_in_metabox(inode)) {
		mdev = (struct erofs_map_dev) {
			.m_deviceid = map.m_deviceid,
			.m_pa = map.m_pa,
		};
		ret = erofs_map_dev(sb, &mdev);
		if (ret)
			return ret;

		if (flags & IOMAP_DAX)
			iomap->dax_dev = mdev.m_dif->dax_dev;
		else
			iomap->bdev = mdev.m_bdev;
		iomap->addr = mdev.m_dif->fsoff + mdev.m_pa;
		if (flags & IOMAP_DAX)
			iomap->addr += mdev.m_dif->dax_part_off;
	}

	if (map.m_flags & EROFS_MAP_META) {
		void *ptr;
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;

		iomap->type = IOMAP_INLINE;
		ptr = erofs_read_metabuf(&buf, sb, map.m_pa,
					 erofs_inode_in_metabox(inode));
		if (IS_ERR(ptr))
			return PTR_ERR(ptr);
		iomap->inline_data = ptr;
		iomap->private = buf.base;
	} else {
		iomap->type = IOMAP_MAPPED;
	}
	return 0;
}

static int erofs_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	void *ptr = iomap->private;

	if (ptr) {
		struct erofs_buf buf = {
			.page = kmap_to_page(ptr),
			.base = ptr,
		};

		DBG_BUGON(iomap->type != IOMAP_INLINE);
		erofs_put_metabuf(&buf);
	} else {
		DBG_BUGON(iomap->type == IOMAP_INLINE);
	}
	return written;
}

static const struct iomap_ops erofs_iomap_ops = {
	.iomap_begin = erofs_iomap_begin,
	.iomap_end = erofs_iomap_end,
};

int erofs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 start, u64 len)
{
	if (erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout)) {
#ifdef CONFIG_EROFS_FS_ZIP
		return iomap_fiemap(inode, fieinfo, start, len,
				    &z_erofs_iomap_report_ops);
#else
		return -EOPNOTSUPP;
#endif
	}
	return iomap_fiemap(inode, fieinfo, start, len, &erofs_iomap_ops);
}

#define EROFS_FLAT_RA_BATCH	16

struct erofs_flat_bio_ctx {
	struct super_block *sb;
	struct bio *bio;
	unsigned int nr_pages;
	union {
		struct kthread_work kwork;
		struct work_struct work;
	};
	u8 digests[][32];
};

struct erofs_flat_sync_ctx {
	struct completion *done;
	blk_status_t status;
};

static inline bool erofs_is_flat_data_verity(struct inode *inode)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);
	struct erofs_inode *vi = EROFS_I(inode);

	return erofs_sb_has_verity(sbi) && S_ISREG(inode->i_mode) &&
	       vi->datalayout == EROFS_INODE_FLAT_PLAIN &&
	       vi->startblk != EROFS_NULL_ADDR &&
	       vi->startblk < sbi->meta_verity_startblk &&
	       inode->i_sb->s_blocksize == PAGE_SIZE &&
	       !erofs_is_fileio_mode(sbi) &&
	       !erofs_is_fscache_mode(inode->i_sb);
}

static inline erofs_off_t erofs_flat_digest_pos(struct inode *inode,
						pgoff_t lblk)
{
	struct erofs_inode *vi = EROFS_I(inode);

	return round_up(erofs_iloc(inode) + vi->inode_isize + vi->xattr_isize,
			8) + (erofs_off_t)lblk * 32;
}

static bool erofs_try_get_warm_digest(struct inode *inode,
				      erofs_off_t digest_pos, u8 *out_digest)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	erofs_off_t abs_pos = sbi->dif0.fsoff + digest_pos;
	pgoff_t index = abs_pos >> PAGE_SHIFT;
	u32 rel_blk = index - sbi->meta_verity_startblk;
	struct folio *mfolio;
	void *kaddr;

	if (erofs_inode_in_metabox(inode) ||
	    unlikely((abs_pos & ~PAGE_MASK) > PAGE_SIZE - 32) ||
	    rel_blk >= sbi->meta_verity_blocks ||
	    !test_bit(rel_blk, sbi->meta_verified_bitmap))
		return false;

	mfolio = filemap_get_folio(sb->s_bdev->bd_mapping, index);
	if (IS_ERR(mfolio))
		return false;
	if (unlikely(!folio_test_uptodate(mfolio))) {
		folio_put(mfolio);
		return false;
	}
	kaddr = kmap_local_folio(mfolio, offset_in_folio(mfolio, abs_pos));
	memcpy(out_digest, kaddr, 32);
	kunmap_local(kaddr);
	folio_put(mfolio);
	return true;
}

static int erofs_read_flat_digest(struct inode *inode, struct erofs_buf *buf,
				  erofs_off_t digest_pos, u8 *out_digest)
{
	struct super_block *sb = inode->i_sb;
	bool in_mbox = erofs_inode_in_metabox(inode);
	unsigned int off_in_page = digest_pos & ~PAGE_MASK;
	void *ptr;

	ptr = erofs_read_metabuf(buf, sb, digest_pos, in_mbox);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	if (likely(off_in_page <= PAGE_SIZE - 32)) {
		memcpy(out_digest, ptr, 32);
		return 0;
	}
	{
		unsigned int part1 = PAGE_SIZE - off_in_page;

		memcpy(out_digest, ptr, part1);
		ptr = erofs_read_metabuf(buf, sb, digest_pos + part1, in_mbox);
		if (IS_ERR(ptr))
			return PTR_ERR(ptr);
		memcpy(out_digest + part1, ptr, 32 - part1);
		return 0;
	}
}

static void erofs_flat_verify_ctx_run(struct erofs_flat_bio_ctx *ctx)
{
	struct erofs_sb_info *sbi = EROFS_SB(ctx->sb);
	struct bio *bio = ctx->bio;
	struct folio_iter fi;
	struct folio *prev_folio = NULL;
	unsigned int i = 0;
	bool status_ok = (bio->bi_status == BLK_STS_OK);

	bio_for_each_folio_all(fi, bio) {
		if (!status_ok || i >= ctx->nr_pages ||
		    folio_size(fi.folio) != PAGE_SIZE) {
			if (prev_folio) {
				bool ok = !erofs_verify_data_folio(ctx->sb,
								   prev_folio,
								   ctx->digests[i - 1]);
				folio_end_read(prev_folio, ok);
				prev_folio = NULL;
			}
			folio_end_read(fi.folio, false);
			++i;
			continue;
		}
		if (!prev_folio) {
			prev_folio = fi.folio;
			++i;
			continue;
		}
		{
			u8 actual0[32], actual1[32];
			const u8 *addr0 = kmap_local_folio(prev_folio, 0);
			const u8 *addr1 = kmap_local_folio(fi.folio, 0);
			bool ok0, ok1;

			sha256_finup_2x(&sbi->verity_init_ctx, addr0, addr1,
					PAGE_SIZE, actual0, actual1);
			kunmap_local(addr1);
			kunmap_local(addr0);

			ok0 = (memcmp(actual0, ctx->digests[i - 1], 32) == 0);
			ok1 = (memcmp(actual1, ctx->digests[i], 32) == 0);
			if (unlikely(!ok0 || !ok1))
				erofs_err(ctx->sb, "verity: flat data corruption detected");
			folio_end_read(prev_folio, ok0);
			folio_end_read(fi.folio, ok1);
			prev_folio = NULL;
			++i;
		}
	}
	if (prev_folio) {
		bool ok = !erofs_verify_data_folio(ctx->sb, prev_folio,
						   ctx->digests[i - 1]);
		folio_end_read(prev_folio, ok);
	}
	bio_put(bio);
	kfree(ctx);
}

static void erofs_flat_verify_wq(struct work_struct *work)
{
	struct erofs_flat_bio_ctx *ctx =
		container_of(work, struct erofs_flat_bio_ctx, work);

	erofs_flat_verify_ctx_run(ctx);
}

static void erofs_flat_verify_kthread(struct kthread_work *kwork)
{
	struct erofs_flat_bio_ctx *ctx =
		container_of(kwork, struct erofs_flat_bio_ctx, kwork);

	erofs_flat_verify_ctx_run(ctx);
}

static void erofs_flat_bio_endio(struct bio *bio)
{
	struct erofs_flat_bio_ctx *ctx = bio->bi_private;
	static atomic_t worker_seq = ATOMIC_INIT(0);
	unsigned int seq, ncpus;
	int cpu;

	if (!bio->bi_status && ctx->nr_pages <= 4 && in_softirq()) {
		erofs_flat_verify_ctx_run(ctx);
		return;
	}
	ncpus = max(1U, num_online_cpus());
	seq = (unsigned int)atomic_inc_return(&worker_seq);
	cpu = cpumask_nth(seq % ncpus, cpu_online_mask);
	if (unlikely((unsigned int)cpu >= nr_cpu_ids))
		cpu = raw_smp_processor_id();
	erofs_queue_verity_work(cpu, &ctx->kwork, erofs_flat_verify_kthread,
				&ctx->work, erofs_flat_verify_wq);
}

static void erofs_flat_sync_endio(struct bio *bio)
{
	struct erofs_flat_sync_ctx *sctx = bio->bi_private;

	sctx->status = bio->bi_status;
	complete(sctx->done);
}

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int erofs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct super_block *sb = inode->i_sb;

	trace_erofs_read_folio(folio, true);

	if (erofs_is_flat_data_verity(inode) && folio_size(folio) == PAGE_SIZE) {
		struct erofs_inode *vi = EROFS_I(inode);
		pgoff_t lblk = folio->index;
		erofs_off_t digest_pos = erofs_flat_digest_pos(inode, lblk);
		struct erofs_map_dev mdev = {
			.m_pa = erofs_pos(sb, vi->startblk + lblk),
		};
		u8 digest[32];
		struct bio *bio;

		if (folio_pos(folio) >= inode->i_size) {
			folio_zero_range(folio, 0, folio_size(folio));
			folio_end_read(folio, true);
			return 0;
		}
		if (erofs_map_dev(sb, &mdev)) {
			folio_end_read(folio, false);
			return 0;
		}

		if (erofs_try_get_warm_digest(inode, digest_pos, digest)) {
			struct erofs_flat_bio_ctx *ctx =
				kmalloc(struct_size(ctx, digests, 1), GFP_NOIO);

			if (likely(ctx)) {
				bio = bio_alloc(mdev.m_bdev, 1, REQ_OP_READ,
						GFP_NOIO);
				ctx->sb = sb;
				ctx->bio = bio;
				ctx->nr_pages = 1;
				memcpy(ctx->digests[0], digest, 32);
				bio->bi_iter.bi_sector =
					(mdev.m_dif->fsoff + mdev.m_pa) >>
					SECTOR_SHIFT;
				bio_add_folio_nofail(bio, folio, PAGE_SIZE, 0);
				bio->bi_private = ctx;
				bio->bi_end_io = erofs_flat_bio_endio;
				submit_bio(bio);
				return 0;
			}
		}

		/*
		 * Cold digest metadata page (e.g. after drop_caches): submit
		 * the data bio first so the block device fetches data and
		 * digest metadata concurrently in parallel.
		 */
		{
			DECLARE_COMPLETION_ONSTACK(done);
			struct erofs_flat_sync_ctx sctx = { .done = &done };
			struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
			int err;

			bio = bio_alloc(mdev.m_bdev, 1, REQ_OP_READ, GFP_NOIO);
			bio->bi_iter.bi_sector =
				(mdev.m_dif->fsoff + mdev.m_pa) >> SECTOR_SHIFT;
			bio_add_folio_nofail(bio, folio, PAGE_SIZE, 0);
			bio->bi_private = &sctx;
			bio->bi_end_io = erofs_flat_sync_endio;
			submit_bio(bio);

			err = erofs_read_flat_digest(inode, &buf, digest_pos,
						     digest);
			erofs_put_metabuf(&buf);

			wait_for_completion_io(&done);
			if (!err && sctx.status)
				err = blk_status_to_errno(sctx.status);
			if (!err)
				err = erofs_verify_data_folio(sb, folio, digest);
			folio_end_read(folio, !err);
			bio_put(bio);
			return 0;
		}
	}

	if (erofs_sb_has_verity(EROFS_SB(sb))) {
		struct erofs_map_blocks map = {
			.m_la = folio_pos(folio),
			.m_llen = folio_size(folio),
		};
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
		void *ptr;
		size_t copy_len;
		int err;

		err = erofs_map_blocks(inode, &map);
		if (err < 0) {
			folio_end_read(folio, false);
			return 0;
		}
		if (!(map.m_flags & EROFS_MAP_MAPPED)) {
			folio_zero_range(folio, 0, folio_size(folio));
			folio_end_read(folio, true);
			return 0;
		}
		ptr = erofs_read_metabuf(&buf, sb, map.m_pa,
					 erofs_inode_in_metabox(inode));
		if (IS_ERR(ptr)) {
			folio_end_read(folio, false);
			return 0;
		}
		copy_len = min_t(u64, folio_size(folio), map.m_llen);
		copy_len = min_t(size_t, copy_len,
				 sb->s_blocksize - erofs_blkoff(sb, map.m_pa));
		memcpy_to_folio(folio, 0, ptr, copy_len);
		if (copy_len < folio_size(folio))
			folio_zero_range(folio, copy_len,
					 folio_size(folio) - copy_len);
		erofs_put_metabuf(&buf);
		folio_end_read(folio, true);
		return 0;
	}

	return iomap_read_folio(folio, &erofs_iomap_ops);
}

static void erofs_prefetch_flat_digests(struct inode *inode, pgoff_t start_lblk)
{
	struct super_block *sb = inode->i_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct address_space *mapping = sb->s_bdev->bd_mapping;
	pgoff_t total_lblks = DIV_ROUND_UP_ULL(inode->i_size, PAGE_SIZE);
	erofs_off_t start_pos, end_pos;
	pgoff_t index, end_index;
	unsigned long ra_pages;

	if (erofs_inode_in_metabox(inode) || start_lblk >= total_lblks)
		return;
	start_pos = sbi->dif0.fsoff + erofs_flat_digest_pos(inode, start_lblk);
	end_pos = sbi->dif0.fsoff + erofs_flat_digest_pos(inode, total_lblks);
	index = start_pos >> PAGE_SHIFT;
	end_index = DIV_ROUND_UP_ULL(end_pos, PAGE_SIZE);
	if (end_index <= index + 1 || xa_load(&mapping->i_pages, index))
		return;
	ra_pages = min_t(unsigned long, end_index - index, 32);
	{
		DEFINE_READAHEAD(mrac, NULL, NULL, mapping, index);

		filemap_invalidate_lock_shared(mapping);
		page_cache_ra_unbounded(&mrac, ra_pages, 0);
		filemap_invalidate_unlock_shared(mapping);
	}
}

static void erofs_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct super_block *sb = inode->i_sb;

	trace_erofs_readahead(inode, readahead_index(rac),
					readahead_count(rac), true);

	if (erofs_is_flat_data_verity(inode)) {
		struct erofs_inode *vi = EROFS_I(inode);
		struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
		struct erofs_flat_bio_ctx *ctx = NULL;
		pgoff_t next_lblk = 0;
		struct folio *folio;
		u8 digest[32];

		erofs_prefetch_flat_digests(inode, readahead_index(rac));
		while ((folio = readahead_folio(rac))) {
			pgoff_t lblk = folio->index;
			erofs_off_t digest_pos;

			if (folio_size(folio) != PAGE_SIZE ||
			    folio_pos(folio) >= inode->i_size) {
				if (ctx) {
					submit_bio(ctx->bio);
					ctx = NULL;
				}
				erofs_read_folio(NULL, folio);
				continue;
			}

			if (ctx && (lblk != next_lblk ||
				    ctx->nr_pages >= EROFS_FLAT_RA_BATCH)) {
				submit_bio(ctx->bio);
				ctx = NULL;
			}

			digest_pos = erofs_flat_digest_pos(inode, lblk);
			if (erofs_read_flat_digest(inode, &buf, digest_pos,
						   digest)) {
				if (ctx) {
					submit_bio(ctx->bio);
					ctx = NULL;
				}
				folio_end_read(folio, false);
				continue;
			}

			if (!ctx) {
				struct erofs_map_dev mdev = {
					.m_pa = erofs_pos(sb, vi->startblk + lblk),
				};
				struct bio *bio;

				if (erofs_map_dev(sb, &mdev)) {
					folio_end_read(folio, false);
					continue;
				}
				ctx = kmalloc(struct_size(ctx, digests,
							  EROFS_FLAT_RA_BATCH),
					      GFP_NOIO);
				if (!ctx) {
					erofs_read_folio(NULL, folio);
					continue;
				}
				bio = bio_alloc(mdev.m_bdev, EROFS_FLAT_RA_BATCH,
						REQ_OP_READ | REQ_RAHEAD,
						GFP_NOIO);
				bio->bi_iter.bi_sector =
					(mdev.m_dif->fsoff + mdev.m_pa) >>
					SECTOR_SHIFT;
				bio->bi_private = ctx;
				bio->bi_end_io = erofs_flat_bio_endio;
				ctx->sb = sb;
				ctx->bio = bio;
				ctx->nr_pages = 0;
			}

			memcpy(ctx->digests[ctx->nr_pages], digest, 32);
			bio_add_folio_nofail(ctx->bio, folio, PAGE_SIZE, 0);
			++ctx->nr_pages;
			next_lblk = lblk + 1;
		}
		if (ctx)
			submit_bio(ctx->bio);
		erofs_put_metabuf(&buf);
		return;
	}

	if (erofs_sb_has_verity(EROFS_I_SB(inode))) {
		struct folio *folio;

		while ((folio = readahead_folio(rac)))
			erofs_read_folio(NULL, folio);
		return;
	}

	return iomap_readahead(rac, &erofs_iomap_ops);
}

static sector_t erofs_bmap(struct address_space *mapping, sector_t block)
{
	return iomap_bmap(mapping, block, &erofs_iomap_ops);
}

static ssize_t erofs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	/* no need taking (shared) inode lock since it's a ro filesystem */
	if (!iov_iter_count(to))
		return 0;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(inode))
		return dax_iomap_rw(iocb, to, &erofs_iomap_ops);
#endif
	if ((iocb->ki_flags & IOCB_DIRECT) && inode->i_sb->s_bdev)
		return iomap_dio_rw(iocb, to, &erofs_iomap_ops,
				    NULL, 0, NULL, 0);
	return filemap_read(iocb, to, 0);
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations erofs_aops = {
	.read_folio = erofs_read_folio,
	.readahead = erofs_readahead,
	.bmap = erofs_bmap,
	.direct_IO = noop_direct_IO,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};

#ifdef CONFIG_FS_DAX
static vm_fault_t erofs_dax_huge_fault(struct vm_fault *vmf,
		unsigned int order)
{
	return dax_iomap_fault(vmf, order, NULL, NULL, &erofs_iomap_ops);
}

static vm_fault_t erofs_dax_fault(struct vm_fault *vmf)
{
	return erofs_dax_huge_fault(vmf, 0);
}

static const struct vm_operations_struct erofs_dax_vm_ops = {
	.fault		= erofs_dax_fault,
	.huge_fault	= erofs_dax_huge_fault,
};

static int erofs_file_mmap_prepare(struct vm_area_desc *desc)
{
	if (!IS_DAX(file_inode(desc->file)))
		return generic_file_readonly_mmap_prepare(desc);

	if ((desc->vm_flags & VM_SHARED) && (desc->vm_flags & VM_MAYWRITE))
		return -EINVAL;

	desc->vm_ops = &erofs_dax_vm_ops;
	desc->vm_flags |= VM_HUGEPAGE;
	return 0;
}
#else
#define erofs_file_mmap_prepare	generic_file_readonly_mmap_prepare
#endif

static loff_t erofs_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	const struct iomap_ops *ops = &erofs_iomap_ops;

	if (erofs_inode_is_data_compressed(EROFS_I(inode)->datalayout))
#ifdef CONFIG_EROFS_FS_ZIP
		ops = &z_erofs_iomap_report_ops;
#else
		return generic_file_llseek(file, offset, whence);
#endif

	if (whence == SEEK_HOLE)
		offset = iomap_seek_hole(inode, offset, ops);
	else if (whence == SEEK_DATA)
		offset = iomap_seek_data(inode, offset, ops);
	else
		return generic_file_llseek(file, offset, whence);

	if (offset < 0)
		return offset;
	return vfs_setpos(file, offset, inode->i_sb->s_maxbytes);
}

const struct file_operations erofs_file_fops = {
	.llseek		= erofs_file_llseek,
	.read_iter	= erofs_file_read_iter,
	.unlocked_ioctl = erofs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = erofs_compat_ioctl,
#endif
	.mmap_prepare	= erofs_file_mmap_prepare,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= filemap_splice_read,
};
