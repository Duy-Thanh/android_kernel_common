// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Emulation
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/page_size_compat.h>

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>

#if defined(CONFIG_PAGE_SHIFT_COMPAT) && CONFIG_PAGE_SHIFT_COMPAT > PAGE_SHIFT
DEFINE_STATIC_KEY_FALSE(page_size_compat);

unsigned __page_shift(void)
{
	if (static_branch_unlikely(&page_size_compat))
		return CONFIG_PAGE_SHIFT_COMPAT;
	else
		return PAGE_SHIFT;
}

static int __init early_page_size_compat(char *buf)
{
	int ret;
	bool bool_result;

	ret = kstrtobool(buf, &bool_result);
	if (ret)
		return ret;

	if (bool_result)
		static_branch_enable(&page_size_compat);
	else
		static_branch_disable(&page_size_compat);
	return 0;
}
early_param("page_size_compat", early_page_size_compat);

#define __MMAP_RND_BITS(x)      (x - (__PAGE_SHIFT - PAGE_SHIFT))

static int __init init_mmap_rnd_bits(void)
{
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
	mmap_rnd_bits_min = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MIN);
	mmap_rnd_bits_max = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MAX);
	mmap_rnd_bits = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS);
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
	mmap_rnd_compat_bits_min = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MIN);
	mmap_rnd_compat_bits_max = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MAX);
	mmap_rnd_compat_bits = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_COMPAT_BITS);
#endif
	return 0;
}
core_initcall(init_mmap_rnd_bits);

/*
 * Updates len to avoid mapping off the end of the file.
 *
 * The length of the original mapping must be updated before
 * it's VMA is created to avoid an unaligned munmap in the
 * MAP_FIXED fixup mapping.
 */
void __filemap_len(struct inode *inode, unsigned long pgoff, unsigned long *len, bool compat)
{
	unsigned long file_size = (unsigned long) i_size_read(inode);
    /*
     * Round up, so that this is a count (not an index). This simplifies
     * the following calculations.
     */
	pgoff_t max_idx = DIV_ROUND_UP(file_size, PAGE_SIZE);
	pgoff_t index = pgoff + (*len >> PAGE_SHIFT);
	unsigned long new_len = 0;

	if (!compat)
		return;

	if (unlikely(index >= max_idx)) {
		new_len = (max_idx - pgoff)  << PAGE_SHIFT;
		/* Careful of overflows in special files */
		if (new_len > 0 && new_len < *len)
			*len = new_len;
	}
}

/*
 * This is called to fill any holes created by __filemap_len()
 * with an anonymous mapping.
 */
void __filemap_fixup(unsigned long addr, unsigned long prot, unsigned long old_len,
					 unsigned long new_len)
{
	unsigned long anon_len = old_len - new_len;
	unsigned long anon_addr = addr + new_len;
	struct mm_struct *mm = current->mm;
	unsigned long populate = 0;
	struct vm_area_struct *vma;

	if (!anon_len)
		return;

	BUG_ON(new_len > old_len);

	/* Not a filemap fault */
	if (IS_ERR_VALUE(addr))
		return;

	vma = find_vma(mm, addr);

	/*
	 * This should never happen, VMA was inserted and we still
	 * haven't released the write lock.
	 */
	BUG_ON(!vma);

	/* Only handle fixups for filemap faults */
	if (vma->vm_ops && vma->vm_ops->fault != filemap_fault)
		return;

	/*
	 * Override the the end of the file mapping that is off the file
	 * with an anonymous mapping.
	 */
	anon_addr = do_mmap(NULL, anon_addr, anon_len, prot,
					MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED|__MAP_NO_COMPAT,
					0, &populate, NULL);

	if (!IS_ERR_VALUE(anon_addr)) {
		struct anon_vma_name *anon_name = anon_vma_name_alloc("filemap_fixup");

		if (!anon_name)
			return;

		/* Label the fixup VMA */
		madvise_set_anon_name(mm, anon_addr, anon_len, anon_name);
	}
}

bool __log_alignment(const char* func, unsigned long addr, bool is_aligned)
{
	if (is_aligned)
		return true;

	if (!static_branch_unlikely(&page_size_compat))
		return false;

	pgcompat_err("%s: addr (0x%08lx) not page aligned", func, addr);

	return false;
}

#endif /* defined(CONFIG_PAGE_SHIFT_COMPAT) && CONFIG_PAGE_SHIFT_COMPAT > PAGE_SHIFT */
