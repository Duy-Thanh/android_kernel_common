// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Emulation
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/page_size_compat.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/perf_event.h>

#define MIN_PAGE_SHIFT_COMPAT (PAGE_SHIFT + 1)
#define MAX_PAGE_SHIFT_COMPAT 16 /* Max of 64KB */
#define __MMAP_RND_BITS(x)      (x - (__PAGE_SHIFT - PAGE_SHIFT))

DEFINE_STATIC_KEY_FALSE(page_shift_compat_enabled);
EXPORT_SYMBOL_GPL(page_shift_compat_enabled);

int page_shift_compat __ro_after_init = MIN_PAGE_SHIFT_COMPAT;
EXPORT_SYMBOL_GPL(page_shift_compat);

static int __init early_page_shift_compat(char *buf)
{
	int ret;

	ret = kstrtoint(buf, 10, &page_shift_compat);
	if (ret)
		return ret;

	/* Only supported on 4KB kernel */
	if (PAGE_SHIFT != 12)
		return -ENOTSUPP;

	if (page_shift_compat < MIN_PAGE_SHIFT_COMPAT ||
		page_shift_compat > MAX_PAGE_SHIFT_COMPAT)
		return -EINVAL;

	static_branch_enable(&page_shift_compat_enabled);

	return 0;
}
early_param("page_shift", early_page_shift_compat);

static int __init init_mmap_rnd_bits(void)
{
	if (!static_branch_unlikely(&page_shift_compat_enabled))
		return 0;

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
	mmap_rnd_bits_min = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MIN);
	mmap_rnd_bits_max = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MAX);
	mmap_rnd_bits = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS);
#endif

	return 0;
}
core_initcall(init_mmap_rnd_bits);

/*
 * Returns size of the portion of the VMA backed by the
 * underlying file.
 */
unsigned long ___filemap_len(struct inode *inode, unsigned long pgoff, unsigned long len,
			     unsigned long flags)
{
	unsigned long file_size;
	unsigned long filemap_len;
	pgoff_t max_pgcount;
	pgoff_t last_pgoff;

	if (flags & __MAP_NO_COMPAT)
		return len;

	file_size = (unsigned long) i_size_read(inode);

	/*
	 * Round up, so that this is a count (not an index). This simplifies
	 * the following calculations.
	 */
	max_pgcount = DIV_ROUND_UP(file_size, PAGE_SIZE);
	last_pgoff = pgoff + (len >> PAGE_SHIFT);

	if (unlikely(last_pgoff >= max_pgcount)) {
		filemap_len = (max_pgcount - pgoff)  << PAGE_SHIFT;
		/* Careful of underflows in special files */
		if (filemap_len > 0 && filemap_len < len)
			return filemap_len;
	}

	return len;
}

/*
 * Given a file mapping of 48KiB backed by a file of size 18KiB, the
 * faulting behaviour of the different page-size configurations is
 * explained below.

 * In a 4KiB base page size system, when a file backed mapping extends
 * past the end of the file, accessed is allowed to the entire last
 * page that at least partially corresponds to valid offsets on the
 * file. However, access beyond that page will generate a SIGBUS, since
 * the offset we are trying to fault doesn't correspond to anywhere on
 * the backing file.
 *
 * This is illustrated below. The offsets are given in units of KiB.
 *
 *                    Access OK (4KiB page paritially backed by file)
 *                                │
 *    ┌──────────────────────────┬┼─┬─────────────────────────────────────────┐
 *    │                          │▼ │                                         │
 *    │       File backed        │  │     SIGBUS (Invalid filemap_fault)      │
 *    │                          │  │                                         │
 *    └──────────────────────────┴──┴─────────────────────────────────────────┘
 *
 *    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
 *    0     4     8     12   16    20    24    28    32    36    40    44    48
 *
 * In a x86_64 emulated 16KiB page size system, userspace beleives the page
 * size is 16KiB and therefore shoud be able to access the entire last 16KiB page
 * that is at least partially backed by the file. However, the kernel is still a
 * 4KiB kernel and will fault at each 4KiB page that makes up the "emulated"
 * 16KiB page, which will generate a SIGBUS any of the 4KiB pages making up the
 * 16KiB expect the first is being faulted.
 *
 *                    Access OK (4KiB page paritially backed by file)
 *                                │
 *    ┌──────────────────────────┬┼─┬─────────────────────────────────────────┐
 *    │                          │▼ │                                         │
 *    │       File backed        │  │     SIGBUS (Invalid filemap_fault)      │
 *    │                          │  │                                         │
 *    └──────────────────────────┴──┴─────────────────────────────────────────┘
 *
 *    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
 *    0     4     8     12   16    20    24    28    32    36    40    44    48
 *
 * To fix this semantic in the emulated page size mode an anonymous mapping is
 * inserted into to replace the full 4KiB pages that make up the last 16KiB page
 * partially backed by the file.
 *
 *
 *                    Access OK (4KiB page paritially backed by file)
 *                                │
 *                                │        ┌─── Access OK
 *                                │        │   (16KiB page partially backed
 *                                │        │       by file)
 *                                │        │
 *    ┌──────────────────────────┬┼─┬──────┼──────────┬───────────────────────┐
 *    │                          │▼ │      ▼          │      SIGBUS           │
 *    │       File backed        │  │   Access OK     │(Invalid filemap fault)│
 *    │                          │  │  (Anon Mapping) │                       │
 *    └──────────────────────────┴──┴─────────────────┴───────────────────────┘
 *
 *    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
 *    0     4     8     12   16    20    24    28    32    36    40    44    48
 */
void ___filemap_fixup(unsigned long addr, unsigned long prot, unsigned long file_backed_len,
		      unsigned long len)
{
	unsigned long anon_addr = addr + file_backed_len;
	unsigned long __offset = __offset_in_page(anon_addr);
	unsigned long anon_len = __offset ? __PAGE_SIZE - __offset : 0;
	struct mm_struct *mm = current->mm;
	unsigned long populate = 0;
	struct vm_area_struct *vma;

	if (!anon_len)
		return;

	BUG_ON(anon_len >= __PAGE_SIZE);

	/* The original do_mmap() failed */
	if (IS_ERR_VALUE(addr))
		return;

	vma = find_vma(mm, addr);

	/*
	 * This should never happen, VMA was inserted and we still
	 * haven't released the mmap write lock.
	 */
	BUG_ON(!vma);

	/*
	 * Insert fixup vmas for file backed, including tmpfs (shmem) backed, VMAs.
	 *
	 * Faulting off the end of a file will result in SIGBUS since there is no
	 * file page for the given file offset.
	 *
	 * shmem pages live in page cache or swap cache. Looking up a page cache
	 * page with an index (pgoff) beyond the file is invalid and will result
	 * in shmem_get_folio_gfp() returning -EINVAL.
	 *
	 * It's not pratical to maintain a list of vm_ops for the constantly
	 * changing list of supported filesystems on Android, so only test that
	 * vm_ops exists.
	 */
	if (!vma->vm_ops)
		return;

	/*
	 * Override the partial emulated page of the file backed portion of the VMA
	 * with an anonymous mapping.
	 */
	anon_addr = do_mmap(NULL, anon_addr, anon_len, prot,
					MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|__MAP_NO_COMPAT,
					0, 0, &populate, NULL);
}

/*
 * Folds any anon fixup entries created by ___filemap_fixup()
 * into the previous mapping so that /proc/<pid>/[s]maps don't
 * show unaliged entries.
 */
void __fold_filemap_fixup_entry(struct vma_iterator *iter, unsigned long *end)
{
	struct vm_area_struct *next_vma;

	/* Not emulating page size? */
	if (!static_branch_unlikely(&page_shift_compat_enabled))
		return;

	/* Advance iterator */
	next_vma = vma_next(iter);

	/* If fixup VMA, adjust the end to cover its extent */
	if (next_vma && (next_vma->vm_flags & __VM_NO_COMPAT)) {
		*end = next_vma->vm_end;
		return;
	}

	/* Rewind iterator */
	vma_prev(iter);
}

#define SWAP_MAGIC	"SWAPSPACE2"
#define SWAP_MAGIC_LEN	10

/*
 * Rewrite a swap header laid out in pages of (PAGE_SIZE << shift) in the
 * layout of PAGE_SIZE pages: the magic moves to the end of the first
 * PAGE_SIZE page, and last_page counts PAGE_SIZE pages. The rest of the old
 * header page becomes swap space.
 *
 * Headers that swapon() would refuse anyway are left alone. Bad pages are
 * refused too: nothing on Android writes them, and each one would become
 * several PAGE_SIZE entries, which swapon() can't read while badpages[] is
 * declared with one entry (that traps with CONFIG_UBSAN_BOUNDS).
 */
static int __convert_swap_header(union swap_header *hdr, unsigned int shift,
				 u64 nr_pages)
{
	unsigned int last_page = hdr->info.last_page;

	if (hdr->info.version != 1) {
		pgcompat_err("Unsupported swap header version %u", hdr->info.version);
		return -EINVAL;
	}
	if (!last_page) {
		pgcompat_err("Empty swap area");
		return -EINVAL;
	}
	if (hdr->info.nr_badpages) {
		pgcompat_err("Swap header with bad pages not supported");
		return -EINVAL;
	}
	if (last_page >= U32_MAX >> shift) {
		pgcompat_err("Swap area too large to convert");
		return -EINVAL;
	}
	/* Also refuses a header that a failed conversion left half written */
	if (((u64)last_page + 1) << shift > nr_pages) {
		pgcompat_err("Swap area larger than its file or device");
		return -EINVAL;
	}

	hdr->info.last_page = ((last_page + 1) << shift) - 1;
	memcpy(hdr->magic.magic, SWAP_MAGIC, SWAP_MAGIC_LEN);

	return 0;
}

/*
 * mkswap lays a swap area out in pages of the page size that userspace sees.
 * In the emulated mode, that puts the magic in the last 10 bytes of the first
 * __PAGE_SIZE page, and makes last_page and badpages[] count __PAGE_SIZE
 * pages. swapon() reads the header in the layout of PAGE_SIZE pages, so it
 * would find no magic, or with the magic moved alone, use only a fraction of
 * the area.
 *
 * Convert such a header to the PAGE_SIZE layout and write it back through
 * the filesystem, before swapon() takes the inode lock, so that the
 * filesystem sees an ordinary write. Sync it before swapon() goes on, as
 * swapping and discard can overwrite the old magic. The next swapon() then
 * finds a PAGE_SIZE header, which needs no conversion. A header or file that
 * swapon() refuses is left as it is.
 */
int __fixup_swap_header(struct file *swap_file, struct address_space *mapping)
{
	unsigned int shift = __PAGE_SHIFT - PAGE_SHIFT;
	struct dentry *dentry = swap_file->f_path.dentry;
	struct inode *inode = mapping->host;
	loff_t magic_pos = __PAGE_SIZE - SWAP_MAGIC_LEN;
	char magic[SWAP_MAGIC_LEN];
	union swap_header *hdr;
	struct folio *folio;
	loff_t pos = 0;
	ssize_t written;
	int error = 0;

	/* swapon() fails these itself; leave them as they are */
	if (!shift || !mapping->a_ops->read_folio || IS_SWAPFILE(inode) ||
	    d_unlinked(dentry) || cant_mount(dentry) ||
	    mapping_min_folio_order(mapping) > 0 ||
	    i_size_read(inode) < __PAGE_SIZE)
		return 0;

	folio = read_mapping_folio(mapping, magic_pos >> PAGE_SHIFT, swap_file);
	if (IS_ERR(folio)) {
		pgcompat_err("Failed reading swap magic page");
		return PTR_ERR(folio);
	}
	memcpy_from_folio(magic, folio, offset_in_folio(folio, magic_pos),
			  SWAP_MAGIC_LEN);
	folio_put(folio);

	/* Nothing to do; mkswap tool may have hardcoded a 4096 page size */
	if (memcmp(magic, SWAP_MAGIC, SWAP_MAGIC_LEN))
		return 0;

	hdr = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	folio = read_mapping_folio(mapping, 0, swap_file);
	if (IS_ERR(folio)) {
		pgcompat_err("Failed reading swap header page");
		error = PTR_ERR(folio);
		goto out;
	}
	memcpy_from_folio((char *)hdr, folio, 0, PAGE_SIZE);
	folio_put(folio);

	/* A PAGE_SIZE header, such as one converted by an earlier swapon() */
	if (!memcmp(hdr->magic.magic, SWAP_MAGIC, SWAP_MAGIC_LEN))
		goto out;

	error = __convert_swap_header(hdr, shift, i_size_read(inode) >> PAGE_SHIFT);
	if (error)
		goto out;

	/*
	 * RLIMIT_FSIZE would cut the write short, and leave last_page scaled
	 * without the magic that marks the header converted.
	 */
	if (S_ISREG(inode->i_mode) && rlimit(RLIMIT_FSIZE) < PAGE_SIZE) {
		pgcompat_err("RLIMIT_FSIZE too low to convert swap header");
		error = -EFBIG;
		goto out;
	}

	written = kernel_write(swap_file, hdr, PAGE_SIZE, &pos);
	/* A concurrent swapon() of the file won, and swapon() returns -EBUSY */
	if (written == -ETXTBSY)
		goto out;
	if (written != PAGE_SIZE) {
		error = written < 0 ? written : -EIO;
		pgcompat_err("Failed writing swap header: %d", error);
		goto out;
	}
	error = vfs_fsync_range(swap_file, 0, PAGE_SIZE - 1, 1);
	if (error) {
		pgcompat_err("Failed syncing swap header: %d", error);
		goto out;
	}
	pr_info("pgcompat: %pD: converted swap header from %luK to %luK pages\n",
		swap_file, __PAGE_SIZE >> 10, PAGE_SIZE >> 10);
out:
	kfree(hdr);
	return error;
}

#if IS_ENABLED(CONFIG_PERF_EVENTS)
static int __init init_sysctl_perf_event_mlock(void)
{
	if (!static_branch_unlikely(&page_shift_compat_enabled))
		return 0;

	/* Minimum for 512 kiB + 1 user control page */
	sysctl_perf_event_mlock = 512 + (__PAGE_SIZE / 1024); /* 'free' kiB per user */

	return 0;
}
core_initcall(init_sysctl_perf_event_mlock);
#endif
