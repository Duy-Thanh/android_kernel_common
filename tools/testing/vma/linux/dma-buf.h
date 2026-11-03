/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VMA_TEST_DMA_BUF_H
#define _VMA_TEST_DMA_BUF_H

/* Test files are ordinary files, never dma-buf exports. */
#define is_dma_buf_file(file) false
#define dma_buf_account_task(buf, info) 0
#define dma_buf_unaccount_task(buf, info) do { } while (0)

#endif
