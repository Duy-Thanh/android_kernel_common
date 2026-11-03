/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VMA_TEST_PGSIZE_MIGRATION_H
#define _VMA_TEST_PGSIZE_MIGRATION_H

/* No Android ELF padding is attached to the harness's VMAs. */
#define VM_PAD_MASK 0
#define is_mergable_pad_vma(vma, flags) true
#define split_pad_vma(vma, new, addr, below) do { } while (0)

#endif
